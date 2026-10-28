/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 EyeCrasher.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix;

/** Events accepted by the native Linux overlay extension. */
public final class OverlayInputType {
    public static final int POINTER_POSITION = 0;
    public static final int MOUSE_BUTTON = 1;
    public static final int MOUSE_WHEEL = 2;
    public static final int KEY = 3;
    public static final int CHARACTER = 4;
    public static final int FOCUS = 5;

    private OverlayInputType() {}
}
