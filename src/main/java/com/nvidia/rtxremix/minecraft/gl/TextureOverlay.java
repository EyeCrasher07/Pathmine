/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.gl;

import com.mojang.blaze3d.systems.RenderSystem;
import com.mojang.blaze3d.systems.VertexSorter;
import com.mojang.blaze3d.platform.GlStateManager;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.render.BufferBuilder;
import net.minecraft.client.render.BufferRenderer;
import net.minecraft.client.render.GameRenderer;
import net.minecraft.client.render.Tessellator;
import net.minecraft.client.render.VertexFormat;
import net.minecraft.client.render.VertexFormats;
import org.joml.Matrix4f;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL14;
import org.lwjgl.opengl.GL30;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Draws the Remix output texture as a full-screen, fully opaque quad on
 * Minecraft's main framebuffer, after the world and first-person model have
 * been submitted and before the screen overlays and HUD.
 *
 * <p>Render thread only. It goes through {@code RenderSystem} and
 * {@code Tessellator} rather than raw GL so the surrounding vanilla render
 * state stays valid.
 */
public class TextureOverlay {
    
    private static final Logger LOGGER = LoggerFactory.getLogger(TextureOverlay.class);
    
    private final MinecraftClient client;
    
    public TextureOverlay() {
        this.client = MinecraftClient.getInstance();
        LOGGER.info("TextureOverlay initialized");
    }
    
    /**
     * Renders a texture as a full-screen overlay.
     * 
     * @param textureId The GL texture ID to render as full-screen overlay
     */
    public void render(int textureId) {
        if (textureId <= 0) {
            return;
        }
        
        if (client == null || client.getWindow() == null) {
            return;
        }
        
        try {
            renderTextureOverlay(textureId);
        } catch (Exception e) {
            LOGGER.error("Error rendering texture overlay", e);
        }
    }
    
    private void renderTextureOverlay(int texId) {
        renderTextureOverlay(texId, false);
    }

    /** Composites the premultiplied native UI after Minecraft has drawn to the window. */
    public void renderNativeOverlay(int textureId) {
        if (textureId > 0) renderTextureOverlay(textureId, true);
    }

    private void renderTextureOverlay(int texId, boolean overlay) {
        int width = overlay ? client.getWindow().getFramebufferWidth() : client.getWindow().getScaledWidth();
        int height = overlay ? client.getWindow().getFramebufferHeight() : client.getWindow().getScaledHeight();
        int previousFramebuffer = overlay ? GL11.glGetInteger(GL30.GL_DRAW_FRAMEBUFFER_BINDING) : 0;
        boolean previousDepth = overlay && GL11.glIsEnabled(GL11.GL_DEPTH_TEST);
        boolean previousDepthMask = overlay && GL11.glGetBoolean(GL11.GL_DEPTH_WRITEMASK);
        boolean previousBlend = overlay && GL11.glIsEnabled(GL11.GL_BLEND);
        int srcRgb = overlay ? GL11.glGetInteger(GL14.GL_BLEND_SRC_RGB) : 0;
        int dstRgb = overlay ? GL11.glGetInteger(GL14.GL_BLEND_DST_RGB) : 0;
        int srcAlpha = overlay ? GL11.glGetInteger(GL14.GL_BLEND_SRC_ALPHA) : 0;
        int dstAlpha = overlay ? GL11.glGetInteger(GL14.GL_BLEND_DST_ALPHA) : 0;

        if (overlay) {
            GlStateManager._glBindFramebuffer(GL30.GL_DRAW_FRAMEBUFFER, 0);
        } else if (client.getFramebuffer() != null) {
            client.getFramebuffer().beginWrite(true);
        }

        Matrix4f savedProjection = RenderSystem.getProjectionMatrix();

        // Both composites ignore depth. The world replaces the framebuffer and the UI blends over it.
        RenderSystem.disableDepthTest();
        RenderSystem.depthMask(false);
        if (overlay) {
            RenderSystem.enableBlend();
            RenderSystem.blendFuncSeparate(GL11.GL_ONE, GL11.GL_ONE_MINUS_SRC_ALPHA,
                    GL11.GL_ONE, GL11.GL_ONE_MINUS_SRC_ALPHA);
        } else {
            RenderSystem.disableBlend();
        }

        Matrix4f projMatrix = new Matrix4f().setOrtho(
            0.0f, (float) width, (float) height, 0.0f, -1000.0f, 1000.0f
        );
        RenderSystem.setProjectionMatrix(projMatrix, VertexSorter.BY_Z);

        RenderSystem.getModelViewStack().pushMatrix();
        RenderSystem.getModelViewStack().identity();
        RenderSystem.applyModelViewMatrix();

        RenderSystem.setShader(GameRenderer::getPositionTexProgram);
        RenderSystem.setShaderTexture(0, texId);
        RenderSystem.setShaderColor(1.0f, 1.0f, 1.0f, 1.0f);

        // Standard GL UVs, with (0,0) at the top left: the interop blit already
        // corrects the Vulkan image's orientation.
        Matrix4f identity = new Matrix4f();
        BufferBuilder bufferBuilder = Tessellator.getInstance()
            .begin(VertexFormat.DrawMode.QUADS, VertexFormats.POSITION_TEXTURE);

        bufferBuilder.vertex(identity, 0,     0,      0).texture(0.0f, 0.0f);
        bufferBuilder.vertex(identity, 0,     height, 0).texture(0.0f, 1.0f);
        bufferBuilder.vertex(identity, width, height, 0).texture(1.0f, 1.0f);
        bufferBuilder.vertex(identity, width, 0,      0).texture(1.0f, 0.0f);

        BufferRenderer.drawWithGlobalProgram(bufferBuilder.end());

        RenderSystem.getModelViewStack().popMatrix();
        RenderSystem.applyModelViewMatrix();
        RenderSystem.setProjectionMatrix(savedProjection, VertexSorter.BY_Z);

        if (overlay) {
            GlStateManager._glBindFramebuffer(GL30.GL_DRAW_FRAMEBUFFER, previousFramebuffer);
            RenderSystem.depthMask(previousDepthMask);
            if (previousDepth) RenderSystem.enableDepthTest(); else RenderSystem.disableDepthTest();
            RenderSystem.blendFuncSeparate(srcRgb, dstRgb, srcAlpha, dstAlpha);
            if (previousBlend) RenderSystem.enableBlend(); else RenderSystem.disableBlend();
        } else {
            RenderSystem.depthMask(true);
            RenderSystem.enableDepthTest();
        }
        RenderSystem.setShaderColor(1.0f, 1.0f, 1.0f, 1.0f);
    }
    
    /**
     * Cleans up resources.
     */
    public void shutdown() {
        LOGGER.info("TextureOverlay shutdown");
    }
}
