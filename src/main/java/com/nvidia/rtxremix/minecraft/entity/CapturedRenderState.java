/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.entity;

/** The scene owner and interpolation time associated with an extracted vanilla render state. */
public interface CapturedRenderState<T> {
    T rtxremix$owner();
    float rtxremix$partialTick();
    void rtxremix$setSource(T owner, float partialTick);
}
