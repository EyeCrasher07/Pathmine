/* SPDX-License-Identifier: GPL-3.0-only */
package com.nvidia.rtxremix.minecraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import com.mojang.blaze3d.systems.RenderSystem;
import com.mojang.blaze3d.vertex.BufferBuilder;
import com.mojang.blaze3d.vertex.MeshData;
import com.mojang.renderpearl.api.buffers.GpuBufferSlice;
import com.nvidia.rtxremix.minecraft.RtxRemixMod;
import com.nvidia.rtxremix.minecraft.sky.WorldRendererStarAccess;
import java.nio.ByteOrder;
import net.minecraft.client.renderer.SkyRenderer;
import net.minecraft.client.renderer.state.level.SkyRenderState;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(SkyRenderer.class)
public abstract class VanillaSkyRendererMixin implements WorldRendererStarAccess {
    @Unique private float[] rtxremix$stars = new float[0];

    @WrapOperation(method = "buildStars", at = @At(value = "INVOKE",
            target = "Lcom/mojang/blaze3d/vertex/BufferBuilder;buildOrThrow()Lcom/mojang/blaze3d/vertex/MeshData;"))
    private MeshData rtxremix$stars(BufferBuilder builder, Operation<MeshData> original) {
        MeshData mesh = original.call(builder);
        var state = mesh.drawState();
        int offset = state.format().getElement(com.mojang.blaze3d.vertex.DefaultVertexFormat.POSITION_SEMANTIC_NAME).offset();
        int stride = state.format().getVertexSize();
        var bytes = mesh.vertexBuffer().duplicate().order(ByteOrder.nativeOrder());
        float[] stars = new float[state.vertexCount() * 3];
        for (int i = 0; i < state.vertexCount(); i++) {
            for (int j = 0; j < 3; j++) stars[i * 3 + j] = bytes.getFloat(i * stride + offset + j * Float.BYTES);
        }
        rtxremix$stars = stars;
        return mesh;
    }

    @Override public float[] rtxremix$copyStarVertices() { return rtxremix$stars.clone(); }

    @Inject(method = "render", at = @At("HEAD"), cancellable = true)
    private void rtxremix$skipCapturedSky(GpuBufferSlice fog, SkyRenderState state, CallbackInfo ci) {
        if (RtxRemixMod.isRemixWorldRendering()) {
            RenderSystem.setShaderFog(fog);
            ci.cancel();
        }
    }
}
