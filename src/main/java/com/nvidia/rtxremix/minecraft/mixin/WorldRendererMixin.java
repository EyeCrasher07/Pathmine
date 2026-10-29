/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.llamalad7.mixinextras.injector.wrapmethod.WrapMethod;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import com.llamalad7.mixinextras.sugar.Local;
import com.mojang.blaze3d.resource.GraphicsResourceAllocator;
import com.mojang.blaze3d.vertex.PoseStack;
import com.mojang.blaze3d.vertex.QuadInstance;
import com.mojang.renderpearl.api.buffers.GpuBufferSlice;
import com.nvidia.rtxremix.minecraft.RemixRenderer;
import com.nvidia.rtxremix.minecraft.RtxRemixMod;
import com.nvidia.rtxremix.minecraft.cloud.WorldRendererCloudAccess;
import com.nvidia.rtxremix.minecraft.profiling.ProfileScope;
import com.nvidia.rtxremix.minecraft.profiling.WorldRendererProfilingAccess;
import com.nvidia.rtxremix.minecraft.sky.WorldRendererStarAccess;
import com.nvidia.rtxremix.minecraft.terrain.WorldRendererTerrainAccess;
import com.nvidia.rtxremix.minecraft.terrain.capture.RegionBuilderRelease;
import com.nvidia.rtxremix.minecraft.terrain.visibility.BuiltChunkStorageSectionAccess;
import it.unimi.dsi.fastutil.longs.LongOpenHashSet;
import it.unimi.dsi.fastutil.longs.LongSet;
import it.unimi.dsi.fastutil.longs.LongSets;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import net.minecraft.client.Camera;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.client.renderer.LevelRenderer;
import net.minecraft.client.renderer.SubmitNodeCollector;
import net.minecraft.client.renderer.ViewArea;
import net.minecraft.client.renderer.block.dispatch.BlockStateModelPart;
import net.minecraft.client.renderer.chunk.CompiledSectionMesh;
import net.minecraft.client.renderer.chunk.RenderRegionCache;
import net.minecraft.client.renderer.chunk.SectionRenderDispatcher;
import net.minecraft.client.renderer.state.level.BlockBreakingRenderState;
import net.minecraft.client.renderer.state.level.CameraRenderState;
import net.minecraft.client.renderer.state.level.LevelRenderState;
import net.minecraft.client.renderer.texture.OverlayTexture;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.core.SectionPos;
import net.minecraft.util.LightCoordsUtil;
import net.minecraft.util.profiling.ProfilerFiller;
import net.minecraft.world.level.chunk.LevelChunk;
import net.minecraft.world.level.lighting.LevelLightEngine;
import org.joml.Vector4f;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;

