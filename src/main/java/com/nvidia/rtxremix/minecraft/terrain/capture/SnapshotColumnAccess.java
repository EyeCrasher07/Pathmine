/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.terrain.capture;

/** Whether a section copy came from a loaded column, including loaded air sections. */
public interface SnapshotColumnAccess {
    boolean rtxremix$columnWasLoaded();
}
