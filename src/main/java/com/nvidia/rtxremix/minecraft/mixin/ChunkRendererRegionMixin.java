/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.nvidia.rtxremix.minecraft.terrain.capture.RegionSnapshotRelease;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import java.util.concurrent.atomic.AtomicBoolean;
import net.minecraft.client.renderer.chunk.SectionCopy;
import net.minecraft.client.renderer.chunk.RenderSectionRegion;

/**
 * A render region holds one reference on each of its 27 section snapshots from construction
 * until {@link #rtxremix$release}, which the rebuild task that consumed it calls exactly once;
 * see {@link RebuildTaskMixin}.
 */
@Mixin(RenderSectionRegion.class)
public abstract class ChunkRendererRegionMixin implements RegionSnapshotRelease, com.nvidia.rtxremix.minecraft.terrain.capture.RegionNeighbourhood {

    private int rtxremix$snapshotMask;

    @Override
    public int rtxremix$snapshotMask() {
        return rtxremix$snapshotMask;
    }

    @Shadow
    @Final
    private SectionCopy[] sections;

    private final AtomicBoolean rtxremix$released = new AtomicBoolean();

    @Inject(method = "<init>", at = @At("RETURN"))
    private void rtxremix$retainSnapshots(CallbackInfo ci) {
        for (SectionCopy snapshot : sections) {
            ((RegionSnapshotRelease) snapshot).rtxremix$retain();
        }
        for (int k = 0; k < com.nvidia.rtxremix.minecraft.terrain.NeighbourReflagFilter.NEIGHBOUR_COUNT; k++) {
            int x = com.nvidia.rtxremix.minecraft.terrain.NeighbourReflagFilter.DX[k] + 1;
            int z = com.nvidia.rtxremix.minecraft.terrain.NeighbourReflagFilter.DZ[k] + 1;
            SectionCopy snapshot = sections[x + 3 + z * 9];
            if (((com.nvidia.rtxremix.minecraft.terrain.capture.SnapshotColumnAccess) snapshot).rtxremix$columnWasLoaded()) {
                rtxremix$snapshotMask |= com.nvidia.rtxremix.minecraft.terrain.NeighbourReflagFilter.bit(k);
            }
        }
    }

    @Override
    public void rtxremix$retain() {
        throw new UnsupportedOperationException("regions are held by exactly one rebuild task");
    }

    @Override
    public void rtxremix$release() {
        if (!rtxremix$released.compareAndSet(false, true)) {
            return;
        }
        for (SectionCopy snapshot : sections) {
            ((RegionSnapshotRelease) snapshot).rtxremix$release();
        }
    }
}
