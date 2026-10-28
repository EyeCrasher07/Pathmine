/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix;

import java.util.Locale;

/** Native payload identities. An identity does not establish runtime availability. */
public enum RuntimePlatform {
    WINDOWS_X64("win64", "d3d9.dll", "remixbridge.dll"),
    WINDOWS_ARM64("arm64", "d3d9.dll", "remixbridge.dll"),
    LINUX_X64("linux64", "libremix.so", "libremixbridge.so");

    private final String target;
    private final String runtimeLibraryName;
    private final String bridgeLibraryName;

    RuntimePlatform(String target, String runtimeLibraryName, String bridgeLibraryName) {
        this.target = target;
        this.runtimeLibraryName = runtimeLibraryName;
        this.bridgeLibraryName = bridgeLibraryName;
    }

    public static RuntimePlatform current() {
        return detect(System.getProperty("os.name"), System.getProperty("os.arch"));
    }

    /** Rejects unknown platforms before selecting or extracting any native payload. */
    public static RuntimePlatform detect(String osName, String osArch) {
        String os = osName.toLowerCase(Locale.ROOT);
        String arch = osArch.toLowerCase(Locale.ROOT);
        boolean x64 = arch.equals("amd64") || arch.equals("x86_64");
        boolean arm64 = arch.equals("aarch64") || arch.equals("arm64");
        if (os.startsWith("windows")) {
            if (x64) return WINDOWS_X64;
            if (arm64) return WINDOWS_ARM64;
        } else if (os.equals("linux") && x64) {
            return LINUX_X64;
        }
        throw new UnsupportedOperationException(
                "No RTX Remix native target for " + osName + " / " + osArch);
    }

    public String target() {
        return target;
    }

    public String sdkDirectory() {
        return "remix-sdk-" + target;
    }

    public String runtimeLibraryName() {
        return runtimeLibraryName;
    }

    public String bridgeLibraryName() {
        return bridgeLibraryName;
    }

    public static RuntimePlatform forSdkDirectory(String directory) {
        for (RuntimePlatform platform : values()) {
            if (platform.sdkDirectory().equals(directory)) return platform;
        }
        throw new IllegalArgumentException("Unknown Remix SDK payload: " + directory);
    }
}
