/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.weather;

import com.mojang.blaze3d.systems.RenderSystem;
import com.mojang.blaze3d.vertex.DefaultVertexFormat;
import com.mojang.renderpearl.backend.opengl.GlBuffer;
import com.mojang.renderpearl.backend.opengl.GlSampler;
import com.mojang.renderpearl.backend.opengl.GlTexture;
import java.io.IOException;
import java.nio.ByteBuffer;
import net.minecraft.client.Camera;
import net.minecraft.client.Minecraft;
import net.minecraft.resources.Identifier;
import org.joml.Matrix4f;
import org.joml.Quaternionf;
import org.lwjgl.opengl.GL;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL13;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GL30;
import org.lwjgl.system.MemoryStack;
import static org.lwjgl.opengl.GL11.*;
import static org.lwjgl.opengl.GL13.*;
import static org.lwjgl.opengl.GL14.*;
import static org.lwjgl.opengl.GL15.*;
import static org.lwjgl.opengl.GL20.*;
import static org.lwjgl.opengl.GL30.*;
import static org.lwjgl.opengl.GL31.*;
import static org.lwjgl.opengl.GL32.*;
import static org.lwjgl.opengl.GL33.*;

/** Composites vanilla weather geometry over Remix using the authored rain refraction. */
public final class RemixWeatherRenderer {
    private static final float NEAR_PLANE = 0.05f;
    private static final float FAR_PLANE = 1000.0f;
    private static final Identifier RAIN_TEXTURE = Identifier.withDefaultNamespace("textures/environment/rain.png");
    private static final Identifier SNOW_TEXTURE = Identifier.withDefaultNamespace("textures/environment/snow.png");
    private static final Identifier RAIN_VERTEX = Identifier.fromNamespaceAndPath("rtx-remix-renderer", "shaders/core/rain_refraction.vsh");
    private static final Identifier RAIN_FRAGMENT = Identifier.fromNamespaceAndPath("rtx-remix-renderer", "shaders/core/rain_refraction.fsh");
    private final Matrix4f projection = new Matrix4f();
    private final Matrix4f modelView = new Matrix4f();
    private final Quaternionf cameraRotation = new Quaternionf();
    private final WeatherGlState state = new WeatherGlState();
    private float fov;
    private float aspect;
    private boolean cameraReady;
    private int rainColumns;
    private int snowColumns;
    private int lastDrawCount;
    private int lastQuadCount;
    private int vertexArray;
    private int vertexBuffer;
    private int indexBuffer;
    private int indexCapacity;
    private int framebuffer;
    private int depthAttachment;
    private int depthWidth;
    private int depthHeight;
    private int rainProgram;
    private int snowProgram;
    private int lightSampler;
    private DepthSeeder depthSeeder;

    public void beginCaptureFrame() {
        rainColumns = snowColumns = lastDrawCount = lastQuadCount = 0;
    }

    public void updateCamera(Camera camera, float fovYDegrees, float aspect) {
        camera.rotation().conjugate(cameraRotation);
        modelView.rotation(cameraRotation);
        this.fov = (float) Math.toRadians(fovYDegrees);
        this.aspect = aspect;
        cameraReady = true;
    }

