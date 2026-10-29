/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import com.nvidia.rtxremix.minecraft.RtxRemixMod;
import com.nvidia.rtxremix.minecraft.profiling.ProfileScope;
import com.nvidia.rtxremix.minecraft.terrain.BuiltChunkNeighbourhoodAccess;
import com.nvidia.rtxremix.minecraft.terrain.NeighbourReflagFilter;
import com.nvidia.rtxremix.minecraft.terrain.SectionUpdateAccess;
import com.nvidia.rtxremix.minecraft.terrain.SkippedSectionRebuilds;
import com.nvidia.rtxremix.minecraft.terrain.visibility.BuiltChunkStorageSectionAccess;
import com.nvidia.rtxremix.minecraft.terrain.visibility.ChunkStorageTorus;
import net.minecraft.client.Minecraft;
import net.minecraft.client.RotatingSectionStorage;
import net.minecraft.client.renderer.ViewArea;
import net.minecraft.client.renderer.chunk.SectionRenderDispatcher.RenderSection;
import net.minecraft.core.SectionPos;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(ViewArea.class)
public abstract class BuiltChunkStorageMixin
        implements ChunkStorageTorus.StorageAccess, BuiltChunkStorageSectionAccess {
    @Shadow @Final
    private RotatingSectionStorage<RenderSection> sections;
    @Shadow @Final
    private int minY;

    private RenderSection[] rtxremix$slots;
    private ChunkStorageTorus.Workspace rtxremix$torusWorkspace;

    @Inject(method = "<init>", at = @At("RETURN"))
    private void rtxremix$indexSlots(CallbackInfo ci) {
        rtxremix$slots = new RenderSection[sections.size()];
        sections.forEach(section -> rtxremix$slots[section.index] = section);
    }

    @WrapOperation(method = "repositionCamera",
            at = @At(value = "INVOKE",
                    target = "Lnet/minecraft/client/RotatingSectionStorage;repositionCenter(Lnet/minecraft/core/SectionPos;)Z"))
    private boolean rtxremix$repositionEnteringPlanes(
            RotatingSectionStorage<RenderSection> storage, SectionPos cameraSection,
            Operation<Boolean> original) {
        if (!RtxRemixMod.isRtxEnabled()) {
            return original.call(storage, cameraSection);
        }
        if (cameraSection.equals(storage.centerSectionPos())) {
            return false;
        }
        if (rtxremix$torusWorkspace == null) {
            rtxremix$torusWorkspace = new ChunkStorageTorus.Workspace();
        }
        int diameter = storage.radius() * 2 + 1;
        try (ProfileScope scope = ProfileScope.begin("ChunkStorage.updateCameraPosition")) {
            int moved = ChunkStorageTorus.reposition(this, rtxremix$torusWorkspace,
                    diameter, storage.height(), diameter, minY, cameraSection.x(), cameraSection.z());
            scope.captureCount(moved).scannedChunkCount(diameter * 2);
        }
        ((RotatingSectionStorageAccessor) storage).rtxremix$setCenterSectionPos(cameraSection);
        // ViewArea still invalidates its graph, including a vertical-only camera move.
        return true;
    }

    @Override
    public RenderSection[] rtxremix$sections() {
        return rtxremix$slots;
    }

    @Override
    public int rtxremix$getOriginX(int slotIndex) {
        return rtxremix$slots[slotIndex].getRenderOrigin().getX();
    }

    @Override
    public int rtxremix$getOriginZ(int slotIndex) {
        return rtxremix$slots[slotIndex].getRenderOrigin().getZ();
    }

    @Override
    public void rtxremix$setOrigin(int slotIndex, int x, int y, int z) {
        rtxremix$slots[slotIndex].setSectionNode(SectionPos.asLong(x >> 4, y >> 4, z >> 4));
    }

    @Override
    public void rtxremix$scheduleRebuildIfResident(int cx, int sy, int cz) {
        if (sections.getValue(cx, sy, cz) != null) {
            Minecraft.getInstance().levelExtractor.setSectionDirty(cx, sy, cz);
        }
    }

    @Override
    public void rtxremix$flagLoadedSection(int cx, int sy, int cz) {
        if (!NeighbourReflagFilter.sectionInRange(sy, sections.minY(), sections.height())) {
            return;
        }
        rtxremix$scheduleRebuildIfResident(cx, sy, cz);
        Minecraft client = Minecraft.getInstance();
        SectionUpdateAccess updates = (SectionUpdateAccess) client.levelExtractor;
        for (int k = 0; k < NeighbourReflagFilter.NEIGHBOUR_COUNT; k++) {
            int nx = cx + NeighbourReflagFilter.DX[k];
            int nz = cz + NeighbourReflagFilter.DZ[k];
            RenderSection slot = sections.getValue(nx, sy, nz);
            boolean resident = slot != null;
            boolean awaitingBuild = resident && updates.rtxremix$isSectionDirty(slot.getSectionNode());
            int snapshotMask = resident ? ((BuiltChunkNeighbourhoodAccess) slot).rtxremix$snapshotMask() : 0;
            int loaderBit = NeighbourReflagFilter.bit(NeighbourReflagFilter.mirroredIndex(k));
            switch (NeighbourReflagFilter.decideNeighbour(resident, awaitingBuild, snapshotMask, loaderBit)) {
                case FLAG -> {
                    client.levelExtractor.setSectionDirty(nx, sy, nz);
                    NeighbourReflagFilter.recordKept();
                }
                case DROP_IN_SNAPSHOT -> SkippedSectionRebuilds.recordNeighbourInSnapshot();
                case SKIP_NOT_RESIDENT, SKIP_AWAITING_BUILD -> { }
            }
        }
    }

    @Override
    public void rtxremix$clearNeighbourSnapshotBits(int cx, int cz) {
        for (int k = 0; k < NeighbourReflagFilter.NEIGHBOUR_COUNT; k++) {
            int bit = NeighbourReflagFilter.bit(NeighbourReflagFilter.mirroredIndex(k));
            int nx = cx + NeighbourReflagFilter.DX[k];
            int nz = cz + NeighbourReflagFilter.DZ[k];
            for (int sy = sections.minY(); sy <= sections.maxY(); sy++) {
                RenderSection slot = sections.getValue(nx, sy, nz);
                if (slot != null) {
                    ((BuiltChunkNeighbourhoodAccess) slot).rtxremix$clearSnapshotBit(bit);
                }
            }
        }
    }
}
