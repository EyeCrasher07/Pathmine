/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.terrain.visibility;

import net.minecraft.client.RotatingSectionStorage;
import net.minecraft.core.SectionPos;
import org.junit.jupiter.api.Test;
import java.util.Random;
import static org.junit.jupiter.api.Assertions.assertEquals;

class ChunkStorageTorusTest {
    @Test
    void enteringPlaneUpdatesMatchActualMinecraftStorage() {
        Storage reference = new Storage(2, -4, 19);
        Storage optimized = new Storage(2, -4, 19);
        ChunkStorageTorus.Workspace workspace = new ChunkStorageTorus.Workspace();
        int[][] movements = {
            {10, 3, -14}, {10, 3, -14}, {10, 18, -14},
            {11, 3, -14}, {10, 3, -14}, {9, 3, -15},
            {-2, 3, -7}, {0, 3, 0}, {-1, 3, -1},
            {1_875_000, 3, -1_875_000}, {-1_875_000, 3, 1_875_000}
        };
        for (int[] position : movements) {
            compareReposition(reference, optimized, workspace, SectionPos.of(position[0], position[1], position[2]));
        }
        Random random = new Random(0x26_03L);
        for (int i = 0; i < 500; i++) {
            compareReposition(reference, optimized, workspace,
                    SectionPos.of(random.nextInt(2000) - 1000, random.nextInt(24) - 4,
                            random.nextInt(2000) - 1000));
        }
    }

    @Test
    void renderDistanceThirtyTwoOnlyMovesEnteringPlanes() {
        Storage reference = new Storage(32, -4, 19);
        Storage optimized = new Storage(32, -4, 19);
        ChunkStorageTorus.Workspace workspace = new ChunkStorageTorus.Workspace();
        compareReposition(reference, optimized, workspace, SectionPos.of(0, 0, 0));
        assertEquals(1560, compareReposition(reference, optimized, workspace, SectionPos.of(1, 0, 0)));
        assertEquals(3096, compareReposition(reference, optimized, workspace, SectionPos.of(2, 0, 1)));
        assertEquals(0, compareReposition(reference, optimized, workspace, SectionPos.of(2, 16, 1)));
    }

    @Test
    void vanillaMovementBetweenOptimizedMovesDoesNotLeaveStaleWorkspaceState() {
        Storage reference = new Storage(2, -4, 3);
        Storage optimized = new Storage(2, -4, 3);
        ChunkStorageTorus.Workspace workspace = new ChunkStorageTorus.Workspace();
        compareReposition(reference, optimized, workspace, SectionPos.of(-2, 0, 7));
        SectionPos vanillaPosition = SectionPos.of(40, 0, -50);
        reference.vanilla.repositionCenter(vanillaPosition);
        optimized.vanilla.repositionCenter(vanillaPosition);
        compareReposition(reference, optimized, workspace, SectionPos.of(41, 0, -49));
    }

    private static int compareReposition(Storage reference, Storage optimized,
            ChunkStorageTorus.Workspace workspace, SectionPos position) {
        reference.clearMoves();
        optimized.clearMoves();
        reference.vanilla.repositionCenter(position);
        int moved = ChunkStorageTorus.reposition(optimized, workspace,
                optimized.diameter, optimized.height, optimized.diameter, optimized.minSectionY * 16,
                position.x(), position.z());
        int vanillaMoves = 0;
        for (int i = 0; i < reference.slots.length; i++) {
            assertEquals(reference.slots[i].sectionNode, optimized.slots[i].sectionNode,
                    "slot " + i + " after camera section " + position);
            assertEquals(reference.slots[i].moves, optimized.slots[i].moves,
                    "reset count for slot " + i + " after " + position);
            vanillaMoves += reference.slots[i].moves;
        }
        assertEquals(vanillaMoves, moved);
        return moved;
    }

    private static final class Slot implements RotatingSectionStorage.Value {
        private long sectionNode;
        private int moves;

        private Slot(long sectionNode) {
            this.sectionNode = sectionNode;
        }

        @Override
        public long getSectionNode() {
            return sectionNode;
        }

        @Override
        public void setSectionNode(long sectionNode) {
            this.sectionNode = sectionNode;
            moves++;
        }
    }

    private static final class Storage implements ChunkStorageTorus.StorageAccess {
        private final int diameter;
        private final int height;
        private final int minSectionY;
        private final Slot[] slots;
        private final RotatingSectionStorage<Slot> vanilla;

        private Storage(int radius, int minSectionY, int maxSectionY) {
            this.diameter = radius * 2 + 1;
            this.height = maxSectionY - minSectionY + 1;
            this.minSectionY = minSectionY;
            slots = new Slot[diameter * diameter * height];
            vanilla = new RotatingSectionStorage<>(radius, minSectionY, maxSectionY, (index, node) -> {
                Slot slot = new Slot(node);
                slots[index] = slot;
                return slot;
            });
        }

        private void clearMoves() {
            for (Slot slot : slots) {
                slot.moves = 0;
            }
        }

        @Override
        public int rtxremix$getOriginX(int slotIndex) {
            return SectionPos.sectionToBlockCoord(SectionPos.x(slots[slotIndex].sectionNode));
        }

        @Override
        public int rtxremix$getOriginZ(int slotIndex) {
            return SectionPos.sectionToBlockCoord(SectionPos.z(slots[slotIndex].sectionNode));
        }

        @Override
        public void rtxremix$setOrigin(int slotIndex, int x, int y, int z) {
            slots[slotIndex].setSectionNode(SectionPos.asLong(x >> 4, y >> 4, z >> 4));
        }
    }
}
