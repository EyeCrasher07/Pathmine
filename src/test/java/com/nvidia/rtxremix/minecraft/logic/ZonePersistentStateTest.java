/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.logic;

import net.minecraft.SharedConstants;
import net.minecraft.core.RegistryAccess;
import net.minecraft.nbt.CompoundTag;
import net.minecraft.nbt.ListTag;
import net.minecraft.nbt.NbtAccounter;
import net.minecraft.nbt.NbtIo;
import net.minecraft.nbt.NbtOps;
import net.minecraft.nbt.Tag;
import net.minecraft.server.Bootstrap;
import net.minecraft.util.datafix.DataFixers;
import net.minecraft.world.level.storage.SavedDataStorage;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ZonePersistentStateTest {

    @BeforeAll
    static void bootstrapSavedWorldTypes() {
        ensureGameVersionForPersistentStateSave();
        Bootstrap.bootStrap();
    }

    @Test
    void sphereRoundTripsThroughNbt() {
        ZoneDefinition zone = ZoneDefinition.parse(
                "rtx zone define cave --center 10.5 20 -30.25 --inner 8 --outer 24 --channel mc.zone.underground",
                0, 0, 0);

        ZonePersistentState loaded = roundTrip(zone);

        assertEquals(1, loaded.snapshot().size());
        assertEquals(zone, loaded.snapshot().iterator().next());
    }

    @Test
    void normalizedBoxRoundTripsThroughNbt() {
        ZoneDefinition zone = ZoneDefinition.parse(
                "rtx zone define hall --box 10 20 30 -5 4 6 --fade 12 --channel mc.zone.interior",
                0, 0, 0);

        ZoneDefinition restored = roundTrip(zone).snapshot().iterator().next();

        assertEquals(zone, restored);
        assertEquals(-5.0, restored.minX());
        assertEquals(10.0, restored.maxX());
        assertEquals(4.0, restored.minY());
        assertEquals(20.0, restored.maxY());
    }

    @Test
    void effectiveChangesAloneDirtyTheState() {
        ZonePersistentState state = new ZonePersistentState();
        ZoneDefinition first = ZoneDefinition.parse(
                "rtx zone define cave --center 0 0 0 --inner 5 --outer 10", 0, 0, 0);
        ZoneDefinition moved = ZoneDefinition.parse(
                "rtx zone define cave --center 100 0 0 --inner 5 --outer 10", 0, 0, 0);

        assertFalse(state.isDirty());
        assertTrue(state.put(first));
        assertTrue(state.isDirty());

        state.setDirty(false);
        assertFalse(state.put(first));
        assertFalse(state.isDirty(), "an identical redefinition must not dirty the save");

        assertTrue(state.put(moved));
        assertTrue(state.isDirty());

        state.setDirty(false);
        assertFalse(state.remove("missing"));
        assertFalse(state.isDirty());
        assertTrue(state.remove("cave"));
        assertTrue(state.isDirty());
    }

    @Test
    void malformedEntryDoesNotDiscardValidZones() {
        ZoneDefinition valid = ZoneDefinition.parse(
                "rtx zone define valid --center 1 2 3 --inner 4 --outer 8", 0, 0, 0);
        ZonePersistentState source = new ZonePersistentState();
        source.put(valid);
        CompoundTag nbt = source.toNbt();

        CompoundTag malformed = new CompoundTag();
        malformed.putString("name", "broken");
        malformed.putString("channel", "mc.zone.broken");
        malformed.putString("shape", "sphere");
        nbt.getList("zones").orElseThrow().add(malformed);

        ZonePersistentState loaded = ZonePersistentState.fromNbt(nbt);

        assertEquals(1, loaded.snapshot().size());
        assertEquals(valid, loaded.snapshot().iterator().next());
        assertThrows(IllegalStateException.class, () -> loaded.put(valid));
        assertThrows(IllegalStateException.class, () -> loaded.remove(valid.name()));
        assertFalse(loaded.isDirty());
    }

    @Test
    void unsupportedSchemaLoadsAsEmptyWithoutDirtying() {
        CompoundTag nbt = new CompoundTag();
        nbt.putInt("version", 999);
        nbt.put("zones", new ListTag());

        ZonePersistentState loaded = ZonePersistentState.fromNbt(nbt);

        assertTrue(loaded.snapshot().isEmpty());
        assertFalse(loaded.isDirty());
        assertThrows(IllegalStateException.class,
                () -> loaded.put(ZoneDefinition.parse("rtx zone define newer", 0, 0, 0)));
        assertThrows(IllegalStateException.class, () -> loaded.remove("anything"));
        assertFalse(loaded.isDirty(), "an older mod must never overwrite a newer schema");
    }

    @Test
    void duplicateNamesUseLastDefinition() {
        ZoneDefinition first = ZoneDefinition.parse(
                "rtx zone define cave --center 0 0 0 --inner 5 --outer 10", 0, 0, 0);
        ZoneDefinition second = ZoneDefinition.parse(
                "rtx zone define cave --center 100 0 0 --inner 5 --outer 10", 0, 0, 0);

        CompoundTag firstNbt = writeOne(first);
        CompoundTag secondNbt = writeOne(second);
        ListTag zones = new ListTag();
        zones.add(firstNbt);
        zones.add(secondNbt);
        CompoundTag root = new CompoundTag();
        root.putInt("version", 1);
        root.put("zones", zones);

        ZoneDefinition restored = ZonePersistentState.fromNbt(root).snapshot().iterator().next();

        assertEquals(second, restored);
        assertEquals(1.0f, restored.weight(100, 0, 0), 1e-6f);
    }

    @Test
    void snapshotIsImmutableAndNameSorted() {
        ZonePersistentState state = new ZonePersistentState();
        state.put(ZoneDefinition.parse("rtx zone define zed", 0, 0, 0));
        state.put(ZoneDefinition.parse("rtx zone define alpha", 0, 0, 0));

        Collection<ZoneDefinition> snapshot = state.snapshot();
        ArrayList<String> names = new ArrayList<>();
        snapshot.forEach(zone -> names.add(zone.name()));

        assertEquals(List.of("alpha", "zed"), names);
        assertThrows(UnsupportedOperationException.class, snapshot::clear);
    }

    @Test
    void writesMinecraftPersistentStateFile(@TempDir Path tempDir) throws Exception {
        ZoneDefinition zone = ZoneDefinition.parse(
                "rtx zone define saved --center 4 5 6 --inner 7 --outer 11", 0, 0, 0);
        ZonePersistentState state = new ZonePersistentState();
        state.put(zone);
        Path file = ZonePersistentState.TYPE.id().withSuffix(".dat").resolveAgainst(tempDir);

        ensureGameVersionForPersistentStateSave();
        try (SavedDataStorage storage = new SavedDataStorage(tempDir, DataFixers.getDataFixer(), RegistryAccess.EMPTY)) {
            storage.set(ZonePersistentState.TYPE, state);
            storage.saveAndJoin();
        }

        assertFalse(state.isDirty());
        CompoundTag root = NbtIo.readCompressed(file, NbtAccounter.unlimitedHeap());
        ZonePersistentState loaded = ZonePersistentState.TYPE.codec()
                .parse(NbtOps.INSTANCE, root.getCompound("data").orElseThrow()).getOrThrow();
        assertEquals(List.of(zone), new ArrayList<>(loaded.snapshot()));
        try (SavedDataStorage storage = new SavedDataStorage(tempDir, DataFixers.getDataFixer(), RegistryAccess.EMPTY)) {
            assertEquals(List.of(zone), new ArrayList<>(storage.get(ZonePersistentState.TYPE).snapshot()));
        }
    }

    @ParameterizedTest
    @ValueSource(booleans = {false, true})
    void legacyZonesImportThroughBothLookupsAndPersistWithoutChangingTheSource(
            boolean create, @TempDir Path worldRoot) throws Exception {
        ZoneDefinition sphere = ZoneDefinition.parse(
                "rtx zone define cave --center 10 20 30 --inner 4 --outer 9", 0, 0, 0);
        ZoneDefinition box = ZoneDefinition.parse(
                "rtx zone define room --box -5 4 6 10 20 30 --fade 12", 0, 0, 0);
        ZonePersistentState original = new ZonePersistentState();
        original.put(sphere);
        original.put(box);
        Path source = writeLegacy(worldRoot, original.toNbt());
        byte[] sourceBytes = Files.readAllBytes(source);
        Path unrelated = worldRoot.resolve("data/other_mod.dat");
        Files.writeString(unrelated, "unrelated saved data");
        Path target = currentZoneFile(worldRoot);
        assertFalse(Files.exists(target));

        try (SavedDataStorage storage = storage(worldRoot)) {
            ZonePersistentState imported = create ? ZonePersistentState.getOrCreate(storage, worldRoot)
                    : ZonePersistentState.getIfPresent(storage, worldRoot);
            assertEquals(List.of(sphere, box), new ArrayList<>(imported.snapshot()));
            assertTrue(imported.isDirty());
            assertSame(imported, ZonePersistentState.getIfPresent(storage, worldRoot));
            assertSame(imported, ZonePersistentState.getOrCreate(storage, worldRoot));
            storage.saveAndJoin();
            assertFalse(imported.isDirty());
        }

        assertTrue(Files.isRegularFile(target));
        assertArrayEquals(sourceBytes, Files.readAllBytes(source));
        assertEquals("unrelated saved data", Files.readString(unrelated));
        try (SavedDataStorage storage = storage(worldRoot)) {
            ZonePersistentState loaded = ZonePersistentState.getIfPresent(storage, worldRoot);
            assertEquals(List.of(sphere, box), new ArrayList<>(loaded.snapshot()));
            assertFalse(loaded.isDirty());
            assertTrue(loaded.remove(sphere.name()));
            assertTrue(loaded.remove(box.name()));
            storage.saveAndJoin();
        }
        try (SavedDataStorage storage = storage(worldRoot)) {
            assertTrue(ZonePersistentState.getOrCreate(storage, worldRoot).snapshot().isEmpty());
        }
        assertArrayEquals(sourceBytes, Files.readAllBytes(source));
    }

    @Test
    void existingTargetTakesPrecedenceEvenWhenTheLegacyFileIsUnreadable(@TempDir Path worldRoot) throws Exception {
        ZoneDefinition current = ZoneDefinition.parse("rtx zone define current", 0, 0, 0);
        try (SavedDataStorage storage = storage(worldRoot)) {
            ZonePersistentState.getOrCreate(storage, worldRoot).put(current);
            storage.saveAndJoin();
        }
        byte[] targetBytes = Files.readAllBytes(currentZoneFile(worldRoot));
        Path legacy = worldRoot.resolve("data/rtx_remix_zones.dat");
        Files.createDirectories(legacy.getParent());
        Files.writeString(legacy, "unreadable legacy NBT");

        try (SavedDataStorage storage = storage(worldRoot)) {
            assertEquals(List.of(current), new ArrayList<>(ZonePersistentState.getIfPresent(storage, worldRoot).snapshot()));
            assertEquals(List.of(current), new ArrayList<>(ZonePersistentState.getOrCreate(storage, worldRoot).snapshot()));
        }

        assertArrayEquals(targetBytes, Files.readAllBytes(currentZoneFile(worldRoot)));
        assertEquals("unreadable legacy NBT", Files.readString(legacy));
    }

    @Test
    void absentLegacyAndTargetLeaveAReadOnlyWorldUntouched(@TempDir Path worldRoot) throws Exception {
        Path unrelated = worldRoot.resolve("data/other_mod.dat");
        Files.createDirectories(unrelated.getParent());
        Files.writeString(unrelated, "unrelated");
        List<Path> originalPaths;
        try (var paths = Files.walk(worldRoot)) {
            originalPaths = paths.sorted().toList();
        }

        try (SavedDataStorage storage = storage(worldRoot)) {
            assertNull(ZonePersistentState.getIfPresent(storage, worldRoot));
            assertNull(ZonePersistentState.getIfPresent(storage, worldRoot));
        }

        try (var paths = Files.walk(worldRoot)) {
            assertEquals(originalPaths, paths.sorted().toList());
        }
        assertEquals("unrelated", Files.readString(unrelated));
    }

    @ParameterizedTest
    @ValueSource(strings = {"corrupt", "missing-data", "missing-version", "unsupported", "missing-zones", "wrong-zones", "bad-zone"})
    void invalidLegacyDataBlocksBothLookupsWithoutCreatingATarget(String failure, @TempDir Path worldRoot) throws Exception {
        ZonePersistentState original = new ZonePersistentState();
        original.put(ZoneDefinition.parse("rtx zone define valid", 0, 0, 0));
        CompoundTag data = original.toNbt();
        switch (failure) {
            case "missing-version" -> data.remove("version");
            case "unsupported" -> data.putInt("version", 999);
            case "missing-zones" -> data.remove("zones");
            case "wrong-zones" -> data.putString("zones", "invalid");
            case "bad-zone" -> data.getList("zones").orElseThrow().add(new CompoundTag());
        }
        Path legacy = writeLegacy(worldRoot, data);
        if (failure.equals("corrupt")) Files.writeString(legacy, "not compressed NBT");
        if (failure.equals("missing-data")) NbtIo.writeCompressed(new CompoundTag(), legacy);
        byte[] originalBytes = Files.readAllBytes(legacy);

        try (SavedDataStorage storage = storage(worldRoot)) {
            assertThrows(IllegalStateException.class, () -> ZonePersistentState.getIfPresent(storage, worldRoot));
            assertThrows(IllegalStateException.class, () -> ZonePersistentState.getOrCreate(storage, worldRoot));
            storage.saveAndJoin();
        }

        assertArrayEquals(originalBytes, Files.readAllBytes(legacy));
        assertFalse(Files.exists(currentZoneFile(worldRoot)));
    }

    @Test
    void corruptTargetCannotBeReplacedByLegacyOrNewState(@TempDir Path worldRoot) throws Exception {
        ZonePersistentState legacy = new ZonePersistentState();
        legacy.put(ZoneDefinition.parse("rtx zone define legacy", 0, 0, 0));
        Path source = writeLegacy(worldRoot, legacy.toNbt());
        byte[] sourceBytes = Files.readAllBytes(source);
        Path target = currentZoneFile(worldRoot);
        Files.createDirectories(target.getParent());
        Files.writeString(target, "corrupt target NBT");

        try (SavedDataStorage storage = storage(worldRoot)) {
            assertThrows(IllegalStateException.class, () -> ZonePersistentState.getIfPresent(storage, worldRoot));
            assertThrows(IllegalStateException.class, () -> ZonePersistentState.getOrCreate(storage, worldRoot));
            storage.saveAndJoin();
        }

        assertEquals("corrupt target NBT", Files.readString(target));
        assertArrayEquals(sourceBytes, Files.readAllBytes(source));
    }

    private static Path currentZoneFile(Path worldRoot) {
        return worldRoot.resolve("dimensions/minecraft/overworld/data/minecraft/rtx_remix_zones.dat");
    }

    private static SavedDataStorage storage(Path worldRoot) {
        ensureGameVersionForPersistentStateSave();
        return new SavedDataStorage(currentZoneFile(worldRoot).getParent().getParent(),
                DataFixers.getDataFixer(), RegistryAccess.EMPTY);
    }

    private static Path writeLegacy(Path worldRoot, CompoundTag data) throws Exception {
        Path file = worldRoot.resolve("data/rtx_remix_zones.dat");
        Files.createDirectories(file.getParent());
        CompoundTag root = new CompoundTag();
        root.put("data", data);
        root.putInt("DataVersion", 3955);
        NbtIo.writeCompressed(root, file);
        return file;
    }

    private static ZonePersistentState roundTrip(ZoneDefinition zone) {
        ZonePersistentState state = new ZonePersistentState();
        state.put(zone);
        Tag encoded = ZonePersistentState.TYPE.codec().encodeStart(NbtOps.INSTANCE, state).getOrThrow();
        return ZonePersistentState.TYPE.codec().parse(NbtOps.INSTANCE, encoded).getOrThrow();
    }

    private static CompoundTag writeOne(ZoneDefinition zone) {
        ZonePersistentState state = new ZonePersistentState();
        state.put(zone);
        return state.toNbt()
                .getList("zones").orElseThrow()
                .getCompound(0).orElseThrow()
                .copy();
    }

    private static void ensureGameVersionForPersistentStateSave() {
        try {
            SharedConstants.getCurrentVersion();
        } catch (IllegalStateException unset) {
            SharedConstants.tryDetectVersion();
        }
    }
}
