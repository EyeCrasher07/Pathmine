/* SPDX-License-Identifier: GPL-3.0-only */
package com.nvidia.rtxremix.minecraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import com.nvidia.rtxremix.minecraft.RtxRemixMod;
import java.nio.ByteBuffer;
import net.minecraft.client.renderer.WeatherEffectRenderer;
import net.minecraft.client.renderer.state.level.WeatherRenderState;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(WeatherEffectRenderer.class)
public abstract class WeatherEffectRendererMixin {
    @WrapOperation(method = "prepare", at = @At(value = "INVOKE",
            target = "Lnet/minecraft/client/renderer/WeatherEffectRenderer;uploadVertexBuffer(Ljava/nio/ByteBuffer;)V"))
    private void rtxremix$capture(WeatherEffectRenderer renderer, ByteBuffer vertices,
                                  Operation<Void> original, Vec3 camera, WeatherRenderState state) {
        if (RtxRemixMod.isRemixWorldRendering()) {
            RtxRemixMod.getInstance().getRenderer().getWeatherRenderer().capture(
                    vertices, state.rainColumns.size(), state.snowColumns.size());
        } else original.call(renderer, vertices);
    }

    @Inject(method = "render(Lnet/minecraft/client/renderer/state/level/WeatherRenderState;Lcom/mojang/renderpearl/api/commands/RenderPass;Lcom/mojang/renderpearl/api/pipeline/RenderPipeline;)V",
            at = @At("HEAD"), cancellable = true)
    private void rtxremix$skipCapturedWeather(CallbackInfo ci) {
        if (RtxRemixMod.isRemixWorldRendering()) ci.cancel();
    }
}
