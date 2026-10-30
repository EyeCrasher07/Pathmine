/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.mojang.renderpearl.api.textures.GpuTexture;
import com.nvidia.rtxremix.minecraft.texture.GlyphPageMirror;
import java.nio.ByteBuffer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Mirrors glyph bytes at the common packed-pixel upload boundary. */
@Mixin(targets = "com.mojang.renderpearl.backend.opengl.GlCommandEncoder")
public abstract class GlStateManagerUploadMixin {
    @Inject(method = "writeToTexture", at = @At("HEAD"))
    private void rtxremix$mirror(GpuTexture texture, ByteBuffer pixels, int mip, int layer,
                                 int x, int y, int width, int height, CallbackInfo ci) {
        GlyphPageMirror page = GlyphPageMirror.uploadTarget();
        if (page != null && mip == 0 && width > 0 && height > 0) {
            int channels = texture.getFormat().blockSize();
            page.blit(x, y, width, height, channels, pixels, width * channels);
        }
    }
}
