/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.terrain.capture;

/** Immutable neighbor-column presence in the region's actual copied sections. */
public interface RegionNeighbourhood {
    int rtxremix$snapshotMask();
}
