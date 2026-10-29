/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import com.nvidia.rtxremix.minecraft.RtxRemixMod;
import com.nvidia.rtxremix.minecraft.terrain.visibility.ChunkInfoPool;
import com.nvidia.rtxremix.minecraft.terrain.visibility.RenderableChunksBuffers;
import com.nvidia.rtxremix.minecraft.terrain.visibility.VisibilityGraphBuffers;
import it.unimi.dsi.fastutil.longs.LongArrayList;
import it.unimi.dsi.fastutil.longs.LongOpenHashSet;
import java.util.Queue;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;
import java.util.function.Consumer;
import net.minecraft.client.renderer.SectionOcclusionGraph;
import net.minecraft.client.renderer.ViewArea;
import net.minecraft.client.renderer.chunk.CompiledSectionMesh;
import net.minecraft.client.renderer.chunk.SectionRenderDispatcher;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.core.SectionPos;
import net.minecraft.world.level.ChunkPos;
import net.minecraft.world.phys.Vec3;
import org.joml.Vector3d;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Retains vanilla's octree and loading rules while reusing visibility-walk scratch data. */
@Mixin(SectionOcclusionGraph.class)
public abstract class ChunkRenderingDataPreparerMixin {
    @Shadow @Final private static Direction[] DIRECTIONS;
    @Shadow @Final private static double CEILED_SECTION_DIAGONAL;
    @Shadow private ViewArea viewArea;
    @Shadow protected abstract SectionRenderDispatcher.RenderSection getRelativeFrom(
            long cameraSection, SectionRenderDispatcher.RenderSection section, Direction direction);

    @Unique private static final boolean rtxremix$VANILLA_WALK =
            Boolean.getBoolean("rtxremix.vanillaVisibilityWalk");
    @Unique private static final Executor rtxremix$executor =
            Executors.newSingleThreadExecutor(task -> {
                Thread thread = new Thread(task, "Pathmine chunk visibility");
                thread.setDaemon(true);
                thread.setPriority(Math.max(Thread.MIN_PRIORITY, Thread.NORM_PRIORITY - 2));
                return thread;
            });

    @Inject(method = "waitAndReset", at = @At(value = "FIELD",
            target = "Lnet/minecraft/client/renderer/SectionOcclusionGraph;viewArea:Lnet/minecraft/client/renderer/ViewArea;",
            opcode = org.objectweb.asm.Opcodes.PUTFIELD))
    private void rtxremix$configure(ViewArea storage, CallbackInfo ci) {
        VisibilityGraphBuffers.configure(storage == null ? 0 : storage.size());
    }

    @WrapOperation(method = "scheduleFullUpdate", at = @At(value = "INVOKE",
            target = "Ljava/util/concurrent/CompletableFuture;runAsync(Ljava/lang/Runnable;Ljava/util/concurrent/Executor;)Ljava/util/concurrent/CompletableFuture;"))
    private CompletableFuture<Void> rtxremix$schedule(Runnable task, Executor executor,
                                                    Operation<CompletableFuture<Void>> original) {
        return original.call(task, RtxRemixMod.isRtxEnabled() ? rtxremix$executor : executor);
    }

