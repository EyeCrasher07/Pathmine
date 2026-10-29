/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.llamalad7.mixinextras.injector.wrapmethod.WrapMethod;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import com.nvidia.rtxremix.minecraft.terrain.capture.RegionBuilderRelease;
import com.nvidia.rtxremix.minecraft.terrain.SectionUpdateAccess;
import it.unimi.dsi.fastutil.longs.LongOpenHashSet;
import it.unimi.dsi.fastutil.longs.LongSet;
import net.minecraft.client.Camera;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.Minecraft;
import net.minecraft.client.SectionUpdateTracker;
import net.minecraft.client.renderer.chunk.RenderRegionCache;
import net.minecraft.client.renderer.extract.LevelExtractor;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import net.minecraft.client.renderer.LevelRenderer;
import net.minecraft.core.SectionPos;
import com.nvidia.rtxremix.minecraft.RtxRemixMod;
import com.nvidia.rtxremix.minecraft.terrain.NeighbourReflagFilter;
import com.nvidia.rtxremix.minecraft.terrain.capture.BlockQuadEmitter;
import com.nvidia.rtxremix.minecraft.terrain.visibility.BuiltChunkStorageSectionAccess;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;

/** Keeps cached section copies alive until every region for the extracted frame owns them. */
@Mixin(LevelExtractor.class)
public abstract class LevelExtractorMixin implements SectionUpdateAccess {
    @Shadow
    private SectionUpdateTracker sectionUpdateTracker;

    @Shadow @Final private LevelRenderer levelRenderer;

    @Shadow private boolean shouldInvalidateCompiledGeometry;
    @Unique private LongSet rtxremix$pendingRecapture;

    @Override
    public boolean rtxremix$deferTerrainRecapture(LongSet sections) {
        if (!shouldInvalidateCompiledGeometry) return false;
        if (rtxremix$pendingRecapture == null) {
            rtxremix$pendingRecapture = new LongOpenHashSet(sections);
        } else {
            rtxremix$pendingRecapture.addAll(sections);
        }
        return true;
    }

    @Inject(method = "setLevel", at = @At("HEAD"))
    private void rtxremix$discardPreviousWorldRecapture(CallbackInfo ci) {
        rtxremix$pendingRecapture = null;
    }

    @Inject(method = "extract", at = @At(value = "FIELD",
            target = "Lnet/minecraft/client/renderer/extract/LevelExtractor;shouldInvalidateCompiledGeometry:Z",
            opcode = org.objectweb.asm.Opcodes.PUTFIELD, shift = At.Shift.AFTER))
    private void rtxremix$recaptureNewSectionSlots(CallbackInfo ci) {
        LongSet sections = rtxremix$pendingRecapture;
        rtxremix$pendingRecapture = null;
        if (sections != null) {
            RtxRemixMod.recaptureTerrainForRemix(Minecraft.getInstance(), sections);
        }
    }

    @Inject(method = "setBlocksDirty", at = @At("HEAD"), cancellable = true)
    private void rtxremix$waterMargin(int minX, int minY, int minZ,
                                      int maxX, int maxY, int maxZ, CallbackInfo ci) {
        if (!RtxRemixMod.isRtxEnabled()) return;
        int radius = Math.max(1, BlockQuadEmitter.getWaterTopOcclusionRadius());
        LevelExtractor self = (LevelExtractor) (Object) this;
        for (int z = SectionPos.blockToSectionCoord(minZ - radius); z <= SectionPos.blockToSectionCoord(maxZ + radius); z++) {
            for (int x = SectionPos.blockToSectionCoord(minX - radius); x <= SectionPos.blockToSectionCoord(maxX + radius); x++) {
                for (int y = SectionPos.blockToSectionCoord(minY - 1); y <= SectionPos.blockToSectionCoord(maxY + 1); y++) {
                    self.setSectionDirty(x, y, z);
                }
            }
        }
        ci.cancel();
    }

    @Inject(method = "setSectionDirtyWithNeighbors", at = @At("HEAD"), cancellable = true)
    private void rtxremix$filterNeighbors(int x, int y, int z, CallbackInfo ci) {
        if (!RtxRemixMod.isRtxEnabled() || !NeighbourReflagFilter.ENABLED || levelRenderer.viewArea() == null) return;
        ((BuiltChunkStorageSectionAccess) levelRenderer.viewArea()).rtxremix$flagLoadedSection(x, y, z);
        ci.cancel();
    }

    @Unique
    private RenderRegionCache rtxremix$regionCache;

    @Override
    public boolean rtxremix$isSectionDirty(long sectionNode) {
        var state = sectionUpdateTracker == null ? null : sectionUpdateTracker.getDirtyState(sectionNode);
        return state != null && state.isDirty();
    }

    @Override
    public void rtxremix$setSectionNotDirty(long sectionNode) {
        var state = sectionUpdateTracker == null ? null : sectionUpdateTracker.getDirtyState(sectionNode);
        if (state != null) {
            state.setNotDirty();
        }
    }

    @WrapOperation(method = "extract",
            at = @At(value = "NEW", target = "()Lnet/minecraft/client/renderer/chunk/RenderRegionCache;"))
    private RenderRegionCache rtxremix$trackRegionCache(Operation<RenderRegionCache> original) {
        RenderRegionCache cache = original.call();
        rtxremix$regionCache = cache;
        return cache;
    }

    @WrapMethod(method = "extract")
    private void rtxremix$releaseCachedSections(
            DeltaTracker deltaTracker, Camera camera, float partialTick, Operation<Void> original) {
        try {
            original.call(deltaTracker, camera, partialTick);
        } finally {
            RenderRegionCache cache = rtxremix$regionCache;
            rtxremix$regionCache = null;
            if (cache != null) {
                ((RegionBuilderRelease) cache).rtxremix$releaseSnapshots();
            }
        }
    }
}
