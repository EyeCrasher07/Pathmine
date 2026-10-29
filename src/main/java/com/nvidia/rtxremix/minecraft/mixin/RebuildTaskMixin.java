/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.nvidia.rtxremix.minecraft.terrain.capture.RegionSnapshotRelease;
import com.llamalad7.mixinextras.injector.wrapmethod.WrapMethod;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import net.minecraft.client.renderer.SectionBufferBuilderPack;
import net.minecraft.client.renderer.chunk.RenderSectionRegion;
import net.minecraft.client.renderer.chunk.SectionRenderDispatcher.RenderSection.SectionTask.SectionTaskResult;

/**
 * Releases a rebuild task's region snapshot once the section build that read it has
 * returned. Cancellation sets a flag without releasing the region while the worker reads
 * it. The dispatcher may retry a task that throws, so only a normal return releases the
 * snapshot. A task cancelled before it ever runs leaves its region to the
 * collector, which only forgoes the reuse.
 */
@Mixin(targets = "net.minecraft.client.renderer.chunk.SectionRenderDispatcher$RenderSection$CompileTask")
public abstract class RebuildTaskMixin {

    @Shadow
    @Final
    private RenderSectionRegion region;

    @WrapMethod(method = "doTask")
    private SectionTaskResult rtxremix$releaseRegion(
            SectionBufferBuilderPack buffers, Operation<SectionTaskResult> original) {
        SectionTaskResult result = original.call(buffers);
        ((RegionSnapshotRelease) region).rtxremix$release();
        return result;
    }
}
