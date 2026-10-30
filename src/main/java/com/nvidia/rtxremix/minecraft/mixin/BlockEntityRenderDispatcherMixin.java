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
import net.minecraft.client.renderer.blockentity.BlockEntityRenderDispatcher;
import net.minecraft.client.renderer.blockentity.state.BlockEntityRenderState;
import net.minecraft.client.renderer.feature.ModelFeatureRenderer;
import net.minecraft.client.renderer.state.level.CameraRenderState;
import net.minecraft.world.level.block.entity.BlockEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(BlockEntityRenderDispatcher.class)
public abstract class BlockEntityRenderDispatcherMixin {
    @SuppressWarnings("unchecked")
    @Inject(method = "tryExtractRenderState", at = @At("RETURN"))
    private void rtxremix$recordSource(BlockEntity blockEntity, float partialTick,
            ModelFeatureRenderer.CrumblingOverlay breakProgress, boolean global,
            CallbackInfoReturnable<BlockEntityRenderState> ci) {
        if (ci.getReturnValue() != null) {
            ((CapturedRenderState<BlockEntity>) ci.getReturnValue()).rtxremix$setSource(blockEntity, partialTick);
        }
    }

    @SuppressWarnings("unchecked")
    @WrapMethod(method = "submit")
    private void rtxremix$captureBlockEntity(BlockEntityRenderState state, PoseStack matrices,
            SubmitNodeCollector output, CameraRenderState camera, Operation<Void> original) {
        var mod = RtxRemixMod.getInstance();
        var renderer = mod == null ? null : mod.getRenderer();
        BlockEntity owner = ((CapturedRenderState<BlockEntity>) state).rtxremix$owner();
        if (!EntityRenderContext.get().isActive() && renderer != null && owner != null && owner.hasLevel()
                && renderer.renderBlockEntityInPlace(owner, (captureMatrices, captureOutput) ->
                        original.call(state, captureMatrices, captureOutput, camera))) {
            return;
        }
        original.call(state, matrices, output, camera);
    }
}
