/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.nvidia.rtxremix.minecraft.entity.ItemCaptureState;
import net.minecraft.client.renderer.item.ItemModelResolver;
import net.minecraft.client.renderer.item.ItemStackRenderState;
import net.minecraft.world.entity.ItemOwner;
import net.minecraft.world.item.ItemDisplayContext;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.level.Level;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(ItemModelResolver.class)
public abstract class ItemModelResolverMixin {
    @Inject(method = "appendItemLayers", at = @At("HEAD"))
    private void rtxremix$recordLayerItem(ItemStackRenderState output, ItemStack item,
            ItemDisplayContext display, Level level, ItemOwner owner, int seed, CallbackInfo ci) {
        ItemCaptureState source = (ItemCaptureState) output;
        if (source.rtxremix$item() == null) source.rtxremix$setItem(item);
    }

    @Inject(method = "updateForTopItem", at = @At("RETURN"))
    private void rtxremix$recordTopItem(ItemStackRenderState output, ItemStack item,
            ItemDisplayContext display, Level level, ItemOwner owner, int seed, CallbackInfo ci) {
        ((ItemCaptureState) output).rtxremix$setItem(item);
    }
}
