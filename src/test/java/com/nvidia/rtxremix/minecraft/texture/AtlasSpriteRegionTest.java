/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Lazar Nikolic
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.texture;

import com.mojang.blaze3d.platform.NativeImage;
import java.util.Arrays;
import net.minecraft.client.renderer.texture.SpriteContents;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.client.resources.metadata.animation.FrameSize;
import net.minecraft.resources.Identifier;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import static org.junit.jupiter.api.Assertions.*;

class AtlasSpriteRegionTest {
    @Test
    void spriteUvsAddressTheInnerPixelsOfANonSquareAtlas() {
        try (SpriteContents contents = new SpriteContents(Identifier.withDefaultNamespace("test"),
                new FrameSize(4, 2), new NativeImage(4, 2, true))) {
            TextureAtlasSprite sprite = new TextureAtlasSprite(
                    Identifier.withDefaultNamespace("textures/atlas/blocks.png"), contents,
                    64, 32, 10, 5, 3) {};
            assertEquals(new AtlasSpriteRegion(13, 8, 4, 2, 3), AtlasSpriteRegion.of(sprite, 64, 32));
        }
    }

    @ParameterizedTest
    @ValueSource(ints = {1, 4})
    void borderCopiesTheNearestInnerTexelAndLeavesOtherSpritesAlone(int channels) {
        byte[] image = new byte[8 * 6 * channels];
        Arrays.fill(image, (byte) 0x7F);
        for (int row = 2; row < 4; row++) {
            for (int x = 3; x < 5; x++) {
                for (int c = 0; c < channels; c++) {
                    image[(row * 8 + x) * channels + c] = (byte) (row * 20 + x + c);
                }
            }
        }
        byte[] original = image.clone();
        new AtlasSpriteRegion(3, 2, 2, 2, 1).copyPadding(image, 8, channels);
        for (int row = 0; row < 6; row++) {
            for (int x = 0; x < 8; x++) {
                for (int c = 0; c < channels; c++) {
                    int expected = row >= 1 && row <= 4 && x >= 2 && x <= 5
                            ? original[(Math.clamp(row, 2, 3) * 8 + Math.clamp(x, 3, 4)) * channels + c]
                            : original[(row * 8 + x) * channels + c];
                    assertEquals(expected, image[(row * 8 + x) * channels + c],
                            "pixel " + x + "," + row + " channel " + c);
                }
            }
        }
    }
}
