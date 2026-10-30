/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.entity;

import java.util.concurrent.atomic.AtomicInteger;
import net.fabricmc.fabric.api.client.rendering.v1.FeatureRendererRegistry;
import net.minecraft.client.renderer.feature.FeatureRendererType;
import net.minecraft.client.renderer.feature.ItemFeatureRenderer;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

class RemixFeatureCaptureTest {
    @Test
    void registeredFeaturesHaveIndependentTessellatorsAtEachCaptureDepth() {
        FeatureRendererType<ItemFeatureRenderer.Submit> type =
                FeatureRendererType.create("Pathmine capture registration test");
        AtomicInteger created = new AtomicInteger();
        FeatureRendererRegistry.register(type, () -> {
            created.incrementAndGet();
            return new ItemFeatureRenderer();
        });

        try (var outer = RemixFeatureCapture.createRenderers();
             var nested = RemixFeatureCapture.createRenderers()) {
            assertNotNull(outer.get(type), "Capture must include Fabric's registered features");
            assertNotNull(nested.get(type));
            assertNotSame(outer.get(type), nested.get(type),
                    "A nested submission must not overwrite its parent's tessellator scratch state");
            assertEquals(2, created.get());
        }
    }
}
