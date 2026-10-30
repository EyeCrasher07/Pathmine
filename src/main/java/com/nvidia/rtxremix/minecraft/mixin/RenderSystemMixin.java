/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.nvidia.rtxremix.minecraft.RtxRemixMod;
import com.nvidia.rtxremix.minecraft.RemixRenderer;
import com.mojang.renderpearl.backend.opengl.GlSurface;
import org.lwjgl.sdl.SDLVideo;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

/**
 * Integrates {@link RemixRenderer#presentFrameVk()} with vanilla's buffer swap
 * inside the OpenGL surface presentation. Before initialization the SDL swap
 * runs unchanged. Windows direct presentation still belongs to Remix.
 */
@Mixin(GlSurface.class)
public abstract class RenderSystemMixin {

    @Redirect(
        method = "present",
        at = @At(value = "INVOKE", target = "Lorg/lwjgl/sdl/SDLVideo;SDL_GL_SwapWindow(J)Z", remap = false)
    )
    private boolean rtxremix$redirectSwapBuffers(long window) {
        RemixRenderer renderer = RtxRemixMod.renderer;
        if (renderer != null && renderer.isInitialized()) {
            renderer.presentFrameVk();
            if (renderer.presentsThroughOpenGl()) {
                return SDLVideo.SDL_GL_SwapWindow(window);
            }
            return true;
        } else {
            return SDLVideo.SDL_GL_SwapWindow(window);
        }
    }
}
