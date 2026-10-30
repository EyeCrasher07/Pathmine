/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.llamalad7.mixinextras.injector.wrapmethod.WrapMethod;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.mojang.blaze3d.vertex.PoseStack;
import com.nvidia.rtxremix.minecraft.entity.EntityRenderContext;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.FirstPersonHandsAndItemsRenderer;
import net.minecraft.client.renderer.SubmitNodeCollector;
import net.minecraft.client.renderer.state.level.FirstPersonHandsAndItemsRenderState;
import net.minecraft.client.renderer.state.level.PlayerRenderState;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.item.ItemStack;
import org.spongepowered.asm.mixin.Mixin;

@Mixin(FirstPersonHandsAndItemsRenderer.class)
public abstract class HeldItemRendererMixin {
    @WrapMethod(method = "submitArmWithItem")
    private void rtxremix$firstPersonHand(PlayerRenderState player,
            FirstPersonHandsAndItemsRenderState state, float partialTick, float pitch,
            InteractionHand hand, float swing, ItemStack item, float equip,
            PoseStack matrices, SubmitNodeCollector output, int light, Operation<Void> original) {
        var context = EntityRenderContext.get();
        var previousHand = context.getActiveHand();
        var previousHolder = context.getHeldItemHolder();
        if (context.isRemixPass()) {
            context.setActiveHand(hand);
            context.setHeldItemHolder(Minecraft.getInstance().player);
        }
        try {
            original.call(player, state, partialTick, pitch, hand, swing, item, equip, matrices, output, light);
        } finally {
            context.setActiveHand(previousHand);
            context.setHeldItemHolder(previousHolder);
        }
    }
}
