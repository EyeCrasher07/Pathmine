/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.nvidia.rtxremix.minecraft.entity.CapturedRenderState;
import net.minecraft.client.renderer.entity.state.EntityRenderState;
import net.minecraft.world.entity.Entity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;

@Mixin(EntityRenderState.class)
public abstract class EntityRenderStateMixin implements CapturedRenderState<Entity> {
    @Unique private Entity rtxremix$owner;
    @Unique private float rtxremix$partialTick;
    @Override public Entity rtxremix$owner() { return rtxremix$owner; }
    @Override public float rtxremix$partialTick() { return rtxremix$partialTick; }
    @Override public void rtxremix$setSource(Entity owner, float partialTick) {
        rtxremix$owner = owner;
        rtxremix$partialTick = partialTick;
    }
}
