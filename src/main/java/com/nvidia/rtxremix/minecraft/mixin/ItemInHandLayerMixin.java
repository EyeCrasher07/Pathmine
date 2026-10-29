/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.llamalad7.mixinextras.injector.wrapmethod.WrapMethod;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.mojang.blaze3d.vertex.PoseStack;
import com.nvidia.rtxremix.minecraft.entity.CapturedRenderState;
import com.nvidia.rtxremix.minecraft.entity.EntityRenderContext;
import net.minecraft.client.renderer.SubmitNodeCollector;
import net.minecraft.client.renderer.entity.layers.ItemInHandLayer;
import net.minecraft.client.renderer.entity.state.ArmedEntityRenderState;
import net.minecraft.client.renderer.item.ItemStackRenderState;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.HumanoidArm;
import net.minecraft.world.item.ItemStack;
import org.spongepowered.asm.mixin.Mixin;

@Mixin(ItemInHandLayer.class)
public abstract class ItemInHandLayerMixin {
    @SuppressWarnings("unchecked")
    @WrapMethod(method = "submitArmWithItem")
    private void rtxremix$heldOwner(ArmedEntityRenderState state, ItemStackRenderState item,
            ItemStack stack, HumanoidArm arm, PoseStack matrices, SubmitNodeCollector output,
            int light, Operation<Void> original) {
        var context = EntityRenderContext.get();
        LivingEntity previous = context.getHeldItemHolder();
        Entity owner = ((CapturedRenderState<Entity>) state).rtxremix$owner();
        if (context.isRemixPass() && owner instanceof LivingEntity living) {
            context.setHeldItemHolder(living);
        }
        try {
            original.call(state, item, stack, arm, matrices, output, light);
        } finally {
            context.setHeldItemHolder(previous);
        }
    }
}
