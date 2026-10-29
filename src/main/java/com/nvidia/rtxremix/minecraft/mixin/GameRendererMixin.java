/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import com.mojang.blaze3d.vertex.PoseStack;
import com.nvidia.rtxremix.minecraft.RtxRemixMod;
import com.nvidia.rtxremix.minecraft.profiling.ProfileScope;
import net.minecraft.client.gui.render.GuiRenderer;
import net.minecraft.client.renderer.FirstPersonHandsAndItemsRenderer;
import net.minecraft.client.renderer.GameRenderer;
import net.minecraft.client.renderer.SubmitNodeCollector;
import net.minecraft.client.renderer.state.level.FirstPersonHandsAndItemsRenderState;
import net.minecraft.client.renderer.state.level.PlayerRenderState;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(GameRenderer.class)
public abstract class GameRendererMixin {
    @WrapOperation(method = "renderItemInHand", at = @At(value = "INVOKE",
            target = "Lnet/minecraft/client/renderer/FirstPersonHandsAndItemsRenderer;submitHandsWithItems(FLcom/mojang/blaze3d/vertex/PoseStack;Lnet/minecraft/client/renderer/SubmitNodeCollector;Lnet/minecraft/client/renderer/state/level/PlayerRenderState;Lnet/minecraft/client/renderer/state/level/FirstPersonHandsAndItemsRenderState;)V"))
    private void rtxremix$captureHands(FirstPersonHandsAndItemsRenderer renderer,
            float partialTick, PoseStack matrices, SubmitNodeCollector output,
            PlayerRenderState player, FirstPersonHandsAndItemsRenderState hands, Operation<Void> original) {
        if (RtxRemixMod.shouldCaptureFirstPersonModel()) {
            RtxRemixMod.renderer.captureFirstPersonModel(renderer, partialTick, matrices, player, hands);
        } else {
            original.call(renderer, partialTick, matrices, output, player, hands);
        }
    }

    @Inject(method = "render3dHud", at = @At(value = "INVOKE",
            target = "Lnet/minecraft/client/renderer/GameRenderer;renderItemInHand(Lnet/minecraft/client/renderer/state/level/CameraRenderState;Lnet/minecraft/client/renderer/state/level/PlayerRenderState;Lcom/mojang/renderpearl/api/textures/GpuTextureView;)V",
            shift = At.Shift.AFTER))
    private void rtxremix$submitBeforeScreenEffects(CallbackInfo ci) {
        RtxRemixMod.submitRemixFrameIfReady();
    }

    @Inject(method = "renderLevel", at = @At("TAIL"))
    private void rtxremix$submitAtWorldTail(CallbackInfo ci) {
        RtxRemixMod.submitRemixFrameIfReady();
    }

    @Inject(method = "extract", at = @At("HEAD"))
    private void rtxremix$followMenuBeforeGuiExtraction(CallbackInfo ci) {
        RtxRemixMod.followRemixMenuVisibility();
    }

    @WrapOperation(method = "render", at = @At(value = "INVOKE",
            target = "Lnet/minecraft/client/gui/render/GuiRenderer;render()V"))
    private void rtxremix$profileGui(GuiRenderer renderer, Operation<Void> original) {
        try (ProfileScope scope = ProfileScope.begin("GameRenderer.render.gui")
                .plotOnClose("Java.GameRenderer.render.gui.ms")) {
            original.call(renderer);
        }
    }
}
