/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.terrain.capture;

import java.util.function.Consumer;
import java.util.function.Predicate;
import net.fabricmc.fabric.api.client.renderer.v1.Renderer;
import net.fabricmc.fabric.api.client.renderer.v1.mesh.MutableQuadView;
import net.fabricmc.fabric.api.client.renderer.v1.mesh.QuadEmitter;
import net.minecraft.client.renderer.block.BlockAndTintGetter;
import net.minecraft.client.renderer.block.dispatch.BlockStateModel;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.util.RandomSource;
import net.minecraft.world.level.block.state.BlockState;

/**
 * Reuses Fabric's quad emitter for model capture, including custom model transforms.
 * The renderer owns the emitter implementation; emitted views are consumed before
 * the callback returns and are never retained by the capture path.
 */
final class RemixRenderContext {
    private final Consumer<? super MutableQuadView> output;
    private QuadEmitter emitter;

    RemixRenderContext(Consumer<? super MutableQuadView> output) {
        this.output = output;
    }

    void emit(BlockStateModel model, BlockAndTintGetter world, BlockPos pos,
              BlockState state, RandomSource random, Predicate<Direction> cullTest) {
        if (emitter == null) emitter = Renderer.get().quadEmitter(output);
        emitter.clear();
        try {
            model.emitQuads(emitter, world, pos, state, random, cullTest);
        } catch (RuntimeException | Error failure) {
            // A failed model may leave transforms pushed. Do not reuse its emitter
            // on the next section built by this worker.
            emitter = null;
            throw failure;
        }
    }
}
