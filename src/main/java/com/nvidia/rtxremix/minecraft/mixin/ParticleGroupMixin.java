/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Lazar Nikolic
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.llamalad7.mixinextras.injector.wrapmethod.WrapMethod;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.nvidia.rtxremix.minecraft.profiling.ProfileScope;
import net.minecraft.client.particle.ParticleGroup;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;

/** Keeps the coarse per-group marker on the26.3 owner of particle ticking. */
@Mixin(ParticleGroup.class)
public abstract class ParticleGroupMixin {
    @Shadow public abstract int size();

    @WrapMethod(method = "tickParticles")
    private void rtxremix$profileGroupTick(Operation<Void> original) {
        try (ProfileScope scope = ProfileScope.beginJfrOnly("ParticleManager.tickParticles")
                .captureCount(size()).plotOnClose("Java.ParticleManager.tickParticles.ms")) {
            original.call();
        }
    }
}
