/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.sky;

import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL12;
import org.lwjgl.opengl.GL13;
import org.lwjgl.opengl.GL15;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GL21;
import org.lwjgl.opengl.GL30;
import org.lwjgl.opengl.GL31;
import org.lwjgl.opengl.GL32;
import org.lwjgl.opengl.GL33;
import org.lwjgl.opengl.GL42;

/** Restores the bindings Renderpearl caches across the atmospheric compute pass. */
final class SkyComputeGlState {
    static final int FINAL_SKY_TEXTURE_UNIT = 14;
    static final int TRANSMITTANCE_TEXTURE_UNIT = 15;

    private static final int[] IMAGE_PARAMETERS = {
        GL42.GL_IMAGE_BINDING_NAME,
        GL42.GL_IMAGE_BINDING_LEVEL,
        GL42.GL_IMAGE_BINDING_LAYERED,
        GL42.GL_IMAGE_BINDING_LAYER,
        GL42.GL_IMAGE_BINDING_ACCESS,
        GL42.GL_IMAGE_BINDING_FORMAT
    };
    private static final int[] PACK_PARAMETERS = {
        GL11.GL_PACK_ALIGNMENT,
        GL11.GL_PACK_ROW_LENGTH,
        GL11.GL_PACK_SKIP_ROWS,
        GL11.GL_PACK_SKIP_PIXELS,
        GL12.GL_PACK_IMAGE_HEIGHT,
        GL12.GL_PACK_SKIP_IMAGES,
        GL11.GL_PACK_SWAP_BYTES,
        GL11.GL_PACK_LSB_FIRST
    };

    private final int[] images = new int[4 * IMAGE_PARAMETERS.length];
    private final int[] pack = new int[PACK_PARAMETERS.length];
    private int program;
    private int activeTexture;
    private int finalSkyTexture;
    private int transmittanceTexture;
    private int transmittanceSampler;
    private int uniformBuffer;
    private int uniformBufferZero;
    private long uniformBufferStart;
    private long uniformBufferSize;
    private int packBuffer;

    void save() {
        program = GL11.glGetInteger(GL20.GL_CURRENT_PROGRAM);
        activeTexture = GL11.glGetInteger(GL13.GL_ACTIVE_TEXTURE);
        GL13.glActiveTexture(GL13.GL_TEXTURE0 + FINAL_SKY_TEXTURE_UNIT);
        finalSkyTexture = GL11.glGetInteger(GL11.GL_TEXTURE_BINDING_2D);
        GL13.glActiveTexture(GL13.GL_TEXTURE0 + TRANSMITTANCE_TEXTURE_UNIT);
        transmittanceTexture = GL11.glGetInteger(GL11.GL_TEXTURE_BINDING_2D);
        GL13.glActiveTexture(activeTexture);
        transmittanceSampler = GL30.glGetIntegeri(
                GL33.GL_SAMPLER_BINDING, TRANSMITTANCE_TEXTURE_UNIT);
        uniformBuffer = GL11.glGetInteger(GL31.GL_UNIFORM_BUFFER_BINDING);
        uniformBufferZero = GL30.glGetIntegeri(GL31.GL_UNIFORM_BUFFER_BINDING, 0);
        uniformBufferStart = GL32.glGetInteger64i(GL31.GL_UNIFORM_BUFFER_START, 0);
        uniformBufferSize = GL32.glGetInteger64i(GL31.GL_UNIFORM_BUFFER_SIZE, 0);
        for (int unit = 0; unit < 4; unit++) {
            for (int parameter = 0; parameter < IMAGE_PARAMETERS.length; parameter++) {
                images[unit * IMAGE_PARAMETERS.length + parameter] =
                        GL30.glGetIntegeri(IMAGE_PARAMETERS[parameter], unit);
            }
        }
        packBuffer = GL11.glGetInteger(GL21.GL_PIXEL_PACK_BUFFER_BINDING);
        for (int parameter = 0; parameter < PACK_PARAMETERS.length; parameter++) {
            pack[parameter] = GL11.glGetInteger(PACK_PARAMETERS[parameter]);
        }
    }

    void restore() {
        for (int unit = 0; unit < 4; unit++) {
            int offset = unit * IMAGE_PARAMETERS.length;
            GL42.glBindImageTexture(unit,
                    images[offset], images[offset + 1], images[offset + 2] != 0,
                    images[offset + 3], images[offset + 4], images[offset + 5]);
        }
        if (uniformBufferZero != 0 && uniformBufferSize != 0) {
            GL30.glBindBufferRange(GL31.GL_UNIFORM_BUFFER, 0,
                    uniformBufferZero, uniformBufferStart, uniformBufferSize);
        } else {
            GL30.glBindBufferBase(GL31.GL_UNIFORM_BUFFER, 0, uniformBufferZero);
        }
        GL15.glBindBuffer(GL31.GL_UNIFORM_BUFFER, uniformBuffer);
        GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER, packBuffer);
        for (int parameter = 0; parameter < PACK_PARAMETERS.length; parameter++) {
            GL11.glPixelStorei(PACK_PARAMETERS[parameter], pack[parameter]);
        }
        GL13.glActiveTexture(GL13.GL_TEXTURE0 + FINAL_SKY_TEXTURE_UNIT);
        GL11.glBindTexture(GL11.GL_TEXTURE_2D, finalSkyTexture);
        GL13.glActiveTexture(GL13.GL_TEXTURE0 + TRANSMITTANCE_TEXTURE_UNIT);
        GL11.glBindTexture(GL11.GL_TEXTURE_2D, transmittanceTexture);
        GL33.glBindSampler(TRANSMITTANCE_TEXTURE_UNIT, transmittanceSampler);
        GL13.glActiveTexture(activeTexture);
        GL20.glUseProgram(program);
    }

    static void prepareReadback() {
        GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT, 1);
        for (int parameter = 1; parameter < PACK_PARAMETERS.length; parameter++) {
            GL11.glPixelStorei(PACK_PARAMETERS[parameter], 0);
        }
    }
}
