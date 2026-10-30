/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.mojang.blaze3d.platform.NativeImage;
import com.mojang.renderpearl.api.buffers.GpuBufferSlice;
import com.mojang.renderpearl.api.commands.RenderPass;
import com.nvidia.rtxremix.minecraft.texture.AtlasAnimationForwarder;
import com.nvidia.rtxremix.minecraft.texture.SpriteAnimationCapture;
import net.minecraft.client.renderer.texture.SpriteContents;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Mirrors the GPU animator's exact frame and quantized interpolation phase. */
@Mixin(SpriteContents.AnimationState.class)
public abstract class SpriteContentsUploadMixin implements SpriteAnimationCapture {
    @Shadow private int frame;
    @Shadow private int subFrame;
    @Shadow @Final private GpuBufferSlice[] spriteUbosByMip;
    @Unique private TextureAtlasSprite rtxremix$sprite;
    @Unique private int rtxremix$padding;
    @Unique private NativeImage rtxremix$interpolated;

    @Override public void rtxremix$attachSprite(TextureAtlasSprite sprite, int padding) {
        rtxremix$sprite = sprite;
        rtxremix$padding = padding;
    }

    @Inject(method = "drawToAtlas", at = @At("HEAD"))
    private void rtxremix$mirror(RenderPass pass, GpuBufferSlice ubo, CallbackInfo ci) {
        if (ubo != spriteUbosByMip[0] || rtxremix$sprite == null
                || !AtlasAnimationForwarder.isCapturing(rtxremix$sprite.atlasLocation())) return;
        SpriteContents contents = rtxremix$sprite.contents();
        SpriteContents.AnimatedTexture animation = contents.animatedTexture;
        var source = ((SpriteContentsAccessor) contents).getImage();
        int columns = source.getWidth() / contents.width();
        int current = animation.frames.get(frame).index();
        int next = animation.frames.get((frame + 1) % animation.frames.size()).index();
        boolean interpolate = ((SpriteAnimationInfoAccessor) animation).rtxremix$interpolates();
        float progress = interpolate ? (int) (((float) subFrame / animation.frames.get(frame).time()) * 1000.0f) / 1000.0f : 0.0f;
        int x = current % columns * contents.width();
        int y = current / columns * contents.height();
        NativeImage pixels = source;
        int rowPitch = source.getWidth() * 4;
        long pointer;
        if (progress > 0.0f && current != next) {
            if (rtxremix$interpolated == null) rtxremix$interpolated = new NativeImage(contents.width(), contents.height(), false);
            int nextX = next % columns * contents.width();
            int nextY = next / columns * contents.height();
            com.nvidia.rtxremix.minecraft.texture.SpriteFrameInterpolation.copy(
                    source, x, y, nextX, nextY, progress, rtxremix$interpolated);
            pixels = rtxremix$interpolated;
            rowPitch = pixels.getWidth() * 4;
            pointer = ((NativeImageAccessor) (Object) pixels).getPointer();
        } else {
            pointer = ((NativeImageAccessor) (Object) source).getPointer() + ((long) y * source.getWidth() + x) * 4;
        }
        AtlasAnimationForwarder.addPatch(contents.name(),
                rtxremix$sprite.getX() + rtxremix$padding, rtxremix$sprite.getY() + rtxremix$padding,
                contents.width(), contents.height(), current, next, progress, rtxremix$padding, pointer, rowPitch);
    }

    @Inject(method = "close", at = @At("HEAD"))
    private void rtxremix$close(CallbackInfo ci) {
        if (rtxremix$interpolated != null) {
            rtxremix$interpolated.close();
            rtxremix$interpolated = null;
        }
    }
}
