/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.nvidia.rtxremix.minecraft.RtxRemixMod;
import com.nvidia.rtxremix.OverlayInputType;
import com.nvidia.rtxremix.minecraft.input.RemixMouseAccess;
import com.nvidia.rtxremix.minecraft.input.SdlOverlayInput;
import net.minecraft.client.Minecraft;
import net.minecraft.client.MouseHandler;
import net.minecraft.client.input.MouseButtonInfo;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Applies the shared Remix overlay input policy to Minecraft's mouse callbacks.
 *
 * The gate blocks Minecraft only while the overlay capture session owns input.
 */
@Mixin(MouseHandler.class)
public abstract class MouseMixin implements RemixMouseAccess {
    private static final double rtxremix$HIDDEN_CURSOR_COORDINATE = -Double.MAX_VALUE;

    @Shadow @Final
    private Minecraft minecraft;

    @Shadow
    private MouseButtonInfo activeButton;

    @Shadow
    public abstract void onButton(long window, MouseButtonInfo button, int action);

    private boolean rtxremix$releasingActiveButton;

    @Override
    public void rtxremix$releaseActiveButton() {
        if (activeButton == null) {
            return;
        }

        rtxremix$releasingActiveButton = true;
        try {
            onButton(minecraft.getWindow().handle(), new MouseButtonInfo(activeButton.button(), 0), 0);
        } finally {
            rtxremix$releasingActiveButton = false;
        }
    }

    @Shadow
    private double xpos;

    @Shadow
    private double ypos;

    @Shadow
    private boolean ignoreFirstMove;

    @Inject(method = "onMove", at = @At("HEAD"), cancellable = true)
    private void rtxremix$blockMovementWhileMenuOpen(
            long window, double cursorX, double cursorY, double relativeX, double relativeY, CallbackInfo ci) {
        if (window == 0 || window != minecraft.getWindow().handle()) {
            return;
        }
        if (RtxRemixMod.getRemixInputGate().isCaptureSessionOpen()) {
            RtxRemixMod.feedNativeOverlayInput(OverlayInputType.POINTER_POSITION, 0, cursorX, cursorY);
            xpos = cursorX;
            ypos = cursorY;
            ignoreFirstMove = false;
            ci.cancel();
        }
    }

    /* Screen rendering derives hover state from these coordinates even without a movement event. */
    @Inject(method = "xpos", at = @At("HEAD"), cancellable = true)
    private void rtxremix$hideXWhileMenuOpen(CallbackInfoReturnable<Double> cir) {
        if (RtxRemixMod.getRemixInputGate().isCaptureSessionOpen()) {
            cir.setReturnValue(rtxremix$HIDDEN_CURSOR_COORDINATE);
        }
    }

    @Inject(method = "ypos", at = @At("HEAD"), cancellable = true)
    private void rtxremix$hideYWhileMenuOpen(CallbackInfoReturnable<Double> cir) {
        if (RtxRemixMod.getRemixInputGate().isCaptureSessionOpen()) {
            cir.setReturnValue(rtxremix$HIDDEN_CURSOR_COORDINATE);
        }
    }

    @Inject(method = "grabMouse", at = @At("HEAD"), cancellable = true)
    private void rtxremix$blockLockWhileMenuOpen(CallbackInfo ci) {
        if (RtxRemixMod.getRemixInputGate().isCaptureSessionOpen()) {
            ci.cancel();
        }
    }

    @Inject(method = "onButton", at = @At("HEAD"), cancellable = true)
    private void rtxremix$blockClickWhileMenuOpen(long window, MouseButtonInfo button, int action, CallbackInfo ci) {
        if (window == 0 || window != minecraft.getWindow().handle()) {
            return;
        }
        boolean syntheticRelease = rtxremix$releasingActiveButton
                && action == 0
                && activeButton != null
                && button.button() == activeButton.button();
        if (RtxRemixMod.getRemixInputGate().isCaptureSessionOpen() && !syntheticRelease) {
            RtxRemixMod.feedNativeOverlayInput(OverlayInputType.MOUSE_BUTTON,
                    SdlOverlayInput.mouseButton(button.button()), action == 0 ? 0 : 1,
                    SdlOverlayInput.modifiers(button.modifiers()));
            ci.cancel();
        }
    }

    @Inject(method = "onScroll", at = @At("HEAD"), cancellable = true)
    private void rtxremix$blockScrollWhileMenuOpen(long window, double horizontal, double vertical, CallbackInfo ci) {
        if (window == 0 || window != minecraft.getWindow().handle()) {
            return;
        }
        if (RtxRemixMod.getRemixInputGate().isCaptureSessionOpen()) {
            RtxRemixMod.feedNativeOverlayInput(OverlayInputType.MOUSE_WHEEL, 0, horizontal, vertical);
            ci.cancel();
        }
    }
}
