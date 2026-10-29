/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.gl;

import com.mojang.renderpearl.backend.opengl.GlTexture;
import java.nio.ByteBuffer;
import net.minecraft.client.Minecraft;
import org.lwjgl.BufferUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import static org.lwjgl.opengl.GL11.*;
import static org.lwjgl.opengl.GL13.*;
import static org.lwjgl.opengl.GL14.*;
import static org.lwjgl.opengl.GL20.*;
import static org.lwjgl.opengl.GL30.*;
import static org.lwjgl.opengl.GL33.*;

/**
 * Composites Remix's borrowed OpenGL texture into the main color attachment,
 * then composites its premultiplied UI onto the window before presentation.
 * Render thread only. All modified GL state is restored before Renderpearl resumes.
 */
public class TextureOverlay {
    private static final Logger LOGGER = LoggerFactory.getLogger(TextureOverlay.class);
    private static final String VERTEX_SHADER = """
            #version 330 core
            out vec2 texCoord;
            void main() {
                vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
                gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
                texCoord = vec2(p.x, 1.0 - p.y);
            }
            """;
    private static final String FRAGMENT_SHADER = """
            #version 330 core
            in vec2 texCoord;
            uniform sampler2D sourceTexture;
            out vec4 color;
            void main() { color = texture(sourceTexture, texCoord); }
            """;

    private final Minecraft client;
    private final int[] viewport = new int[4];
    private final int[] polygonMode = new int[2];
    private final ByteBuffer colorMask = BufferUtils.createByteBuffer(4);
    private int program;
    private int vertexArray;
    private int framebuffer;
    private int samplerUniform;

    public TextureOverlay() {
        client = Minecraft.getInstance();
    }

