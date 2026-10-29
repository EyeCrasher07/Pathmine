/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.terrain;

import it.unimi.dsi.fastutil.longs.LongSet;

/** Render-thread access to the section dirtiness owned by the level extractor. */
public interface SectionUpdateAccess {
    boolean rtxremix$isSectionDirty(long sectionNode);

    void rtxremix$setSectionNotDirty(long sectionNode);

    /** Keeps recapture off section slots that the next extraction will replace. */
    boolean rtxremix$deferTerrainRecapture(LongSet sections);
}
