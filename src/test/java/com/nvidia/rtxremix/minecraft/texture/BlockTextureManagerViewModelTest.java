/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.texture;

import com.nvidia.rtxremix.minecraft.asset.RemixTexture;
import java.lang.reflect.Field;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

class BlockTextureManagerViewModelTest {
    private static BlockTextureManager manager() throws Exception {
        BlockTextureManager manager = new BlockTextureManager(null);
        Field albedo = BlockTextureManager.class.getDeclaredField("atlasAlbedo");
        albedo.setAccessible(true);
        // Material-description tests do not create GPU textures or native handles.
        albedo.set(manager, new RemixTexture(1, 1, 0, 0, null));
        return manager;
    }

    @Test
    void cameraShrinkPreservesBeerLambertAbsorption() throws Exception {
        var manager = manager();
        var world = manager.buildViewModelTranslucentMaterialInfo(1f, false);
        var held = manager.buildViewModelTranslucentMaterialInfo(0.1f, false);
        assertEquals(0.35f, world.translucentExt.transmittanceMeasurementDistance);
        assertEquals(0.035f, held.translucentExt.transmittanceMeasurementDistance, 1e-7f);
        double authoredTransmission = 0.2;
        double originalPathLength = 0.4;
        assertEquals(Math.pow(authoredTransmission,
                        originalPathLength / world.translucentExt.transmittanceMeasurementDistance),
                Math.pow(authoredTransmission,
                        originalPathLength * 0.1 / held.translucentExt.transmittanceMeasurementDistance), 1e-7);
        assertEquals(world.translucentExt.refractiveIndex, held.translucentExt.refractiveIndex);
        assertEquals(world.translucentExt.transmittanceTexture, held.translucentExt.transmittanceTexture);
        assertEquals(0, held.translucentExt.useDiffuseLayer);
    }

    @Test
    void flatItemSurfaceTintDoesNotMutateTheVolumeDescription() throws Exception {
        var manager = manager();
        var volume = manager.buildViewModelTranslucentMaterialInfo(0.1f, false);
        var card = manager.buildViewModelTranslucentMaterialInfo(0.1f, true);
        assertNotSame(volume.translucentExt, card.translucentExt);
        assertEquals(0, volume.translucentExt.useDiffuseLayer);
        assertEquals(1, card.translucentExt.useDiffuseLayer);
        assertEquals(0.035f, card.translucentExt.transmittanceMeasurementDistance, 1e-7f);
        assertEquals(1.52f, card.translucentExt.refractiveIndex);
        assertEquals(0.35f, manager.buildViewModelTranslucentMaterialInfo(1f, false)
                .translucentExt.transmittanceMeasurementDistance);
    }
}