    /** Replaces the main color attachment before Minecraft draws the HUD and screens. */
    public void render(int textureId) {
        if (textureId <= 0 || client == null) return;
        var target = client.gameRenderer.mainRenderTarget();
        if (target == null || target.getColorTexture() == null) return;
        if (!(target.getColorTexture() instanceof GlTexture colorTexture)) {
            throw new IllegalStateException("Pathmine OpenGL presentation requires Minecraft's OpenGL backend");
        }
        initialize();
        int previous = glGetInteger(GL_DRAW_FRAMEBUFFER_BINDING);
        try {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
            // Reattach each frame: resize can delete and reuse the same texture name.
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture.glId(), 0);
            if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                throw new IllegalStateException("Pathmine presentation color attachment is incomplete");
            }
            draw(textureId, target.width, target.height, false);
        } finally {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previous);
        }
    }

    /** The runtime UI is premultiplied and must be composed after the final window blit. */
    public void renderNativeOverlay(int textureId) {
        if (textureId <= 0 || client == null || client.getWindow() == null) return;
        initialize();
        int previous = glGetInteger(GL_DRAW_FRAMEBUFFER_BINDING);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        int previousDrawBuffer = glGetInteger(GL_DRAW_BUFFER);
        try {
            glDrawBuffer(GL_BACK);
            draw(textureId, client.getWindow().getWidth(), client.getWindow().getHeight(), true);
        } finally {
            glDrawBuffer(previousDrawBuffer);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previous);
        }
    }

    private void initialize() {
        if (program != 0) return;
        int vertex = compileShader(GL_VERTEX_SHADER, VERTEX_SHADER);
        int fragment = 0;
        int linked = 0;
        try {
            fragment = compileShader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
            linked = glCreateProgram();
            glAttachShader(linked, vertex);
            glAttachShader(linked, fragment);
            glLinkProgram(linked);
            if (glGetProgrami(linked, GL_LINK_STATUS) == GL_FALSE) {
                throw new IllegalStateException("Pathmine composite shader link failed: " + glGetProgramInfoLog(linked));
            }
            samplerUniform = glGetUniformLocation(linked, "sourceTexture");
            vertexArray = glGenVertexArrays();
            framebuffer = glGenFramebuffers();
            program = linked;
            linked = 0;
        } finally {
            glDeleteShader(vertex);
            if (fragment != 0) glDeleteShader(fragment);
            if (linked != 0) glDeleteProgram(linked);
        }
    }

    private static int compileShader(int type, String source) {
        int shader = glCreateShader(type);
        glShaderSource(shader, source);
        glCompileShader(shader);
        if (glGetShaderi(shader, GL_COMPILE_STATUS) == GL_FALSE) {
            String error = glGetShaderInfoLog(shader);
            glDeleteShader(shader);
            throw new IllegalStateException("Pathmine composite shader compile failed: " + error);
        }
        return shader;
    }

    private void draw(int texture, int width, int height, boolean overlay) {
        int previousProgram = glGetInteger(GL_CURRENT_PROGRAM);
        int previousVertexArray = glGetInteger(GL_VERTEX_ARRAY_BINDING);
        int previousActiveTexture = glGetInteger(GL_ACTIVE_TEXTURE);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_POLYGON_MODE, polygonMode);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
        boolean previousDepth = glIsEnabled(GL_DEPTH_TEST);
        boolean previousDepthMask = glGetBoolean(GL_DEPTH_WRITEMASK);
        boolean previousBlend = glIsEnabled(GL_BLEND);
        boolean previousScissor = glIsEnabled(GL_SCISSOR_TEST);
        boolean previousCull = glIsEnabled(GL_CULL_FACE);
        boolean previousStencil = glIsEnabled(GL_STENCIL_TEST);
        boolean previousDiscard = glIsEnabled(GL_RASTERIZER_DISCARD);
        int srcRgb = glGetInteger(GL_BLEND_SRC_RGB);
        int dstRgb = glGetInteger(GL_BLEND_DST_RGB);
        int srcAlpha = glGetInteger(GL_BLEND_SRC_ALPHA);
        int dstAlpha = glGetInteger(GL_BLEND_DST_ALPHA);
        int rgbEquation = glGetInteger(GL_BLEND_EQUATION_RGB);
        int alphaEquation = glGetInteger(GL_BLEND_EQUATION_ALPHA);
        glActiveTexture(GL_TEXTURE0);
        int previousTexture = glGetInteger(GL_TEXTURE_BINDING_2D);
        int previousSampler = glGetIntegeri(GL_SAMPLER_BINDING, 0);
        try {
            glViewport(0, 0, width, height);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(false);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_CULL_FACE);
            glDisable(GL_STENCIL_TEST);
            glDisable(GL_RASTERIZER_DISCARD);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glColorMask(true, true, true, true);
            if (overlay) {
                glEnable(GL_BLEND);
                glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
                glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            } else {
                glDisable(GL_BLEND);
            }
            glUseProgram(program);
            glBindVertexArray(vertexArray);
            glBindTexture(GL_TEXTURE_2D, texture);
            // Interop owns the source's filter/wrap parameters; a vanilla sampler
            // left on unit0 must not replace them.
            glBindSampler(0, 0);
            glUniform1i(samplerUniform, 0);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        } finally {
            glBindSampler(0, previousSampler);
            glBindTexture(GL_TEXTURE_2D, previousTexture);
            glActiveTexture(previousActiveTexture);
            glBindVertexArray(previousVertexArray);
            glUseProgram(previousProgram);
            glBlendEquationSeparate(rgbEquation, alphaEquation);
            glBlendFuncSeparate(srcRgb, dstRgb, srcAlpha, dstAlpha);
            setEnabled(GL_BLEND, previousBlend);
            setEnabled(GL_DEPTH_TEST, previousDepth);
            glDepthMask(previousDepthMask);
            setEnabled(GL_SCISSOR_TEST, previousScissor);
            setEnabled(GL_CULL_FACE, previousCull);
            setEnabled(GL_STENCIL_TEST, previousStencil);
            setEnabled(GL_RASTERIZER_DISCARD, previousDiscard);
            glPolygonMode(GL_FRONT_AND_BACK, polygonMode[0]);
            glColorMask(colorMask.get(0) != 0, colorMask.get(1) != 0,
                    colorMask.get(2) != 0, colorMask.get(3) != 0);
            glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        }
    }

    private static void setEnabled(int state, boolean enabled) {
        if (enabled) glEnable(state); else glDisable(state);
    }

    public void shutdown() {
        if (framebuffer != 0) glDeleteFramebuffers(framebuffer);
        if (vertexArray != 0) glDeleteVertexArrays(vertexArray);
        if (program != 0) glDeleteProgram(program);
        framebuffer = vertexArray = program = 0;
        LOGGER.info("TextureOverlay shutdown");
    }
}
