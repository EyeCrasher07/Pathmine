/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.llamalad7.mixinextras.injector.wrapmethod.WrapMethod;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import com.nvidia.rtxremix.minecraft.RemixRenderer;
import com.nvidia.rtxremix.minecraft.RtxRemixMod;
import com.nvidia.rtxremix.minecraft.profiling.ProfileScope;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.GameRenderer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Lifecycle and frame timing around the current extraction/render split. */
@Mixin(Minecraft.class)
public abstract class MinecraftClientMixin {
    @com.llamalad7.mixinextras.injector.ModifyExpressionValue(method = "<init>",
            at = @At(value = "INVOKE", target = "Lnet/minecraft/client/PreferredGraphicsApi;getBackendsToTry()[Lcom/mojang/renderpearl/api/device/GpuBackend;"))
    private com.mojang.renderpearl.api.device.GpuBackend[] rtxremix$interopBackend(
            com.mojang.renderpearl.api.device.GpuBackend[] requested) {
        return new com.mojang.renderpearl.api.device.GpuBackend[]{
                new com.mojang.renderpearl.backend.opengl.GlBackend()};
    }

    @WrapMethod(method = "runTick")
    private void rtxremix$frame(boolean advanceGameTime, Operation<Void> original) {
        ProfileScope.beginFrame();
        long start = System.nanoTime();
        try (ProfileScope scope = ProfileScope.beginJfrOnly("MinecraftClient.render")) {
            original.call(advanceGameTime);
        } finally {
            long elapsed = System.nanoTime() - start;
            ProfileScope.emitFrameLedger(elapsed);
            RemixRenderer renderer = RtxRemixMod.renderer;
            if (renderer != null) renderer.onMinecraftRenderFrameEnd(elapsed);
        }
    }

    @Inject(method = "renderFrame", at = @At(value = "INVOKE",
            target = "Lnet/minecraft/client/renderer/GameRenderer;update(Lnet/minecraft/client/DeltaTracker;)V"))
    private void rtxremix$prepareCamera(boolean advanceGameTime, CallbackInfo ci) {
        RemixRenderer renderer = RtxRemixMod.renderer;
        if (renderer != null) renderer.onMinecraftWorldRenderFrameStart(
                (Minecraft) (Object) this, advanceGameTime);
    }

    @WrapOperation(method = "renderFrame", at = @At(value = "INVOKE",
            target = "Lnet/minecraft/client/renderer/GameRenderer;render()V"))
    private void rtxremix$render(GameRenderer renderer, Operation<Void> original) {
        try (ProfileScope scope = ProfileScope.begin("MinecraftClient.render.gameRenderer")) {
            original.call(renderer);
        }
    }

    @WrapOperation(method = "runTick", at = @At(value = "INVOKE",
            target = "Lnet/minecraft/client/Minecraft;runAllTasks()V"))
    private void rtxremix$tasks(Minecraft client, Operation<Void> original) {
        ProfileScope.beginClientTaskDrain("MinecraftClient", client.getPendingTasksCount());
        try { original.call(client); }
        finally { ProfileScope.endClientTaskDrain(client.getPendingTasksCount()); }
    }

    @WrapMethod(method = "tick")
    private void rtxremix$tick(Operation<Void> original) {
        try (ProfileScope scope = ProfileScope.beginJfrOnly("MinecraftClient.tick")) {
            original.call();
        }
    }

    @Inject(method = "close", at = @At("TAIL"))
    private void rtxremix$closed(CallbackInfo ci) {
        RtxRemixMod.shutdownAfterMinecraftClose();
    }
}
