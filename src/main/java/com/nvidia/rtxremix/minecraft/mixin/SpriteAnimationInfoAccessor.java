/* SPDX-License-Identifier: GPL-3.0-only */
package com.nvidia.rtxremix.minecraft.mixin;

import net.minecraft.client.renderer.texture.SpriteContents;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(SpriteContents.AnimatedTexture.class)
public interface SpriteAnimationInfoAccessor {
    @Accessor("interpolateFrames") boolean rtxremix$interpolates();
}
