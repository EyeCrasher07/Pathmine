/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft;

import com.nvidia.rtxremix.RemixException;
import com.nvidia.rtxremix.minecraft.weather.RemixWeatherRenderer;
import net.minecraft.block.enums.CameraSubmersionType;
import net.minecraft.client.render.BuiltBuffer;
import net.minecraft.client.render.Camera;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.Vec3d;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;

import java.lang.reflect.Method;

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
    void rejectedProjectionDiscardsCapturedRainAndSnow(float fieldOfView, int width, int height) {
        RemixWeatherRenderer weather = new RemixWeatherRenderer();
        TrackingBuiltBuffer rain = new TrackingBuiltBuffer();
        TrackingBuiltBuffer snow = new TrackingBuiltBuffer();
        weather.beginCaptureFrame(null);
        weather.capture(rain, Identifier.ofVanilla("textures/environment/rain.png"));
        weather.capture(snow, Identifier.ofVanilla("textures/environment/snow.png"));

        assertFalse(RemixRenderer.prepareCameraProjection(fieldOfView, width, height, weather));

        assertEquals(1, rain.closeCount);
        assertEquals(1, snow.closeCount);
        assertDoesNotThrow(() -> weather.render(1, 1));
        weather.cleanup();
        assertEquals(1, rain.closeCount);
        assertEquals(1, snow.closeCount);
    }

    @Test
    void readyProjectionKeepsWeatherUntilALaterProjectionIsRejected() {
        RemixWeatherRenderer weather = new RemixWeatherRenderer();
        TrackingBuiltBuffer rain = new TrackingBuiltBuffer();
        TrackingBuiltBuffer snow = new TrackingBuiltBuffer();
        weather.beginCaptureFrame(null);
        weather.capture(rain, Identifier.ofVanilla("textures/environment/rain.png"));
        weather.capture(snow, Identifier.ofVanilla("textures/environment/snow.png"));

        assertTrue(RemixRenderer.prepareCameraProjection(76.90074f, 1280, 720, weather));
        weather.updateCamera(new Camera(), 76.90074f, 1280.0f / 720.0f);
        assertEquals(0, rain.closeCount);
        assertEquals(0, snow.closeCount);

        assertFalse(RemixRenderer.prepareCameraProjection(0.0f, 1280, 720, weather));
        assertEquals(1, rain.closeCount);
        assertEquals(1, snow.closeCount);
        assertDoesNotThrow(() -> weather.render(1, 1));
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
                public Vec3d getPos() {
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
                "updateCameraMedium", Camera.class, Vec3d.class, float.class, float.class);
        update.setAccessible(true);
        for (RuntimeException error : new RuntimeException[] {
                new RemixException("medium failed"), new IllegalStateException("medium failed")}) {
            Camera camera = new Camera() {
                @Override
                public CameraSubmersionType getSubmersionType() {
                    throw error;
                }
            };

            assertDoesNotThrow(() -> update.invoke(renderer, camera, Vec3d.ZERO, 0.0f, 0.0f));
        }
    }

    private static final class TrackingBuiltBuffer extends BuiltBuffer {
        private int closeCount;

        private TrackingBuiltBuffer() {
            super(null, new DrawParameters(null, 4, 6, null, null));
        }

        @Override
        public void close() {
            closeCount++;
        }
    }
}
