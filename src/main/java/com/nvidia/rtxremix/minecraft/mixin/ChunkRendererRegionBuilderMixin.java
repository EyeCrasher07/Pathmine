/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.nvidia.rtxremix.minecraft.terrain.capture.RegionSnapshotRelease;
import com.nvidia.rtxremix.minecraft.terrain.capture.RegionBuilderRelease;
import com.nvidia.rtxremix.minecraft.terrain.capture.RenderRegionStoragePool;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import it.unimi.dsi.fastutil.longs.Long2ObjectMap;
import net.minecraft.client.renderer.chunk.SectionCopy;
import net.minecraft.client.renderer.chunk.RenderRegionCache;
import net.minecraft.world.level.chunk.LevelChunk;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;

/**
 * The builder's reference on its cached snapshots; see {@link RegionBuilderRelease}. The
 * Each newly cached copy owns one reference independently of the regions sharing it.
 */
@Mixin(RenderRegionCache.class)
public abstract class ChunkRendererRegionBuilderMixin implements RegionBuilderRelease {

    @Shadow
    @Final
    private Long2ObjectMap<SectionCopy> sectionCopyCache;

    @WrapOperation(method = "lambda$getSectionDataCopy$0",
            at = @At(value = "NEW", target = "(Lnet/minecraft/world/level/chunk/LevelChunk;I)Lnet/minecraft/client/renderer/chunk/SectionCopy;"))
    private static SectionCopy rtxremix$copySection(
            LevelChunk chunk, int sectionIndex, Operation<SectionCopy> original) {
        RenderRegionStoragePool.beginCopying();
        SectionCopy snapshot;
        try {
            snapshot = original.call(chunk, sectionIndex);
        } finally {
            RenderRegionStoragePool.endCopying();
        }
        ((RegionSnapshotRelease) snapshot).rtxremix$retain();
        return snapshot;
    }

    @Override
    public void rtxremix$releaseSnapshots() {
        for (SectionCopy snapshot : sectionCopyCache.values()) {
            ((RegionSnapshotRelease) snapshot).rtxremix$release();
        }
        sectionCopyCache.clear();
    }
}
