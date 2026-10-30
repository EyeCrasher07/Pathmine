/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.mojang.blaze3d.vertex.PoseStack;
import com.nvidia.rtxremix.minecraft.entity.EntityRenderContext;
import com.nvidia.rtxremix.minecraft.entity.HeldItemEffectsRenderer;
import com.nvidia.rtxremix.minecraft.entity.RemixFeatureCapture;
import net.minecraft.client.renderer.SubmitNodeCollector;
import net.minecraft.client.resources.model.cuboid.ItemTransform;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(targets = "net.minecraft.client.renderer.item.ItemStackRenderState$LayerRenderState")
public abstract class ItemLayerRenderStateMixin {
    @Shadow private ItemTransform itemTransform;

    @Inject(method = "submit", at = @At(value = "INVOKE",
            target = "Lnet/minecraft/client/renderer/SubmitNodeCollector;submitItem(Lcom/mojang/blaze3d/vertex/PoseStack;Lnet/minecraft/world/item/ItemDisplayContext;III[ILnet/minecraft/client/resources/model/geometry/ItemQuads;Lnet/minecraft/client/renderer/item/ItemStackRenderState$FoilType;)V",
            shift = At.Shift.AFTER))
    private void rtxremix$emitHeldEffects(PoseStack matrices, SubmitNodeCollector output,
            int light, int overlay, int outline, CallbackInfo ci) {
        var context = EntityRenderContext.get();
        var capture = RemixFeatureCapture.current();
        if (capture != null && context.getItemStack() != null) {
            HeldItemEffectsRenderer.render(context.getItemStack(), context.getItemDisplayContext(),
                    matrices, capture.provider(), itemTransform.scale().x());
        }
    }
}
