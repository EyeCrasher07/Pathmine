/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.nvidia.rtxremix.minecraft.entity.CapturedRenderState;
import net.minecraft.client.renderer.blockentity.state.BlockEntityRenderState;
import net.minecraft.world.level.block.entity.BlockEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;

@Mixin(BlockEntityRenderState.class)
public abstract class BlockEntityRenderStateMixin implements CapturedRenderState<BlockEntity> {
    @Unique private BlockEntity rtxremix$owner;
    @Unique private float rtxremix$partialTick;
    @Override public BlockEntity rtxremix$owner() { return rtxremix$owner; }
    @Override public float rtxremix$partialTick() { return rtxremix$partialTick; }
    @Override public void rtxremix$setSource(BlockEntity owner, float partialTick) {
        rtxremix$owner = owner;
        rtxremix$partialTick = partialTick;
    }
}
