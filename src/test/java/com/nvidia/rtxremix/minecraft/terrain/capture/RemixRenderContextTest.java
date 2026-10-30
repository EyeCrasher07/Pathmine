/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Lazar Nikolic
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.terrain.capture;

import java.util.ArrayList;
import java.util.List;
import java.util.function.BiConsumer;
import java.util.function.Predicate;
import net.fabricmc.fabric.api.client.renderer.v1.Renderer;
import net.fabricmc.fabric.api.client.renderer.v1.mesh.MutableQuadView;
import net.fabricmc.fabric.api.client.renderer.v1.mesh.QuadEmitter;
import net.fabricmc.fabric.impl.client.indigo.renderer.IndigoRenderer;
import net.minecraft.client.renderer.block.BlockAndTintGetter;
import net.minecraft.client.renderer.block.dispatch.BlockStateModel;
import net.minecraft.client.renderer.block.dispatch.BlockStateModelPart;
import net.minecraft.client.resources.model.sprite.Material;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.util.RandomSource;
import net.minecraft.world.level.block.state.BlockState;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

class RemixRenderContextTest {
    @BeforeAll
    static void useFabricDefaultRenderer() {
        try {
            Renderer.get();
        } catch (UnsupportedOperationException notInitialized) {
            Renderer.register(IndigoRenderer.INSTANCE);
        }
    }

    @Test
    void emittedQuadsIncludeCustomModelTransformsAndKeepArgb() {
        List<float[]> positions = new ArrayList<>();
        List<Integer> colors = new ArrayList<>();
        RemixRenderContext capture = new RemixRenderContext(quad -> {
            positions.add(new float[]{quad.x(0), quad.y(0), quad.z(0), quad.u(0), quad.v(0)});
            colors.add(quad.color(0));
        });
        BlockStateModel model = model((emitter, random) -> {
            emitter.pushTransform(quad -> {
                quad.translate(2, 3, 4);
                return true;
            });
            emitter.square(Direction.SOUTH, 0, 0, 1, 1, 0)
                    .uv(0, 0.125f, 0.75f).color(0, 0x7F123456).emit();
            emitter.popTransform();
        });

        capture.emit(model, null, BlockPos.ZERO, null, RandomSource.create(4), face -> false);

        assertEquals(1, positions.size());
        assertArrayEquals(new float[]{2, 4, 5, 0.125f, 0.75f}, positions.getFirst());
        assertEquals(0x7F123456, colors.getFirst());
    }

    @Test
    void repeatedModelsReuseTheEmitterWithoutKeepingPreviousQuadAttributes() {
        List<QuadEmitter> emitters = new ArrayList<>();
        List<Integer> colors = new ArrayList<>();
        RemixRenderContext capture = new RemixRenderContext(quad -> colors.add(quad.color(0)));
        BlockStateModel colored = model((emitter, random) -> {
            emitters.add(emitter);
            emitter.square(Direction.UP, 0, 0, 1, 1, 0).color(0, 0xFF120034).emit();
        });
        BlockStateModel plain = model((emitter, random) -> {
            emitters.add(emitter);
            emitter.square(Direction.UP, 0, 0, 1, 1, 0).emit();
        });

        capture.emit(colored, null, BlockPos.ZERO, null, RandomSource.create(1), face -> false);
        capture.emit(plain, null, BlockPos.ZERO, null, RandomSource.create(1), face -> false);

        assertSame(emitters.get(0), emitters.get(1));
        assertEquals(List.of(0xFF120034, 0xFFFFFFFF), colors);
    }

    @Test
    void failedModelCannotLeaveItsTransformOnTheNextCapture() {
        List<QuadEmitter> emitters = new ArrayList<>();
        List<Float> heights = new ArrayList<>();
        RemixRenderContext capture = new RemixRenderContext(quad -> heights.add(quad.y(0)));
        IllegalStateException failure = new IllegalStateException("model failed");
        BlockStateModel broken = model((emitter, random) -> {
            emitters.add(emitter);
            emitter.pushTransform(quad -> { quad.translate(0, 100, 0); return true; });
            throw failure;
        });
        assertSame(failure, assertThrows(IllegalStateException.class,
                () -> capture.emit(broken, null, BlockPos.ZERO, null, RandomSource.create(1), face -> false)));
        capture.emit(model((emitter, random) -> {
            emitters.add(emitter);
            emitter.square(Direction.UP, 0, 0, 1, 1, 0).emit();
        }), null, BlockPos.ZERO, null, RandomSource.create(1), face -> false);

        assertNotSame(emitters.get(0), emitters.get(1));
        assertEquals(List.of(1f), heights);
    }

    private static BlockStateModel model(BiConsumer<QuadEmitter, RandomSource> emission) {
        return new BlockStateModel() {
            @Override
            public void collectParts(RandomSource random, List<BlockStateModelPart> output) {
                fail("Capture must use Fabric's model emission entry point");
            }

            @Override
            public Material.Baked particleMaterial() { return null; }

            @Override
            public int materialFlags() { return 0; }

            @Override
            public void emitQuads(QuadEmitter emitter, BlockAndTintGetter world, BlockPos pos,
                                  BlockState state, RandomSource random, Predicate<Direction> cullTest) {
                emission.accept(emitter, random);
            }
        };
    }
}
