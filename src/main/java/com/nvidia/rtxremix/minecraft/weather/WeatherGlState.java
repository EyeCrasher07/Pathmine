/* SPDX-License-Identifier: GPL-3.0-only */
package com.nvidia.rtxremix.minecraft.weather;

import java.nio.ByteBuffer;
import org.lwjgl.BufferUtils;
import static org.lwjgl.opengl.GL11.*;
import static org.lwjgl.opengl.GL13.*;
import static org.lwjgl.opengl.GL14.*;
import static org.lwjgl.opengl.GL15.*;
import static org.lwjgl.opengl.GL20.*;
import static org.lwjgl.opengl.GL30.*;
import static org.lwjgl.opengl.GL31.*;
import static org.lwjgl.opengl.GL32.*;
import static org.lwjgl.opengl.GL33.*;

/** Restores every GL binding and fixed-function setting touched by the weather pass. */
final class WeatherGlState {
    private static final int[] CAPABILITIES = {GL_DEPTH_TEST, GL_BLEND, GL_SCISSOR_TEST,
            GL_CULL_FACE, GL_STENCIL_TEST, GL_RASTERIZER_DISCARD, GL_FRAMEBUFFER_SRGB};
    private final boolean[] enabled = new boolean[CAPABILITIES.length];
    private final int[] viewport = new int[4];
    private final int[] polygon = new int[2];
    private final int[] textures = new int[3];
    private final int[] samplers = new int[3];
    private final double[] depthRange = new double[2];
    private final ByteBuffer colorMask = BufferUtils.createByteBuffer(4);
    private int program, vao, arrayBuffer, framebuffer, activeTexture, depthFunction;
    private int srcRgb, dstRgb, srcAlpha, dstAlpha, rgbEquation, alphaEquation;
    private int uniformBuffer, fogBuffer;
    private long fogOffset, fogSize;
    private boolean depthMask;

    void capture() {
        program = glGetInteger(GL_CURRENT_PROGRAM);
        vao = glGetInteger(GL_VERTEX_ARRAY_BINDING);
        arrayBuffer = glGetInteger(GL_ARRAY_BUFFER_BINDING);
        framebuffer = glGetInteger(GL_DRAW_FRAMEBUFFER_BINDING);
        activeTexture = glGetInteger(GL_ACTIVE_TEXTURE);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_POLYGON_MODE, polygon);
        glGetDoublev(GL_DEPTH_RANGE, depthRange);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
        depthMask = glGetBoolean(GL_DEPTH_WRITEMASK);
        depthFunction = glGetInteger(GL_DEPTH_FUNC);
        srcRgb = glGetInteger(GL_BLEND_SRC_RGB);
        dstRgb = glGetInteger(GL_BLEND_DST_RGB);
        srcAlpha = glGetInteger(GL_BLEND_SRC_ALPHA);
        dstAlpha = glGetInteger(GL_BLEND_DST_ALPHA);
        rgbEquation = glGetInteger(GL_BLEND_EQUATION_RGB);
        alphaEquation = glGetInteger(GL_BLEND_EQUATION_ALPHA);
        uniformBuffer = glGetInteger(GL_UNIFORM_BUFFER_BINDING);
        fogBuffer = glGetIntegeri(GL_UNIFORM_BUFFER_BINDING, 0);
        fogOffset = glGetInteger64i(GL_UNIFORM_BUFFER_START, 0);
        fogSize = glGetInteger64i(GL_UNIFORM_BUFFER_SIZE, 0);
        for (int i = 0; i < CAPABILITIES.length; i++) enabled[i] = glIsEnabled(CAPABILITIES[i]);
        for (int i = 0; i < textures.length; i++) {
            glActiveTexture(GL_TEXTURE0 + i);
            textures[i] = glGetInteger(GL_TEXTURE_BINDING_2D);
            samplers[i] = glGetIntegeri(GL_SAMPLER_BINDING, i);
        }
        glActiveTexture(activeTexture);
    }

    void restore() {
        for (int i = 0; i < textures.length; i++) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, textures[i]);
            glBindSampler(i, samplers[i]);
        }
        glActiveTexture(activeTexture);
        if (fogBuffer != 0 && fogSize > 0) glBindBufferRange(GL_UNIFORM_BUFFER, 0, fogBuffer, fogOffset, fogSize);
        else glBindBufferBase(GL_UNIFORM_BUFFER, 0, fogBuffer);
        glBindBuffer(GL_UNIFORM_BUFFER, uniformBuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
        glUseProgram(program);
        glBlendEquationSeparate(rgbEquation, alphaEquation);
        glBlendFuncSeparate(srcRgb, dstRgb, srcAlpha, dstAlpha);
        glDepthFunc(depthFunction);
        glDepthRange(depthRange[0], depthRange[1]);
        glDepthMask(depthMask);
        glColorMask(colorMask.get(0) != 0, colorMask.get(1) != 0,
                colorMask.get(2) != 0, colorMask.get(3) != 0);
        glPolygonMode(GL_FRONT_AND_BACK, polygon[0]);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        for (int i = 0; i < CAPABILITIES.length; i++) {
            if (enabled[i]) glEnable(CAPABILITIES[i]); else glDisable(CAPABILITIES[i]);
        }
    }
}
