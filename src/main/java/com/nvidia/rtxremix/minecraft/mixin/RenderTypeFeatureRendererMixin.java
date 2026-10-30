/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.mojang.blaze3d.vertex.VertexConsumer;
import com.nvidia.rtxremix.minecraft.entity.RemixFeatureCapture;
import net.minecraft.client.renderer.feature.RenderTypeFeatureRenderer;
import net.minecraft.client.renderer.rendertype.RenderType;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(RenderTypeFeatureRenderer.class)
public abstract class RenderTypeFeatureRendererMixin {
    @Inject(method = "getVertexBuilder", at = @At("HEAD"), cancellable = true)
    private void rtxremix$captureVertices(RenderType type, CallbackInfoReturnable<VertexConsumer> ci) {
        RemixFeatureCapture capture = RemixFeatureCapture.current();
        if (capture != null) {
            ci.setReturnValue(capture.provider().getBuffer(type));
        }
    }
}
