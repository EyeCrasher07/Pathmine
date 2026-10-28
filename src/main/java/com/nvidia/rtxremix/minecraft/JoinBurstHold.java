/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Keeps vanilla's terrain-download screen up until the world-join frame burst has settled.
 *
 * <p>The first frames after a join pay Remix's first-sight costs: entity mesh creation,
 * pipeline compiles, first BLAS builds, deferred task flushes. Vanilla closes
 * {@code DownloadingTerrainScreen} as soon as the player's own section has built, which is
 * while that burst is under way, so the player feels the hitches right as they gain control.
 * {@code DownloadingTerrainScreenMixin} begins a hold at vanilla's first close attempt and
 * keeps cancelling the close while {@link #keepScreen(long)} says so.
 *
 * <p>The hold ends after shader compilation and {@link #SETTLE_STREAK} consecutive frames
 * shorter than {@link #SLOW_FRAME_NANOS}. The burst has a {@link #MAX_HOLD_NANOS} limit,
 * with a separate {@link #MAX_SHADER_HOLD_NANOS} limit while the compiler is working. The
 * frame threshold sits above steady frame times on slow hardware and below the burst's own
 * frames, which measure 130 to 630 ms: a threshold near the steady frame time would send
 * every join on a slow machine to the cap.
 *
 * <p>Frames are sampled where the renderer submits and the screen asks from the client tick,
 * both on the render thread, so the state needs no synchronization. Kill switch:
 * {@code -Drtxremix.joinHold=false}.
 */
public final class JoinBurstHold {
    private static final Logger LOGGER = LoggerFactory.getLogger(JoinBurstHold.class);

    public static final String PROPERTY = "rtxremix.joinHold";
    /** Read once: the switch is a launch decision, not a per-join one. */
    public static final boolean ENABLED = Boolean.parseBoolean(System.getProperty(PROPERTY, "true"));

    /** A frame at least this long belongs to the burst. */
    static final long SLOW_FRAME_NANOS = 100_000_000L;
    /** Consecutive fast frames that end the hold. */
    static final int SETTLE_STREAK = 8;
    /** Maximum settling time after the last observed shader work. */
    static final long MAX_HOLD_NANOS = 6_000_000_000L;
    /** Bound for a cold shader compilation before returning control to the player. */
    static final long MAX_SHADER_HOLD_NANOS = 120_000_000_000L;

    private final boolean enabled;
    private boolean holding;
    private long holdStartNanos;
    private long settleStartNanos;
    private long lastFrameNanos;
    private int fastStreak;
    private boolean shadersCompiling;

    public JoinBurstHold(boolean enabled) {
        this.enabled = enabled;
    }

    /** Starts a hold at {@code nowNanos}; the screen calls this on vanilla's first close attempt. */
    public void begin(long nowNanos) {
        if (!enabled) {
            return;
        }
        holding = true;
        holdStartNanos = nowNanos;
        settleStartNanos = nowNanos;
        lastFrameNanos = 0L;
        fastStreak = 0;
        shadersCompiling = false;
    }

    /** Whether the renderer still needs to sample join readiness. */
    public boolean isHolding() {
        return holding;
    }

    /** Records frame timing and actual pending shader work on the render thread. */
    public void onFrame(long nowNanos, boolean compilingShaders) {
        if (!holding) {
            return;
        }
        shadersCompiling = compilingShaders;
        if (compilingShaders) {
            settleStartNanos = nowNanos;
            fastStreak = 0;
            lastFrameNanos = nowNanos;
            return;
        }
        if (lastFrameNanos != 0L) {
            fastStreak = nowNanos - lastFrameNanos < SLOW_FRAME_NANOS ? fastStreak + 1 : 0;
            if (fastStreak >= SETTLE_STREAK) {
                holding = false;
                LOGGER.info("World-join burst settled after {} ms ({} consecutive frames under {} ms)",
                        (nowNanos - holdStartNanos) / 1_000_000L, SETTLE_STREAK, SLOW_FRAME_NANOS / 1_000_000L);
            }
        }
        lastFrameNanos = nowNanos;
    }

    /**
     * Decides vanilla's close attempt at {@code nowNanos}.
     *
     * @return true to cancel the close and keep the screen up
     */
    public boolean keepScreen(long nowNanos) {
        if (holding && nowNanos - holdStartNanos >= MAX_SHADER_HOLD_NANOS) {
            holding = false;
            LOGGER.warn("World-join shader wait exceeded {} s; releasing the terrain-download screen",
                    MAX_SHADER_HOLD_NANOS / 1_000_000_000L);
        } else if (holding && !shadersCompiling && nowNanos - settleStartNanos >= MAX_HOLD_NANOS) {
            holding = false;
            LOGGER.info("World-join burst did not settle within {} s; releasing the terrain-download screen",
                    MAX_HOLD_NANOS / 1_000_000_000L);
        }
        return holding;
    }
}
