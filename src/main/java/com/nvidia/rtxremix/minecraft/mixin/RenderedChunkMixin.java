/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.nvidia.rtxremix.minecraft.terrain.capture.RenderRegionStoragePool;
import com.nvidia.rtxremix.minecraft.terrain.capture.RegionSnapshotRelease;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Mutable;
import org.spongepowered.asm.mixin.Shadow;

import java.util.concurrent.atomic.AtomicInteger;
import net.minecraft.util.SimpleBitStorage;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.chunk.PalettedContainer;

/**
 * A section snapshot for a render region whose block-state copy draws its storage from
 * {@link RenderRegionStoragePool} and give it back when the last region holding the snapshot
 * has been consumed. The region cache holds its own reference while neighboring regions
 * are assembled from the same snapshot.
 */
@Mixin(targets = "net.minecraft.client.renderer.chunk.SectionCopy")
public abstract class RenderedChunkMixin implements RegionSnapshotRelease, com.nvidia.rtxremix.minecraft.terrain.capture.SnapshotColumnAccess {

    @Shadow @Final private net.minecraft.world.level.LevelHeightAccessor levelHeightAccessor;

    @Override
    public boolean rtxremix$columnWasLoaded() {
        return !(levelHeightAccessor instanceof net.minecraft.world.level.chunk.EmptyLevelChunk);
    }

    @Shadow
    @Final
    @Mutable
    private PalettedContainer<BlockState> section;

    private final AtomicInteger rtxremix$regions = new AtomicInteger();

    @Override
    public void rtxremix$retain() {
        rtxremix$regions.incrementAndGet();
    }

    @Override
    public void rtxremix$release() {
        int remaining = rtxremix$regions.decrementAndGet();
        if (remaining < 0) {
            throw new IllegalStateException("Section snapshot released more than retained");
        }
        if (remaining != 0 || section == null) {
            return;
        }
        if (section.data.storage() instanceof SimpleBitStorage packed) {
            RenderRegionStoragePool.release(packed.getRaw());
        }
        section = null;
    }
}
