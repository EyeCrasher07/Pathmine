/* SPDX-License-Identifier: GPL-3.0-only */
package com.nvidia.rtxremix.minecraft.texture;

import com.mojang.blaze3d.platform.NativeImage;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.assertEquals;

class SpriteFrameInterpolationTest {
    @Test
    void blendsRgbAndCoverageFromTheSelectedFramesWithoutReadingAdjacentTiles() {
        try (NativeImage sheet = new NativeImage(4, 2, true);
             NativeImage result = new NativeImage(1, 1, true)) {
            sheet.fillRect(0, 0, 4, 2, 0xFFFFFFFF);
            sheet.setPixel(1, 1, 0x204080C0);
            sheet.setPixel(3, 0, 0xE0C04080);
            SpriteFrameInterpolation.copy(sheet, 1, 1, 3, 0, 0.25f, result);
            assertEquals(0x506070B0, result.getPixel(0, 0));
            SpriteFrameInterpolation.copy(sheet, 1, 1, 3, 0, 0.0f, result);
            assertEquals(0x204080C0, result.getPixel(0, 0));
            SpriteFrameInterpolation.copy(sheet, 1, 1, 3, 0, 1.0f, result);
            assertEquals(0xE0C04080, result.getPixel(0, 0));
        }
    }
}
