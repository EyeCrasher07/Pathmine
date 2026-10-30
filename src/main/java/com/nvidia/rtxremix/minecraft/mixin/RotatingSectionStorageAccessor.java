/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.mixin;

import net.minecraft.client.RotatingSectionStorage;
import net.minecraft.core.SectionPos;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(RotatingSectionStorage.class)
public interface RotatingSectionStorageAccessor {
    @Accessor("centerSectionPos")
    void rtxremix$setCenterSectionPos(SectionPos position);
}
