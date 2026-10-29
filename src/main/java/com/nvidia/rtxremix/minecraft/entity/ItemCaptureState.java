/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.entity;

import net.minecraft.world.item.ItemStack;

/** Item identity retained alongside the extracted model layers. */
public interface ItemCaptureState {
    ItemStack rtxremix$item();
    void rtxremix$setItem(ItemStack item);
}