    /** Copies the exact packed mesh while vanilla still owns its borrowed CPU bytes. */
    public void capture(ByteBuffer vertices, int rain, int snow) {
        int count = Math.addExact(rain, snow);
        int stride = DefaultVertexFormat.PARTICLE.getVertexSize();
        if (vertices.remaining() != Math.multiplyExact(count, stride * 4)) {
            throw new IllegalArgumentException("Weather vertex count does not match the extracted columns");
        }
        int oldVao = glGetInteger(GL_VERTEX_ARRAY_BINDING);
        int oldBuffer = glGetInteger(GL_ARRAY_BUFFER_BINDING);
        try {
            initializeGeometry();
            glBindVertexArray(vertexArray);
            glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
            glBufferData(GL_ARRAY_BUFFER, vertices, GL_STREAM_DRAW);
            int indices = Math.multiplyExact(count, 6);
            if (indices > indexCapacity) {
                int[] data = new int[indices];
                for (int q = 0; q < count; q++) {
                    int i = q * 6, v = q * 4;
                    data[i] = v; data[i + 1] = v + 1; data[i + 2] = v + 2;
                    data[i + 3] = v; data[i + 4] = v + 2; data[i + 5] = v + 3;
                }
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, data, GL_STATIC_DRAW);
                indexCapacity = indices;
            }
            rainColumns = rain;
            snowColumns = snow;
        } finally {
            glBindVertexArray(oldVao);
            glBindBuffer(GL_ARRAY_BUFFER, oldBuffer);
        }
    }

    public void render(int finalColorTexture, int depthTexture) {
        if (rainColumns + snowColumns == 0) return;
        if (!cameraReady || finalColorTexture <= 0 || depthTexture <= 0) {
            throw new IllegalStateException("Weather camera or Remix textures are unavailable");
        }
        var client = Minecraft.getInstance();
        var target = client.gameRenderer.mainRenderTarget();
        if (!(target.getColorTexture() instanceof GlTexture color)
                || !(client.gameRenderer.lightmap().texture() instanceof GlTexture lightmap)) {
            throw new IllegalStateException("Pathmine weather requires the OpenGL backend");
        }
        state.capture();
        try {
            initializePrograms();
            attach(color.glId(), target.width, target.height);
            glViewport(0, 0, target.width, target.height);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_CULL_FACE);
            glDisable(GL_STENCIL_TEST);
            glDisable(GL_RASTERIZER_DISCARD);
            glDisable(GL_FRAMEBUFFER_SRGB);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glDepthRange(0.0, 1.0);
            depthSeeder.seed(depthTexture, target.width, target.height);
            glColorMask(true, true, true, true);
            glDepthMask(false);
            glDepthFunc(GL_LEQUAL);
            glEnable(GL_BLEND);
            glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
            boolean zeroToOne = (GL.getCapabilities().OpenGL45 || GL.getCapabilities().GL_ARB_clip_control)
                    && glGetInteger(org.lwjgl.opengl.GL45.GL_CLIP_DEPTH_MODE) == org.lwjgl.opengl.GL45.GL_ZERO_TO_ONE;
            projection.setPerspective(fov, aspect, NEAR_PLANE, FAR_PLANE, zeroToOne);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, finalColorTexture);
            glBindSampler(1, 0);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, lightmap.glId());
            glBindSampler(2, lightSampler);
            glBindVertexArray(vertexArray);
            if (rainColumns > 0) draw(rainProgram, RAIN_TEXTURE, 0, rainColumns);
            if (snowColumns > 0) {
                var fog = RenderSystem.getShaderFog();
                glBindBufferRange(GL_UNIFORM_BUFFER, 0, ((GlBuffer) fog.buffer()).handle(), fog.offset(), fog.length());
                draw(snowProgram, SNOW_TEXTURE, rainColumns, snowColumns);
            }
            lastDrawCount = (rainColumns > 0 ? 1 : 0) + (snowColumns > 0 ? 1 : 0);
            lastQuadCount = rainColumns + snowColumns;
        } finally {
            state.restore();
            rainColumns = snowColumns = 0;
        }
    }

    private void draw(int program, Identifier texture, int start, int count) {
        glUseProgram(program);
        try (MemoryStack stack = MemoryStack.stackPush()) {
            glUniformMatrix4fv(glGetUniformLocation(program, "ModelViewMat"), false, modelView.get(stack.mallocFloat(16)));
            glUniformMatrix4fv(glGetUniformLocation(program, "ProjMat"), false, projection.get(stack.mallocFloat(16)));
        }
        glUniform4f(glGetUniformLocation(program, "ColorModulator"), 1, 1, 1, 1);
        glUniform1i(glGetUniformLocation(program, "Sampler0"), 0);
        glUniform1i(glGetUniformLocation(program, "Sampler1"), 1);
        glUniform1i(glGetUniformLocation(program, "Sampler2"), 2);
        glActiveTexture(GL_TEXTURE0);
        var source = Minecraft.getInstance().getTextureManager().getTexture(texture);
        glBindTexture(GL_TEXTURE_2D, ((GlTexture) source.getTexture()).glId());
        // Keep vanilla's nearest filtering unless the resource pack requests blur.
        // The texture manager owns this sampler, including its reload lifetime.
        glBindSampler(0, ((GlSampler) source.getSampler()).getId());
        glDrawElements(GL_TRIANGLES, count * 6, GL_UNSIGNED_INT, (long) start * 6 * Integer.BYTES);
    }

    private void initializeGeometry() {
        if (vertexArray != 0) return;
        vertexArray = glGenVertexArrays();
        vertexBuffer = glGenBuffers();
        indexBuffer = glGenBuffers();
        glBindVertexArray(vertexArray);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
        var format = DefaultVertexFormat.PARTICLE;
        int stride = format.getVertexSize();
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, false, stride, format.getElement(com.mojang.blaze3d.vertex.DefaultVertexFormat.POSITION_SEMANTIC_NAME).offset());
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, false, stride, format.getElement(com.mojang.blaze3d.vertex.DefaultVertexFormat.UV0_SEMANTIC_NAME).offset());
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, true, stride, format.getElement(com.mojang.blaze3d.vertex.DefaultVertexFormat.COLOR_SEMANTIC_NAME).offset());
        glEnableVertexAttribArray(3);
        glVertexAttribIPointer(3, 2, GL_SHORT, stride, format.getElement(com.mojang.blaze3d.vertex.DefaultVertexFormat.UV2_SEMANTIC_NAME).offset());
    }

    private void initializePrograms() {
        if (rainProgram != 0) return;
        String vertex = readShader(RAIN_VERTEX);
        rainProgram = link(vertex, readShader(RAIN_FRAGMENT));
        try {
            snowProgram = link(SNOW_VERTEX, SNOW_FRAGMENT);
            glUniformBlockBinding(snowProgram, glGetUniformBlockIndex(snowProgram, "Fog"), 0);
            depthSeeder = new DepthSeeder();
            framebuffer = glGenFramebuffers();
            lightSampler = glGenSamplers();
            glSamplerParameteri(lightSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glSamplerParameteri(lightSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glSamplerParameteri(lightSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glSamplerParameteri(lightSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        } catch (RuntimeException failure) {
            cleanup();
            throw failure;
        }
    }

    private void attach(int color, int width, int height) {
        if (depthAttachment == 0 || width != depthWidth || height != depthHeight) {
            if (depthAttachment != 0) glDeleteTextures(depthAttachment);
            depthAttachment = glGenTextures();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, depthAttachment);
            org.lwjgl.opengl.GL42.glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT32F, width, height);
            depthWidth = width;
            depthHeight = height;
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthAttachment, 0);
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            throw new IllegalStateException("Pathmine weather framebuffer is incomplete");
        }
    }

    private static String readShader(Identifier id) {
        var resources = Minecraft.getInstance().getResourceManager();
        try (var input = resources.getResourceOrThrow(id).open()) {
            return new String(input.readAllBytes(), java.nio.charset.StandardCharsets.UTF_8);
        } catch (IOException e) {
            throw new IllegalStateException("Could not load weather shader " + id, e);
        }
    }

    private static int link(String vertexSource, String fragmentSource) {
        int vertex = DepthSeeder.compileShader(GL_VERTEX_SHADER, vertexSource);
        int fragment = 0;
        int program = 0;
        try {
            fragment = DepthSeeder.compileShader(GL_FRAGMENT_SHADER, fragmentSource);
            program = glCreateProgram();
            glAttachShader(program, vertex);
            glAttachShader(program, fragment);
            glBindAttribLocation(program, 0, "Position");
            glBindAttribLocation(program, 1, "UV0");
            glBindAttribLocation(program, 2, "Color");
            glBindAttribLocation(program, 3, "UV2");
            glLinkProgram(program);
            if (glGetProgrami(program, GL_LINK_STATUS) == GL_FALSE) {
                throw new IllegalStateException("Weather shader link failed: " + glGetProgramInfoLog(program));
            }
            int result = program;
            program = 0;
            return result;
        } finally {
            glDeleteShader(vertex);
            if (fragment != 0) glDeleteShader(fragment);
            if (program != 0) glDeleteProgram(program);
        }
    }

    public int getLastDrawCount() { return lastDrawCount; }
    public int getLastQuadCount() { return lastQuadCount; }

    public void cleanup() {
        beginCaptureFrame();
        if (rainProgram != 0) glDeleteProgram(rainProgram);
        if (snowProgram != 0) glDeleteProgram(snowProgram);
        if (vertexArray != 0) glDeleteVertexArrays(vertexArray);
        if (vertexBuffer != 0) glDeleteBuffers(vertexBuffer);
        if (indexBuffer != 0) glDeleteBuffers(indexBuffer);
        if (framebuffer != 0) glDeleteFramebuffers(framebuffer);
        if (depthAttachment != 0) glDeleteTextures(depthAttachment);
        if (lightSampler != 0) glDeleteSamplers(lightSampler);
        if (depthSeeder != null) depthSeeder.destroy();
        rainProgram = snowProgram = vertexArray = vertexBuffer = indexBuffer = framebuffer = depthAttachment = lightSampler = indexCapacity = 0;
        depthSeeder = null;
    }

    private static final String SNOW_VERTEX = """
            #version 150
            in vec3 Position;
            in vec2 UV0;
            in vec4 Color;
            in ivec2 UV2;
            uniform mat4 ModelViewMat;
            uniform mat4 ProjMat;
            uniform sampler2D Sampler2;
            out vec2 texCoord;
            out vec4 vertexColor;
            out float sphericalDistance;
            out float cylindricalDistance;
            void main() {
                gl_Position = ProjMat * ModelViewMat * vec4(Position, 1.0);
                texCoord = UV0;
                vertexColor = Color * texture(Sampler2, clamp(vec2(UV2) / 256.0 + 0.5 / 16.0,
                    vec2(0.5 / 16.0), vec2(15.5 / 16.0)));
                sphericalDistance = length(Position);
                cylindricalDistance = max(length(Position.xz), abs(Position.y));
            }
            """;

    private static final String SNOW_FRAGMENT = """
            #version 150
            uniform sampler2D Sampler0;
            layout(std140) uniform Fog {
                vec4 FogColor;
                float EnvironmentalStart;
                float EnvironmentalEnd;
                float DistanceStart;
                float DistanceEnd;
                float SkyEnd;
                float CloudsEnd;
            };
            in vec2 texCoord;
            in vec4 vertexColor;
            in float sphericalDistance;
            in float cylindricalDistance;
            out vec4 fragColor;
            float fogAt(float distance, float start, float end) {
                if (distance <= start) return 0.0;
                if (distance >= end) return 1.0;
                return (distance - start) / (end - start);
            }
            void main() {
                vec4 color = texture(Sampler0, texCoord) * vertexColor;
                if (color.a < 0.1) discard;
                float fog = max(fogAt(sphericalDistance, EnvironmentalStart, EnvironmentalEnd),
                    fogAt(cylindricalDistance, DistanceStart, DistanceEnd));
                fragColor = vec4(mix(color.rgb, FogColor.rgb, fog * FogColor.a), color.a);
            }
            """;

    private static final class DepthSeeder {
        private static final String VERTEX_SHADER = """
                #version 150
                void main() {
                    vec2 position = vec2(
                        float((gl_VertexID << 1) & 2),
                        float(gl_VertexID & 2));
                    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
                }
                """;

        private static final String FRAGMENT_SHADER = """
                #version 150
                uniform sampler2D RemixDepth;
                uniform vec2 FramebufferSize;
                void main() {
                    vec2 uv = vec2(
                        gl_FragCoord.x / FramebufferSize.x,
                        1.0 - gl_FragCoord.y / FramebufferSize.y);
                    gl_FragDepth = texture(RemixDepth, uv).r;
                }
                """;

        private final int program;
        private final int vertexArray;
        private final int remixDepthLocation;
        private final int framebufferSizeLocation;

        private DepthSeeder() {
            int vertexShader = compileShader(GL20.GL_VERTEX_SHADER, VERTEX_SHADER);
            int fragmentShader = 0;
            int linkedProgram = 0;
            try {
                fragmentShader = compileShader(GL20.GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
                linkedProgram = GL20.glCreateProgram();
                GL20.glAttachShader(linkedProgram, vertexShader);
                GL20.glAttachShader(linkedProgram, fragmentShader);
                GL20.glLinkProgram(linkedProgram);
                if (GL20.glGetProgrami(linkedProgram, GL20.GL_LINK_STATUS) == GL11.GL_FALSE) {
                    String log = GL20.glGetProgramInfoLog(linkedProgram);
                    GL20.glDeleteProgram(linkedProgram);
                    linkedProgram = 0;
                    throw new IllegalStateException("Failed to link depth seeding shader: " + log);
                }
            } finally {
                GL20.glDeleteShader(vertexShader);
                if (fragmentShader != 0) {
                    GL20.glDeleteShader(fragmentShader);
                }
            }
            program = linkedProgram;
            remixDepthLocation = GL20.glGetUniformLocation(program, "RemixDepth");
            framebufferSizeLocation = GL20.glGetUniformLocation(program, "FramebufferSize");
            vertexArray = GL30.glGenVertexArrays();
        }

        private void seed(int depthTexture, int width, int height) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_ALWAYS);
            glDepthMask(true);
            glColorMask(false, false, false, false);
            glDisable(GL_BLEND);
            glUseProgram(program);
            glUniform1i(remixDepthLocation, 0);
            glUniform2f(framebufferSizeLocation, width, height);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, depthTexture);
            glBindSampler(0, 0);
            glBindVertexArray(vertexArray);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        private void destroy() {
            GL30.glDeleteVertexArrays(vertexArray);
            GL20.glDeleteProgram(program);
        }

        private static int compileShader(int type, String source) {
            int shader = GL20.glCreateShader(type);
            GL20.glShaderSource(shader, source);
            GL20.glCompileShader(shader);
            if (GL20.glGetShaderi(shader, GL20.GL_COMPILE_STATUS) == GL11.GL_FALSE) {
                String log = GL20.glGetShaderInfoLog(shader);
                GL20.glDeleteShader(shader);
                throw new IllegalStateException("Failed to compile depth seeding shader: " + log);
            }
            return shader;
        }
    }
}
