/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.mojang.renderpearl.backend.opengl.GlBackend;
import com.nvidia.rtxremix.minecraft.RtxRemixMod;
import org.lwjgl.sdl.SDLError;
import org.lwjgl.sdl.SDLVideo;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Raises the OpenGL context the window is created with to what the Remix GL
 * interop needs, after the OpenGL backend applies its defaults.
 */
@Mixin(GlBackend.class)
public abstract class WindowMixin {
    @Inject(
        method = "createWindow",
        at = @At(
            value = "INVOKE",
            target = "Lorg/lwjgl/sdl/SDLVideo;SDL_CreateWindow(Ljava/lang/CharSequence;IIJ)J",
            remap = false
        )
    )
    private void rtxremix$requireOpenGl44(CallbackInfoReturnable<Long> cir) {
        // Core-profile 4.4: the GL interop imports Remix's images and semaphores
        // through EXT_memory_object and EXT_semaphore.
        if (!SDLVideo.SDL_GL_SetAttribute(SDLVideo.SDL_GL_CONTEXT_MAJOR_VERSION, 4)
                || !SDLVideo.SDL_GL_SetAttribute(SDLVideo.SDL_GL_CONTEXT_MINOR_VERSION, 4)
                || !SDLVideo.SDL_GL_SetAttribute(SDLVideo.SDL_GL_CONTEXT_PROFILE_MASK,
                        SDLVideo.SDL_GL_CONTEXT_PROFILE_CORE)) {
            throw new IllegalStateException("Cannot request the Pathmine OpenGL context: " + SDLError.SDL_GetError());
        }
    }

    /** Logs the SDL window pointer, not a platform-specific native window handle. */
    @Inject(
        method = "createWindow",
        at = @At("RETURN")
    )
    private void rtxremix$onWindowCreated(CallbackInfoReturnable<Long> cir) {
        RtxRemixMod.LOGGER.info("OpenGL window created with SDL handle: {}", cir.getReturnValue());
    }
}
