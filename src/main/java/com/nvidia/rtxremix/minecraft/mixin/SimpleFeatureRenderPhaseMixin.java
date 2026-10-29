/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.nvidia.rtxremix.minecraft.entity.RemixFeatureCapture;
import net.minecraft.client.renderer.feature.phase.SimpleFeatureRenderPhase;
import net.minecraft.client.renderer.feature.submit.SubmitNode;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(SimpleFeatureRenderPhase.class)
public abstract class SimpleFeatureRenderPhaseMixin {
    @Inject(method = "submit", at = @At("HEAD"), cancellable = true)
    private void rtxremix$captureSubmit(SubmitNode submit, CallbackInfo ci) {
        RemixFeatureCapture capture = RemixFeatureCapture.current();
        if (capture != null) {
            capture.capture(submit);
            ci.cancel();
        }
    }
}
