/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.entity;

import net.minecraft.client.renderer.rendertype.RenderTypes;
import net.minecraft.client.renderer.rendertype.RenderType;
import net.minecraft.client.renderer.rendertype.RenderSetup;
import net.minecraft.client.renderer.RenderPipelines;
import net.minecraft.resources.Identifier;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Pins the two layer verdicts that decide what the submitter does with a layer's parts.
 *
 * <p>The surface-mode verdict decides the material a layer is built with. The
 * streamed-per-frame verdict decides which layers bypass the mesh pool: a false positive
 * rebuilds a shareable mesh every frame, a false negative pools a mesh whose content
 * never repeats and fills the pool with dead entries. Standard render types also
 * exercise the actual pipeline, texture and vertex-format contracts.
 */
class RenderLayerUtilsTest {

    private static final Identifier TEXTURE = Identifier.fromNamespaceAndPath("pathmine", "test_material");

    @Test
    void standardPipelinesKeepTheirMaterialBehavior() {
        assertEquals(RenderLayerUtils.SurfaceMode.OPAQUE,
                RenderLayerUtils.surfaceMode(RenderTypes.entitySolid(TEXTURE)));
        assertEquals(RenderLayerUtils.SurfaceMode.CUTOUT,
                RenderLayerUtils.surfaceMode(RenderTypes.entityCutout(TEXTURE)));
        assertEquals(RenderLayerUtils.SurfaceMode.TRANSLUCENT,
                RenderLayerUtils.surfaceMode(RenderTypes.entityTranslucent(TEXTURE)));
        assertEquals(RenderLayerUtils.SurfaceMode.EMISSIVE,
                RenderLayerUtils.surfaceMode(RenderTypes.entityTranslucentEmissive(TEXTURE)));
        assertEquals(RenderLayerUtils.SurfaceMode.EMISSIVE,
                RenderLayerUtils.surfaceMode(RenderTypes.eyes(TEXTURE)));
        assertEquals(RenderLayerUtils.SurfaceMode.CUTOUT,
                RenderLayerUtils.surfaceMode(RenderTypes.text(TEXTURE)));
    }

    @Test
    void captureReadsTheTextureAndRejectsSeeThroughText() {
        assertEquals(TEXTURE, RenderLayerUtils.capturableTexture(RenderTypes.entitySolid(TEXTURE)).orElseThrow());
        assertEquals(TEXTURE, RenderLayerUtils.capturableTexture(RenderTypes.text(TEXTURE)).orElseThrow());
        assertTrue(RenderLayerUtils.capturableTexture(RenderTypes.textSeeThrough(TEXTURE)).isEmpty());
    }

    @Test
    void animatedBeamLayersAndLeashesBypassStaticMeshPooling() {
        assertTrue(RenderLayerUtils.isStreamedPerFrame(RenderTypes.beaconBeam(TEXTURE, false)));
        assertTrue(RenderLayerUtils.isStreamedPerFrame(RenderTypes.beaconBeam(TEXTURE, true)));
        assertTrue(RenderLayerUtils.isStreamedPerFrame(RenderTypes.leash()));
        assertFalse(RenderLayerUtils.isStreamedPerFrame(RenderTypes.entitySolid(TEXTURE)));
    }

    @Test
    void untexturedCaptureIncludesLeashGeometryAndExcludesTheWaterMask() {
        assertTrue(RenderLayerUtils.isCapturableUntexturedLayer(RenderTypes.leash()));
        RenderType waterMask = RenderType.create("water_mask_test",
                RenderSetup.builder(RenderPipelines.WATER_MASK).createRenderSetup());
        assertFalse(RenderLayerUtils.isCapturableUntexturedLayer(waterMask));
    }

    @ParameterizedTest
    @CsvSource({
            "false, false, true, false, true, CUTOUT",
            "false, false, true, false, false, TRANSLUCENT",
            "true, false, true, false, true, EMISSIVE",
            "false, true, false, false, false, EMISSIVE",
            "false, false, false, true, false, OPAQUE"
    })
    void surfaceModeFollowsProgramAndTransparencyPrecedence(
            boolean emissiveProgram, boolean additiveTransparency,
            boolean translucentTransparency, boolean solidProgram,
            boolean textProgram, RenderLayerUtils.SurfaceMode expected) {
        assertEquals(expected, RenderLayerUtils.classifySurfaceMode(
                emissiveProgram, additiveTransparency, translucentTransparency,
                solidProgram, textProgram));
    }

    @ParameterizedTest
    @CsvSource({
            "TRIANGLE_STRIP, false, true",
            "QUADS, true, true",
            "TRIANGLE_STRIP, true, true",
            "QUADS, false, false"
    })
    void stripsAndBeaconBeamsAreStreamedPerFrame(
            RenderLayerUtils.Topology topology, boolean beaconBeamProgram, boolean expected) {
        assertEquals(expected, RenderLayerUtils.classifyStreamedPerFrame(topology, beaconBeamProgram));
    }
}
