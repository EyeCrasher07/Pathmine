/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.llamalad7.mixinextras.injector.wrapmethod.WrapMethod;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.mojang.blaze3d.vertex.PoseStack;
import com.nvidia.rtxremix.minecraft.RtxRemixMod;
import com.nvidia.rtxremix.minecraft.entity.CapturedRenderState;
import com.nvidia.rtxremix.minecraft.entity.EntityRenderContext;
import net.minecraft.client.renderer.SubmitNodeCollector;
import net.minecraft.client.renderer.entity.EntityRenderDispatcher;
import net.minecraft.client.renderer.entity.state.EntityRenderState;
import net.minecraft.client.renderer.state.level.CameraRenderState;
import net.minecraft.world.entity.Entity;
import org.spongepowered.asm.mixin.Mixin;

/** Captures each visible entity from its already extracted state. */
@Mixin(EntityRenderDispatcher.class)
public abstract class EntityRenderDispatcherMixin {
    @SuppressWarnings("unchecked")
    @WrapMethod(method = "submit")
    private void rtxremix$captureEntity(EntityRenderState state, CameraRenderState camera,
            double x, double y, double z, PoseStack matrices, SubmitNodeCollector output,
            Operation<Void> original) {
        CapturedRenderState<Entity> source = (CapturedRenderState<Entity>) state;
        var mod = RtxRemixMod.getInstance();
        var renderer = mod == null ? null : mod.getRenderer();
        if (!EntityRenderContext.get().isActive() && renderer != null && source.rtxremix$owner() != null
                && renderer.renderEntityInPlace(source.rtxremix$owner(), source.rtxremix$partialTick(), state)) {
            return;
        }
        original.call(state, camera, x, y, z, matrices, output);
    }
}
