/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.nvidia.rtxremix.minecraft.terrain.BuiltChunkNeighbourhoodAccess;
import com.nvidia.rtxremix.minecraft.terrain.ChunkBuilderBlockEntityAccess;
import com.nvidia.rtxremix.minecraft.terrain.SectionLeftRenderSetQueue;
import com.nvidia.rtxremix.minecraft.terrain.capture.RegionNeighbourhood;
import com.nvidia.rtxremix.minecraft.terrain.visibility.ChunkSlotMoveLog;
import net.minecraft.client.renderer.chunk.RenderSectionRegion;
import net.minecraft.client.renderer.chunk.SectionMesh;
import net.minecraft.client.renderer.chunk.SectionRenderDispatcher;
import net.minecraft.core.SectionPos;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** Tracks a rotating section slot and the immutable neighborhood its build consumed. */
@Mixin(SectionRenderDispatcher.RenderSection.class)
public abstract class BuiltChunkMixin implements ChunkBuilderBlockEntityAccess, BuiltChunkNeighbourhoodAccess {
    @Shadow @Final public int index;
    @Shadow public abstract long getSectionNode();
    private boolean rtxremix$hasBlockEntities;
    private int rtxremix$snapshotMask;

    @Override public boolean rtxremix$hasBlockEntities() { return rtxremix$hasBlockEntities; }
    @Override public int rtxremix$snapshotMask() { return rtxremix$snapshotMask; }
    @Override public void rtxremix$clearSnapshotBit(int bit) { rtxremix$snapshotMask &= ~bit; }

    @Inject(method = "setSectionNode", at = @At("RETURN"))
    private void rtxremix$recordMove(long sectionNode, CallbackInfo ci) {
        ChunkSlotMoveLog.RENDER_THREAD.record(index);
    }

    @Inject(method = "createCompileTask", at = @At("RETURN"))
    private void rtxremix$recordNeighborhood(RenderSectionRegion region, CallbackInfoReturnable<?> cir) {
        rtxremix$snapshotMask = ((RegionNeighbourhood) region).rtxremix$snapshotMask();
    }

    @Inject(method = "setSectionMesh", at = @At("HEAD"))
    private void rtxremix$onMesh(SectionMesh mesh, CallbackInfoReturnable<SectionMesh> cir) {
        rtxremix$hasBlockEntities = !mesh.getRenderableBlockEntities().isEmpty();
        if (!mesh.hasRenderableLayers()) {
            long node = getSectionNode();
            SectionLeftRenderSetQueue.enqueueEmptyCandidate(SectionPos.x(node), SectionPos.y(node), SectionPos.z(node));
        }
    }

    @Inject(method = "reset", at = @At("HEAD"))
    private void rtxremix$reset(CallbackInfo ci) {
        rtxremix$hasBlockEntities = false;
        rtxremix$snapshotMask = 0;
    }
}
