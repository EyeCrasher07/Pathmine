/* SPDX-License-Identifier: GPL-3.0-only */
package com.nvidia.rtxremix.minecraft.mixin;

import com.nvidia.rtxremix.minecraft.texture.SpriteAnimationCapture;
import net.minecraft.client.renderer.texture.SpriteContents;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(TextureAtlasSprite.class)
public abstract class TextureAtlasSpriteMixin {
    @Shadow @Final private int padding;

    @Inject(method = "createAnimationState", at = @At("RETURN"))
    private void rtxremix$attach(CallbackInfoReturnable<SpriteContents.AnimationState> cir) {
        if (cir.getReturnValue() != null) {
            ((SpriteAnimationCapture) cir.getReturnValue()).rtxremix$attachSprite(
                    (TextureAtlasSprite) (Object) this, padding);
        }
    }
}
