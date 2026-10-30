/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.nvidia.rtxremix.minecraft.entity.RemixFeatureCapture;
import net.minecraft.client.renderer.feature.phase.TranslucentFeatureRenderPhase;
import net.minecraft.client.renderer.feature.submit.TranslucentSubmit;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(TranslucentFeatureRenderPhase.class)
public abstract class TranslucentFeatureRenderPhaseMixin {
    @Inject(method = "submit(Lnet/minecraft/client/renderer/feature/submit/TranslucentSubmit;)V",
            at = @At("HEAD"), cancellable = true)
    private void rtxremix$captureSubmit(TranslucentSubmit submit, CallbackInfo ci) {
        RemixFeatureCapture capture = RemixFeatureCapture.current();
        if (capture != null) {
            capture.capture(submit);
            ci.cancel();
        }
    }
}