/** Hosts capture inside vanilla's complete frame traversal and section scheduling. */
@Mixin(LevelRenderer.class)
public abstract class WorldRendererMixin implements WorldRendererCloudAccess,
        WorldRendererProfilingAccess, WorldRendererStarAccess, WorldRendererTerrainAccess {
    @Shadow private SectionRenderDispatcher sectionRenderDispatcher;
    @Shadow private ViewArea viewArea;
    @Shadow @Final private LevelRenderState levelRenderState;
    @Unique private ProfileScope rtxremix$phase;
    @Unique private final QuadInstance rtxremix$crackInstance = new QuadInstance();
    @Unique private static final Direction[] rtxremix$DIRECTIONS = Direction.values();

    @Override public int rtxremix$getCloudTicks() { return (int) levelRenderState.gameTime; }

    @Override
    public float[] rtxremix$copyStarVertices() {
        var sky = ((LevelRenderer) (Object) this).skyRenderer();
        return sky == null ? new float[0] : ((WorldRendererStarAccess) sky).rtxremix$copyStarVertices();
    }

    @WrapMethod(method = "render")
    private void rtxremix$frame(GraphicsResourceAllocator allocator, boolean outline,
                               CameraRenderState camera, GpuBufferSlice fog, Vector4f fogColor,
                               boolean sky, boolean consistentDepth, Operation<Void> original) {
        RemixRenderer renderer = rtxremix$renderer();
        if (renderer != null && renderer.isInitialized()) {
            renderer.beginWorldRenderFrame();
            renderer.beginEntityFrame();
            renderer.getWeatherRenderer().beginCaptureFrame();
        }
        try (ProfileScope scope = ProfileScope.begin("WorldRenderer.render")) {
            original.call(allocator, outline, camera, fog, fogColor, sky, consistentDepth);
        } finally {
            rtxremix$closeProfilerPhaseForFabricEvent();
        }
    }

    @WrapOperation(method = "render", at = @At(value = "INVOKE",
            target = "Lnet/minecraft/util/profiling/ProfilerFiller;popPush(Ljava/lang/String;)V"))
    private void rtxremix$phase(ProfilerFiller profiler, String name, Operation<Void> original) {
        original.call(profiler, name);
        rtxremix$closeProfilerPhaseForFabricEvent();
        if (ProfileScope.isDeepProfilingEnabled()) {
            rtxremix$phase = ProfileScope.begin("WorldRenderer.phase." + name.replace(' ', '_'));
        }
    }

    @Override public void rtxremix$closeProfilerPhaseForFabricEvent() {
        if (rtxremix$phase != null) {
            rtxremix$phase.close();
            rtxremix$phase = null;
        }
    }

    @WrapOperation(method = "submitBlockDestroyAnimation", at = @At(value = "INVOKE",
            target = "Lnet/minecraft/client/renderer/SubmitNodeCollector;submitBreakingBlockModel(Lcom/mojang/blaze3d/vertex/PoseStack;Ljava/util/List;IZ)V"))
    private void rtxremix$crack(SubmitNodeCollector collector, PoseStack pose,
                               List<BlockStateModelPart> parts, int progress, boolean translucent,
                               Operation<Void> original, @Local BlockBreakingRenderState state) {
        RemixRenderer renderer = rtxremix$renderer();
        boolean captured = renderer != null && renderer.renderBlockDamageInPlace(state.blockPos(), progress,
                (capturePose, consumer) -> {
                    capturePose.translate(state.blockState().getOffset(state.blockPos()));
                    rtxremix$crackInstance.setColor(-1);
                    rtxremix$crackInstance.setLightCoords(LightCoordsUtil.FULL_BRIGHT);
                    rtxremix$crackInstance.setOverlayCoords(OverlayTexture.NO_OVERLAY);
                    for (var part : parts) {
                        for (var direction : rtxremix$DIRECTIONS) {
                            for (var quad : part.getQuads(direction)) {
                                consumer.putBakedQuad(capturePose.last(), quad, rtxremix$crackInstance);
                            }
                        }
                        for (var quad : part.getQuads(null)) {
                            consumer.putBakedQuad(capturePose.last(), quad, rtxremix$crackInstance);
                        }
                    }
                });
        if (!captured) original.call(collector, pose, parts, progress, translucent);
    }

    @Unique private static RemixRenderer rtxremix$renderer() {
        var mod = RtxRemixMod.getInstance();
        return mod == null ? null : mod.getRenderer();
    }

    @Override
    public void rtxremix$onNeighbourColumnInvalidated(int cx, int cz) {
        if (viewArea != null) {
            ((BuiltChunkStorageSectionAccess) viewArea).rtxremix$clearNeighbourSnapshotBits(cx, cz);
        }
    }

    @Override
    public LongSet rtxremix$builtSectionPositions() {
        LongOpenHashSet built = new LongOpenHashSet();
        if (viewArea == null) {
            return built;
        }
        for (SectionRenderDispatcher.RenderSection builtChunk : ((BuiltChunkStorageSectionAccess) viewArea).rtxremix$sections()) {
            if (builtChunk.getSectionMesh() == CompiledSectionMesh.UNCOMPILED) {
                continue;
            }
            BlockPos origin = builtChunk.getRenderOrigin();
            built.add(SectionPos.asLong(
                    SectionPos.blockToSectionCoord(origin.getX()),
                    SectionPos.blockToSectionCoord(origin.getY()),
                    SectionPos.blockToSectionCoord(origin.getZ())));
        }
        return built;
    }

    @Override
    public LongSet rtxremix$queueSectionsForRemix(LongSet sections) {
        if (viewArea == null || sectionRenderDispatcher == null || Minecraft.getInstance().level == null || sections.isEmpty()) {
            return LongSets.emptySet();
        }

        // Same gate as vanilla updateChunks: a section built before its light
        // is ready would bake bad light, and cancelRebuild below means nothing
        // re-flags it afterwards.
        ClientLevel level = Minecraft.getInstance().level;
        LevelLightEngine lightingProvider = level.getLightEngine();

        ArrayList<SectionRenderDispatcher.RenderSection> loadedSections = new ArrayList<>();
        for (SectionRenderDispatcher.RenderSection builtChunk : ((BuiltChunkStorageSectionAccess) viewArea).rtxremix$sections()) {
            BlockPos origin = builtChunk.getRenderOrigin();
            int cx = SectionPos.blockToSectionCoord(origin.getX());
            int sy = SectionPos.blockToSectionCoord(origin.getY());
            int cz = SectionPos.blockToSectionCoord(origin.getZ());
            if (!sections.contains(SectionPos.asLong(cx, sy, cz))) {
                continue;
            }
            LevelChunk worldChunk = level.getChunkSource().getChunk(cx, cz, false);
            if (worldChunk != null && !worldChunk.getSections()[worldChunk.getSectionIndexFromSectionY(sy)].hasOnlyAir()
                    && lightingProvider.lightOnInColumn(SectionPos.asLong(cx, 0, cz))) {
                loadedSections.add(builtChunk);
            }
        }

        // ChunkBuilder's normal task queue is FIFO. Queue nearby sections first
        // so visible holes close promptly while the Remix scene repopulates.
        Camera camera = Minecraft.getInstance().gameRenderer.mainCamera();
        double cameraX = camera.position().x;
        double cameraY = camera.position().y;
        double cameraZ = camera.position().z;
        loadedSections.sort(Comparator.comparingDouble(builtChunk -> {
            BlockPos origin = builtChunk.getRenderOrigin();
            double dx = origin.getX() + 8.0 - cameraX;
            double dy = origin.getY() + 8.0 - cameraY;
            double dz = origin.getZ() + 8.0 - cameraZ;
            return dx * dx + dy * dy + dz * dz;
        }));

        RenderRegionCache regionBuilder = new RenderRegionCache();
        LongOpenHashSet queued = new LongOpenHashSet(loadedSections.size());
        try {
            for (SectionRenderDispatcher.RenderSection builtChunk : loadedSections) {
                builtChunk.compileAsync(regionBuilder.createRegion(level, builtChunk.getSectionNode()));
                // Prevent WorldRenderer.updateChunks from queueing this same slot a
                // second time when it next walks vanilla's visible section list.
                ((com.nvidia.rtxremix.minecraft.terrain.SectionUpdateAccess) Minecraft.getInstance().levelExtractor)
                        .rtxremix$setSectionNotDirty(builtChunk.getSectionNode());
                BlockPos origin = builtChunk.getRenderOrigin();
                queued.add(SectionPos.asLong(
                        SectionPos.blockToSectionCoord(origin.getX()),
                        SectionPos.blockToSectionCoord(origin.getY()),
                        SectionPos.blockToSectionCoord(origin.getZ())));
            }
        } finally {
            ((RegionBuilderRelease) regionBuilder).rtxremix$releaseSnapshots();
        }
        return queued;
    }

    @Override
    public void rtxremix$scheduleResidentSectionRebuild(int cx, int sy, int cz) {
        ((BuiltChunkStorageSectionAccess) viewArea).rtxremix$scheduleRebuildIfResident(cx, sy, cz);
    }

}
