/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.llamalad7.mixinextras.injector.wrapmethod.WrapMethod;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.mojang.blaze3d.vertex.PoseStack;
import com.nvidia.rtxremix.minecraft.entity.EntityRenderContext;
import com.nvidia.rtxremix.minecraft.entity.ItemCaptureState;
import com.nvidia.rtxremix.minecraft.terrain.capture.BlockQuadEmitter;
import com.nvidia.rtxremix.minecraft.texture.BlockTextureManager;
import net.minecraft.client.renderer.SubmitNodeCollector;
import net.minecraft.client.renderer.chunk.ChunkSectionLayer;
import net.minecraft.client.renderer.item.ItemStackRenderState;
import net.minecraft.world.item.BlockItem;
import net.minecraft.world.item.ItemDisplayContext;
import net.minecraft.world.item.ItemStack;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Carries item identity and block material classification into the extracted model's submissions. */
@Mixin(ItemStackRenderState.class)
public abstract class ItemRendererMixin implements ItemCaptureState {
    @Shadow public ItemDisplayContext displayContext;
    @Unique private ItemStack rtxremix$item;
    @Override public ItemStack rtxremix$item() { return rtxremix$item; }
    @Override public void rtxremix$setItem(ItemStack item) { rtxremix$item = item; }

    @Inject(method = "clear", at = @At("HEAD"))
    private void rtxremix$clearItem(CallbackInfo ci) { rtxremix$item = null; }

    @WrapMethod(method = "submit")
    private void rtxremix$captureItem(PoseStack matrices, SubmitNodeCollector output,
            int light, int overlay, int outline, Operation<Void> original) {
        var context = EntityRenderContext.get();
        if (!context.isRemixPass()) {
            original.call(matrices, output, light, overlay, outline);
            return;
        }
        var previousState = context.getBlockItemState();
        int previousLayer = context.getBlockItemLayer();
        var previousStack = context.getItemStack();
        var previousMode = context.getItemDisplayContext();
        var block = rtxremix$item != null && rtxremix$item.getItem() instanceof BlockItem item
                ? item.getBlock().defaultBlockState() : null;
        context.setBlockItemState(block);
        context.setBlockItemLayer(block == null ? EntityRenderContext.NO_BLOCK_ITEM
                : BlockTextureManager.layerType(BlockQuadEmitter.classifyBlockLayer(block, ChunkSectionLayer.SOLID)));
        context.setItem(rtxremix$item, displayContext);
        try {
            original.call(matrices, output, light, overlay, outline);
        } finally {
            context.setBlockItemState(previousState);
            context.setBlockItemLayer(previousLayer);
            context.setItem(previousStack, previousMode);
        }
    }
}
