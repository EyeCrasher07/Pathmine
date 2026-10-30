/* SPDX-License-Identifier: GPL-3.0-only */
package com.nvidia.rtxremix.minecraft.texture;

import net.minecraft.client.renderer.texture.TextureAtlasSprite;

/** Associates the GPU animation state with the sprite's destination rectangle. */
public interface SpriteAnimationCapture {
    void rtxremix$attachSprite(TextureAtlasSprite sprite, int padding);
}
