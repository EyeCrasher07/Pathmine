/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Lazar Nikolic
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.entity;

import com.mojang.blaze3d.platform.NativeImage;
import com.nvidia.rtxremix.minecraft.texture.LabPBRConverter;
import net.minecraft.client.renderer.texture.SpriteContents;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.client.resources.metadata.animation.FrameSize;
import net.minecraft.resources.Identifier;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import java.util.Arrays;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class RemixEntitySubmitterAtlasPbrTest {
    private static final int WIDTH = 16;
    private static final int HEIGHT = 12;
    private static final Identifier ATLAS = Identifier.withDefaultNamespace("textures/atlas/chest.png");

    @Test
    void ambientOcclusionRefreshesAlbedoAndScatteringFallbackBorders() {
        try (SpriteContents contents = new SpriteContents(Identifier.withDefaultNamespace("test"),
                new FrameSize(2, 2), new NativeImage(2, 2, true));
             NativeImage normal = new NativeImage(1, 1, true)) {
            TextureAtlasSprite sprite = new TextureAtlasSprite(ATLAS, contents, 8, 8, 1, 1, 1) {};
            normal.setPixel(0, 0, 0xFF808040);
            byte[] pixels = new byte[8 * 8 * 4];
            Arrays.fill(pixels, (byte) 200);
            RemixEntitySubmitter.AtlasPbrData data = new RemixEntitySubmitter.AtlasPbrData(8, 8, pixels);

            data.blit(sprite, normal, null, null);

            for (byte[] plane : List.of(data.pixels, data.sssAlbedoData)) {
                for (int y = 0; y < 8; y++) {
                    for (int x = 0; x < 8; x++) {
                        for (int channel = 0; channel < 4; channel++) {
                            int expected = x >= 1 && x <= 4 && y >= 1 && y <= 4 && channel < 3 ? 50 : 200;
                            assertEquals(expected, plane[(y * 8 + x) * 4 + channel] & 255);
                        }
                    }
                }
            }
        }
    }

    @ParameterizedTest
    @ValueSource(ints = {0, 1, 2})
    void everyPbrPlaneUsesTheAlbedoOriginAndExtendsTheConvertedEdges(int padding) {
        try (SpriteContents contents = new SpriteContents(Identifier.withDefaultNamespace("test"),
                new FrameSize(2, 2), new NativeImage(2, 2, true));
             NativeImage normal = new NativeImage(2, 2, true);
             NativeImage specular = new NativeImage(2, 2, true);
             NativeImage scattering = new NativeImage(2, 2, true)) {
            for (int y = 0; y < 2; y++) {
                for (int x = 0; x < 2; x++) {
                    int delta = y * 2 + x;
                    normal.setPixel(x, y, ((64 + delta) << 24) | 0x00808000 | (80 + delta * 20));
                    specular.setPixel(x, y, ((120 + delta) << 24) | ((40 + delta * 20) << 16)
                            | (240 << 8) | (90 + delta));
                    scattering.setPixel(x, y, 0xFF204080 + delta * 0x00010203);
                }
            }
            TextureAtlasSprite sprite = new TextureAtlasSprite(ATLAS, contents, WIDTH, HEIGHT, 4, 2, padding) {};
            int innerX = 4 + padding;
            int innerY = 2 + padding;
            byte[] pixels = new byte[WIDTH * HEIGHT * 4];
            for (int i = 0; i < pixels.length; i++) pixels[i] = (byte) (50 + i % 101);
            byte[] source = new byte[2 * 2 * 4];
            for (int y = 0; y < 2; y++) {
                System.arraycopy(pixels, ((innerY + y) * WIDTH + innerX) * 4, source, y * 8, 8);
            }
            RemixEntitySubmitter.AtlasPbrData data = new RemixEntitySubmitter.AtlasPbrData(WIDTH, HEIGHT, pixels);
            List<byte[]> planes = List.of(data.pixels, data.normalData, data.heightData, data.roughData,
                    data.metalData, data.emissiveData, data.sssData, data.sssAlbedoData);
            List<byte[]> before = planes.stream().map(byte[]::clone).toList();
            byte[] expectedAlbedo = source.clone();
            byte[] expectedNormal = new byte[16];
            byte[] expectedHeight = new byte[4];
            byte[] expectedRoughness = new byte[4];
            byte[] expectedMetallic = new byte[4];
            byte[] expectedEmissive = new byte[16];
            byte[] expectedSss = new byte[16];
            byte[] expectedScattering = new byte[16];
            LabPBRConverter.applyAO(expectedAlbedo, 2, normal, 0, 0, 2, 2);
            LabPBRConverter.blitNormal(expectedNormal, 2, normal, 0, 0, 2, 2);
            LabPBRConverter.blitHeight(expectedHeight, 2, normal, 0, 0, 2, 2);
            LabPBRConverter.blitRoughness(expectedRoughness, 2, specular, 0, 0, 2, 2);
            LabPBRConverter.blitMetallic(expectedMetallic, 2, specular, 0, 0, 2, 2);
            LabPBRConverter.blitEmissive(expectedEmissive, 2, expectedAlbedo, specular, 0, 0, 2, 2, 0);
            LabPBRConverter.blitSSS(expectedSss, 2, specular, 0, 0, 2, 2);
            LabPBRConverter.blitSingleScatteringAlbedo(expectedScattering, 2, scattering, 0, 0, 2, 2);
            List<byte[]> expected = List.of(expectedAlbedo, expectedNormal, expectedHeight, expectedRoughness,
                    expectedMetallic, expectedEmissive, expectedSss, expectedScattering);

            data.blit(sprite, normal, specular, scattering);

            assertTrue(data.hasNormal && data.hasSpecular && data.hasEmissive && data.hasSss);
            for (int plane = 0; plane < planes.size(); plane++) {
                int channels = expected.get(plane).length / 4;
                for (int y = 0; y < HEIGHT; y++) {
                    for (int x = 0; x < WIDTH; x++) {
                        boolean covered = x >= 4 && x < innerX + 2 + padding
                                && y >= 2 && y < innerY + 2 + padding;
                        int sample = (Math.clamp(y - innerY, 0, 1) * 2 + Math.clamp(x - innerX, 0, 1)) * channels;
                        int destination = (y * WIDTH + x) * channels;
                        for (int c = 0; c < channels; c++) {
                            byte value = covered ? expected.get(plane)[sample + c] : before.get(plane)[destination + c];
                            assertEquals(value, planes.get(plane)[destination + c],
                                    "plane " + plane + " pixel " + x + "," + y + " channel " + c);
                        }
                    }
                }
            }
        }
    }
}
