/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.texture;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Tag;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.AfterAll;
import com.mojang.blaze3d.systems.RenderSystem;
import com.mojang.renderpearl.backend.opengl.GlBackend;
import com.mojang.renderpearl.api.device.GpuDebugOptions;

import java.lang.ref.WeakReference;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.client.renderer.texture.AbstractTexture;
import net.minecraft.client.renderer.texture.DynamicTexture;
import net.minecraft.client.renderer.texture.TextureManager;
import net.minecraft.resources.Identifier;
import net.minecraft.server.packs.resources.ResourceManager;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

import com.mojang.blaze3d.platform.NativeImage;

/** Exercises image lifetime using a real native Renderpearl/OpenGL device. */
@Tag("gpu")
class DynamicTextureCacheGpuTest {
    private static GlBackend backend;

    @BeforeAll
    static void initializeDevice() throws Exception {
        net.minecraft.SharedConstants.tryDetectVersion();
        RenderSystem.initRenderThread();
        RenderSystem.initBackendSystem();
        backend = new GlBackend();
        backend.loadLibrary();
        RenderSystem.initRenderer(backend.createDevice(new GpuDebugOptions(0, false, false, false)));
    }

    @AfterAll
    static void closeDevice() {
        RenderSystem.shutdownRenderer();
        backend.unloadLibrary();
        org.lwjgl.sdl.SDLInit.SDL_Quit();
    }

    @Test
    void clearDropsAClosedImageTextureEvenWhileTheManagerKeepsItRegistered() {
        Identifier id = Identifier.fromNamespaceAndPath("minecraft", "dynamic/closed-map");
        TextureManager manager = new TextureManager(null);
        DynamicTexture texture = new DynamicTexture(() -> "Pathmine texture test", new NativeImage(1, 1, true));
        manager.register(id, texture);
        DynamicTextureCache.onRegisterTexture(id, texture, texture.getPixels());
        assertNotNull(DynamicTextureCache.get(id));

        texture.close();
        assertTrue(texture.getPixels().isClosed());
        assertSame(texture, manager.getTexture(id));
        DynamicTextureCache.clear();

        assertNull(DynamicTextureCache.get(id));
    }

    @Test
    void clearKeepsALiveImageTextureWhenAClosedTextureSharesItsId() {
        Identifier id = Identifier.fromNamespaceAndPath("minecraft", "dynamic/replaced-map");
        DynamicTexture closed = new DynamicTexture(() -> "Pathmine texture test", new NativeImage(1, 1, true));
        DynamicTextureCache.onRegisterTexture(id, closed, closed.getPixels());
        closed.close();
        try (DynamicTexture live = new DynamicTexture(() -> "Pathmine texture test", new NativeImage(1, 1, true))) {
            DynamicTextureCache.onRegisterTexture(id, live, live.getPixels());
            DynamicTextureCache.Pixels captured = DynamicTextureCache.get(id);

            DynamicTextureCache.clear();

            assertSame(captured, DynamicTextureCache.get(id));
        }
    }

    @Test
    void capacityPruningDropsClosedImageTexturesWithoutWaitingForCollection() {
        Identifier closedId = Identifier.fromNamespaceAndPath("minecraft", "dynamic/pruned-map");
        DynamicTexture closed = new DynamicTexture(() -> "Pathmine texture test", new NativeImage(1, 1, true));
        DynamicTextureCache.onRegisterTexture(closedId, closed, closed.getPixels());
        closed.close();
        List<AbstractTexture> live = new ArrayList<>();
        for (int i = 0; i < DynamicTextureCache.capacity(); i++) {
            AbstractTexture texture = newTexture();
            live.add(texture);
            Identifier id = Identifier.fromNamespaceAndPath("minecraft", "skins/closed-map-neighbour-" + i);
            try (NativeImage image = new NativeImage(1, 1, true)) {
                DynamicTextureCache.onRegisterTexture(id, texture, image);
            }
        }

        assertTrue(closed.getPixels().isClosed());
        assertNull(DynamicTextureCache.get(closedId));
        assertEquals(DynamicTextureCache.capacity(), live.size());
    }

    private static AbstractTexture newTexture() {
        return new AbstractTexture() {};
    }
}
