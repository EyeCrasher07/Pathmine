/* SPDX-License-Identifier: GPL-3.0-only */
package com.nvidia.rtxremix.minecraft.texture;

import com.mojang.blaze3d.platform.NativeImage;

/** The animator blends all four unorm channels, including coverage. */
public final class SpriteFrameInterpolation {
    private SpriteFrameInterpolation() {}

    public static void copy(NativeImage source, int x, int y, int nextX, int nextY,
                            float progress, NativeImage destination) {
        for (int row = 0; row < destination.getHeight(); row++) {
            for (int column = 0; column < destination.getWidth(); column++) {
                int current = source.getPixel(x + column, y + row);
                int next = source.getPixel(nextX + column, nextY + row);
                int result = 0;
                for (int shift = 0; shift < 32; shift += 8) {
                    int a = current >>> shift & 255;
                    int b = next >>> shift & 255;
                    result |= Math.round(a * (1.0f - progress) + b * progress) << shift;
                }
                destination.setPixel(column, row, result);
            }
        }
    }
}
