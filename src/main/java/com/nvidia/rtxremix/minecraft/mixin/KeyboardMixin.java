/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
package com.nvidia.rtxremix.minecraft.mixin;

import com.nvidia.rtxremix.minecraft.RtxRemixMod;
import com.nvidia.rtxremix.OverlayInputType;
import com.nvidia.rtxremix.minecraft.input.RemixInputGate;
import com.nvidia.rtxremix.minecraft.input.RemixKeyboardAccess;
import com.nvidia.rtxremix.minecraft.input.SdlOverlayInput;
import com.mojang.blaze3d.platform.InputConstants;
import net.minecraft.client.KeyboardHandler;
import net.minecraft.client.Minecraft;
import net.minecraft.client.input.CharacterEvent;
import net.minecraft.client.input.KeyEvent;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Routes keyboard input through the Remix overlay gate and intercepts the mod's raw shortcuts:
 * Alt+X without Control toggles the overlay, and while its capture session is open every key
 * and character is withheld from Minecraft. Plain R toggles camera recording once an in-game
 * command has armed it, and = and - scrub time of day while recording. Injecting into
 * {@code KeyboardHandler.keyPress} runs this at key-event time, so the modifier mask is read the
 * instant the key is pressed. Matched shortcuts cancel the event so vanilla does not also
 * handle it.
 */
@Mixin(KeyboardHandler.class)
public class KeyboardMixin implements RemixKeyboardAccess {
    @Shadow
    private long debugCrashKeyTime;

    @Shadow
    private long debugCrashKeyReportedTime;

    @Shadow
    private long debugCrashKeyReportedCount;

    @Shadow
    private boolean usedDebugKeyAsModifier;

    @Inject(method = "keyPress", at = @At("HEAD"), cancellable = true)
    private void rtxremix$onKey(long window, int action, KeyEvent event, CallbackInfo ci) {
        Minecraft client = Minecraft.getInstance();
        if (window == 0 || window != client.getWindow().handle()) {
            return;
        }
        int key = event.key();
        int mods = event.modifiers();
        RtxRemixMod.feedNativeOverlayInput(OverlayInputType.KEY,
                SdlOverlayInput.key(key), action == 0 ? 0 : 1, SdlOverlayInput.modifiers(mods));
        RemixInputGate.KeyDecision decision = RtxRemixMod.getRemixInputGate().onKey(
                key, action, event.hasAltDown(), event.hasControlDown());

        if (decision == RemixInputGate.KeyDecision.BLOCK) {
            ci.cancel();
            return;
        }
        if (decision == RemixInputGate.KeyDecision.TOGGLE_MENU) {
            ci.cancel();
            RtxRemixMod.toggleRemixMenu();
            return;
        }
        if (client.gui.screen() != null || (action != 1 && action != -1)) {
            return;
        }

        if (action == 1
                && key == InputConstants.KEY_R
                && RtxRemixMod.shouldHandlePlainPathRecordingShortcut()) {
            ci.cancel();
            RtxRemixMod.togglePathRecording();
        } else if (RtxRemixMod.handleCameraTimeShortcut(key, action, mods)) {
            ci.cancel();
        }
    }

    @Inject(method = "charTyped", at = @At("HEAD"), cancellable = true)
    private void rtxremix$onChar(long window, CharacterEvent event, CallbackInfo ci) {
        if (window == 0 || window != Minecraft.getInstance().getWindow().handle()) {
            return;
        }
        RtxRemixMod.feedNativeOverlayInput(OverlayInputType.CHARACTER, event.codepoint(), 0, 0);
        if (RtxRemixMod.getRemixInputGate().isCaptureSessionOpen()) {
            ci.cancel();
        }
    }

    @Override
    public void rtxremix$resetDebugKeyState() {
        debugCrashKeyTime = -1L;
        debugCrashKeyReportedTime = -1L;
        debugCrashKeyReportedCount = -1L;
        usedDebugKeyAsModifier = false;
    }
}
