/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.mixin;

import net.minecraft.client.ClientClockManager;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(ClientClockManager.ClientClockInstance.class)
public interface ClientClockInstanceAccessor {
    @Accessor("totalTicks")
    void rtxremix$setTotalTicks(long ticks);

    @Accessor("partialTick")
    void rtxremix$setPartialTick(float partialTick);
}