    @Inject(method = "runUpdates", at = @At("HEAD"), cancellable = true)
    private void rtxremix$walk(SectionOcclusionGraph.GraphStorage storage, Vec3 camera,
                              Queue<SectionOcclusionGraph.Node> queue, boolean culling,
                              Consumer<SectionRenderDispatcher.RenderSection> onAdded,
                              LongOpenHashSet empty, LongOpenHashSet loaded, CallbackInfo ci) {
        if (!RtxRemixMod.isRtxEnabled() || rtxremix$VANILLA_WALK) return;
        SectionPos cameraSection = SectionPos.of(camera);
        long cameraNode = cameraSection.asLong();
        int centerX = SectionPos.sectionToBlockCoord(cameraSection.x()) + 8;
        int centerY = SectionPos.sectionToBlockCoord(cameraSection.y()) + 8;
        int centerZ = SectionPos.sectionToBlockCoord(cameraSection.z()) + 8;
        // JOML's exact normalization and accumulation order, with one pair per walk.
        Vector3d point = new Vector3d();
        Vector3d step = new Vector3d();
        BlockPos.MutableBlockPos probe = new BlockPos.MutableBlockPos();
        ChunkInfoPool pool = ((RenderableChunksBuffers) storage).rtxremix$infoPool();
        while (!queue.isEmpty()) {
            SectionOcclusionGraph.Node node = queue.poll();
            var section = node.section;
            long sectionNode = section.getSectionNode();
            long chunk = ChunkPos.fromSectionNode(sectionNode);
            if (!loaded.contains(chunk)) {
                storage.sectionsWaitingForChunkLoads.computeIfAbsent(chunk, key -> new LongArrayList())
                        .add(sectionNode);
                continue;
            }
            if (!empty.contains(sectionNode)) {
                if (storage.sectionTree.add(section)) onAdded.accept(section);
            } else if (section.sectionMesh.compareAndSet(CompiledSectionMesh.UNCOMPILED, CompiledSectionMesh.EMPTY)) {
                section.updateUploadTime();
            }
            boolean far = Math.abs(SectionPos.x(sectionNode) - cameraSection.x()) > 3
                    || Math.abs(SectionPos.y(sectionNode) - cameraSection.y()) > 3
                    || Math.abs(SectionPos.z(sectionNode) - cameraSection.z()) > 3;
            for (Direction direction : DIRECTIONS) {
                var neighbor = getRelativeFrom(cameraNode, section, direction);
                if (neighbor == null || culling && node.hasDirection(direction.getOpposite())) continue;
                if (culling && node.hasSourceDirections()) {
                    boolean visible = false;
                    for (int i = 0; i < DIRECTIONS.length; i++) {
                        if (node.hasSourceDirection(i)
                                && section.getSectionMesh().facesCanSeeEachother(DIRECTIONS[i].getOpposite(), direction)) {
                            visible = true;
                            break;
                        }
                    }
                    if (!visible) continue;
                }
                if (culling && far) {
                    int x = SectionPos.sectionToBlockCoord(SectionPos.x(sectionNode));
                    int y = SectionPos.sectionToBlockCoord(SectionPos.y(sectionNode));
                    int z = SectionPos.sectionToBlockCoord(SectionPos.z(sectionNode));
                    point.set(x + ((direction.getAxis() == Direction.Axis.X ? centerX > x : centerX < x) ? 16 : 0),
                            y + ((direction.getAxis() == Direction.Axis.Y ? centerY > y : centerY < y) ? 16 : 0),
                            z + ((direction.getAxis() == Direction.Axis.Z ? centerZ > z : centerZ < z) ? 16 : 0));
                    step.set(camera.x, camera.y, camera.z).sub(point).normalize().mul(CEILED_SECTION_DIAGONAL);
                    boolean visible = true;
                    while (point.distanceSquared(camera.x, camera.y, camera.z) > 3600.0) {
                        point.add(step);
                        if (point.y > viewArea.maxY() || point.y < viewArea.minY()) break;
                        var passed = viewArea.getRenderSectionAt(probe.set(point.x, point.y, point.z));
                        if (passed == null || storage.sectionToNodeMap.get(passed) == null) {
                            visible = false;
                            break;
                        }
                    }
                    if (!visible) continue;
                }
                var existing = storage.sectionToNodeMap.get(neighbor);
                if (existing != null) existing.addSourceDirection(direction);
                else {
                    var next = pool.acquire(neighbor, direction, node.step + 1);
                    next.setDirections(node.directions, direction);
                    queue.add(next);
                    storage.sectionToNodeMap.put(neighbor, next);
                }
            }
        }
        ci.cancel();
    }
}
