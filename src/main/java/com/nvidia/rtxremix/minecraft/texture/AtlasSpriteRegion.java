/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Lazar Nikolic
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.texture;

import net.minecraft.client.renderer.texture.TextureAtlasSprite;

/** Mip-zero sprite pixels, accounting for26.3's atlas padding outside its UV rectangle. */
record AtlasSpriteRegion(int x, int y, int width, int height, int padding) {
    static AtlasSpriteRegion of(TextureAtlasSprite sprite, int atlasWidth, int atlasHeight) {
        int x = Math.round(sprite.getU0() * atlasWidth);
        int y = Math.round(sprite.getV0() * atlasHeight);
        int padding = x - sprite.getX();
        if (padding < 0 || padding != y - sprite.getY()) {
            throw new IllegalArgumentException("Atlas dimensions do not match the sprite");
        }
        return new AtlasSpriteRegion(x, y, sprite.contents().width(), sprite.contents().height(), padding);
    }

    /** Extends the edge texels into the same border Minecraft allocates for filtering. */
    void copyPadding(byte[] pixels, int atlasWidth, int channels) {
        if (padding == 0) return;
        for (int row = y - padding; row < y + height + padding; row++) {
            int sourceRow = Math.clamp(row, y, y + height - 1);
            for (int column = x - padding; column < x + width + padding; column++) {
                if (row >= y && row < y + height && column >= x && column < x + width) continue;
                int sourceColumn = Math.clamp(column, x, x + width - 1);
                System.arraycopy(pixels, (sourceRow * atlasWidth + sourceColumn) * channels,
                        pixels, (row * atlasWidth + column) * channels, channels);
            }
        }
    }
}
