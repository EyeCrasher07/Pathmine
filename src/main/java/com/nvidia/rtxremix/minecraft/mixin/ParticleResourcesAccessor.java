/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Lazar Nikolic
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.mixin;

import java.util.Map;
import net.minecraft.client.particle.ParticleResources;
import net.minecraft.resources.Identifier;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(ParticleResources.class)
public interface ParticleResourcesAccessor {
    /** Values implement SpriteSet; their concrete vanilla class is private. */
    @Accessor("spriteSets")
    Map<Identifier, ?> getSpriteAwareFactories();
}
