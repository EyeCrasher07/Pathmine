/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft;

import com.nvidia.rtxremix.RemixException;
import com.nvidia.rtxremix.minecraft.weather.RemixWeatherRenderer;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;

import java.lang.reflect.Method;
import net.minecraft.client.Camera;
import net.minecraft.world.level.material.FogType;
import net.minecraft.world.phys.Vec3;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class RemixRendererCameraFrameTest {

    @Test
    void cameraFrameAdvancesOnlyWhenMinecraftWillRenderAWorld() {
        assertTrue(RemixRenderer.shouldAdvanceWorldRenderFrame(true, true, true));

        assertFalse(RemixRenderer.shouldAdvanceWorldRenderFrame(false, true, true));
        assertFalse(RemixRenderer.shouldAdvanceWorldRenderFrame(true, false, true));
        assertFalse(RemixRenderer.shouldAdvanceWorldRenderFrame(true, true, false));
    }

    @Test
    void cameraProjectionWaitsForVanillaFieldOfViewInitialization() {
        assertFalse(RemixRenderer.prepareCameraProjection(0.0f, 1280, 720, null));
        assertTrue(RemixRenderer.prepareCameraProjection(76.90074f, 1280, 720, null));
    }

    @Test
    void cameraProjectionAcceptsFinitePerspectiveAnglesWithoutClamping() {
        for (float fieldOfView : new float[] {0.01f, 30.0f, 70.0f, 110.0f, 179.99f}) {
            assertTrue(RemixRenderer.prepareCameraProjection(fieldOfView, 1280, 720, null));
        }
        for (float fieldOfView : new float[] {
                -70.0f, 180.0f, Float.NaN, Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY}) {
            assertFalse(RemixRenderer.prepareCameraProjection(fieldOfView, 1280, 720, null));
        }
    }

    @Test
    void cameraProjectionWaitsForADrawableFramebuffer() {
        assertFalse(RemixRenderer.prepareCameraProjection(70.0f, 0, 720, null));
        assertFalse(RemixRenderer.prepareCameraProjection(70.0f, 1280, 0, null));
        assertFalse(RemixRenderer.prepareCameraProjection(70.0f, -1, 720, null));
        assertFalse(RemixRenderer.prepareCameraProjection(70.0f, 1280, -1, null));
        assertTrue(RemixRenderer.prepareCameraProjection(70.0f, 1, 1, null));
    }

    @ParameterizedTest
    @CsvSource({
            "0, 1280, 720",
            "-70, 1280, 720",
            "180, 1280, 720",
            "NaN, 1280, 720",
            "-Infinity, 1280, 720",
            "Infinity, 1280, 720",
            "70, 0, 720",
            "70, 1280, 0",
            "70, -1, 720",
            "70, 1280, -1"
    })
    void rejectedProjectionDiscardsCapturedRainAndSnow(float fieldOfView, int width, int height) throws Exception {
        RemixWeatherRenderer weather = new RemixWeatherRenderer();
        setPendingWeather(weather, 2, 3);
        assertFalse(RemixRenderer.prepareCameraProjection(fieldOfView, width, height, weather));
        assertDoesNotThrow(() -> weather.render(1, 1));
        assertEquals(0, pendingColumns(weather));
    }

    @Test
    void readyProjectionKeepsWeatherUntilALaterProjectionIsRejected() throws Exception {
        RemixWeatherRenderer weather = new RemixWeatherRenderer();
        setPendingWeather(weather, 2, 3);
        assertTrue(RemixRenderer.prepareCameraProjection(76.90074f, 1280, 720, weather));
        assertEquals(5, pendingColumns(weather));
        assertFalse(RemixRenderer.prepareCameraProjection(0.0f, 1280, 720, weather));
        assertEquals(0, pendingColumns(weather));
        assertDoesNotThrow(() -> weather.render(1, 1));
    }

    private static void setPendingWeather(RemixWeatherRenderer weather, int rain, int snow) throws Exception {
        var r = RemixWeatherRenderer.class.getDeclaredField("rainColumns");
        var s = RemixWeatherRenderer.class.getDeclaredField("snowColumns");
        r.setAccessible(true);
        s.setAccessible(true);
        r.setInt(weather, rain);
        s.setInt(weather, snow);
    }

    private static int pendingColumns(RemixWeatherRenderer weather) throws Exception {
        var r = RemixWeatherRenderer.class.getDeclaredField("rainColumns");
        var s = RemixWeatherRenderer.class.getDeclaredField("snowColumns");
        r.setAccessible(true);
        s.setAccessible(true);
        return r.getInt(weather) + s.getInt(weather);
    }

    @Test
    void cameraErrorsStayLocalToTheCameraUpdate() throws Exception {
        RemixRenderer renderer = new RemixRenderer();
        Method update = RemixRenderer.class.getDeclaredMethod("updateCamera", Camera.class, float.class);
        update.setAccessible(true);
        for (RuntimeException error : new RuntimeException[] {
                new RemixException("camera failed"), new IllegalStateException("camera failed")}) {
            Camera camera = new Camera() {
                @Override
                public Vec3 position() {
                    throw error;
                }
            };

            assertDoesNotThrow(() -> update.invoke(renderer, camera, 0.0f));
        }
    }

    @Test
    void mediumErrorsStayLocalToTheMediumUpdate() throws Exception {
        RemixRenderer renderer = new RemixRenderer();
        Method update = RemixRenderer.class.getDeclaredMethod(
                "updateCameraMedium", Camera.class, Vec3.class, float.class, float.class);
        update.setAccessible(true);
        for (RuntimeException error : new RuntimeException[] {
                new RemixException("medium failed"), new IllegalStateException("medium failed")}) {
            Camera camera = new Camera() {
                @Override
                public FogType getFluidInCamera() {
                    throw error;
                }
            };

            assertDoesNotThrow(() -> update.invoke(renderer, camera, Vec3.ZERO, 0.0f, 0.0f));
        }
    }

}
