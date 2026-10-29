/* SPDX-License-Identifier: GPL-3.0-only */
package com.nvidia.rtxremix.minecraft.mixin;

import com.nvidia.rtxremix.minecraft.RtxRemixMod;
import net.minecraft.client.renderer.state.level.ParticlesRenderState;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(ParticlesRenderState.class)
public abstract class ParticlesRenderStateMixin {
    @Inject(method = "submit", at = @At("HEAD"), cancellable = true)
    private void rtxremix$skipCapturedParticles(CallbackInfo ci) {
        if (RtxRemixMod.isRemixWorldRendering()) ci.cancel();
    }
}
