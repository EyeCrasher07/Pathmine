/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 EyeCrasher.
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.input;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.lwjgl.sdl.SDLKeycode.*;
import static org.lwjgl.sdl.SDLScancode.*;

class SdlOverlayInputTest {
    @Test
    void physicalLettersAndDigitsUseTheNativeOverlayProtocol() {
        for (int index = 0; index < 26; index++) {
            assertEquals('A' + index, SdlOverlayInput.key(SDL_SCANCODE_A + index));
        }
        assertEquals('0', SdlOverlayInput.key(SDL_SCANCODE_0));
        assertEquals('1', SdlOverlayInput.key(SDL_SCANCODE_1));
        assertEquals('9', SdlOverlayInput.key(SDL_SCANCODE_9));
        assertEquals('=', SdlOverlayInput.key(SDL_SCANCODE_EQUALS));
        assertEquals('-', SdlOverlayInput.key(SDL_SCANCODE_MINUS));
    }

    @Test
    void navigationAndKeypadStayDistinctFromPrintableText() {
        assertEquals(256, SdlOverlayInput.key(SDL_SCANCODE_ESCAPE));
        assertEquals(257, SdlOverlayInput.key(SDL_SCANCODE_RETURN));
        assertEquals(258, SdlOverlayInput.key(SDL_SCANCODE_TAB));
        assertEquals(259, SdlOverlayInput.key(SDL_SCANCODE_BACKSPACE));
        assertEquals(262, SdlOverlayInput.key(SDL_SCANCODE_RIGHT));
        assertEquals(263, SdlOverlayInput.key(SDL_SCANCODE_LEFT));
        assertEquals(290, SdlOverlayInput.key(SDL_SCANCODE_F1));
        assertEquals(301, SdlOverlayInput.key(SDL_SCANCODE_F12));
        assertEquals(302, SdlOverlayInput.key(SDL_SCANCODE_F13));
        assertEquals(313, SdlOverlayInput.key(SDL_SCANCODE_F24));
        assertEquals(320, SdlOverlayInput.key(SDL_SCANCODE_KP_0));
        assertEquals(329, SdlOverlayInput.key(SDL_SCANCODE_KP_9));
        assertEquals(335, SdlOverlayInput.key(SDL_SCANCODE_KP_ENTER));
        assertEquals(-1, SdlOverlayInput.key(SDL_SCANCODE_UNKNOWN));
        assertEquals(-1, SdlOverlayInput.key(-1));
        assertEquals(-1, SdlOverlayInput.key(Integer.MAX_VALUE));
    }

    @Test
    void eitherSideOfEachModifierSetsTheSameProtocolBit() {
        assertEquals(1, SdlOverlayInput.modifiers(SDL_KMOD_LSHIFT));
        assertEquals(1, SdlOverlayInput.modifiers(SDL_KMOD_RSHIFT));
        assertEquals(2, SdlOverlayInput.modifiers(SDL_KMOD_LCTRL));
        assertEquals(2, SdlOverlayInput.modifiers(SDL_KMOD_RCTRL));
        assertEquals(4, SdlOverlayInput.modifiers(SDL_KMOD_LALT));
        assertEquals(4, SdlOverlayInput.modifiers(SDL_KMOD_RALT));
        assertEquals(8, SdlOverlayInput.modifiers(SDL_KMOD_LGUI));
        assertEquals(8, SdlOverlayInput.modifiers(SDL_KMOD_RGUI));
        assertEquals(6, SdlOverlayInput.modifiers(SDL_KMOD_LCTRL | SDL_KMOD_RALT));
        assertEquals(63, SdlOverlayInput.modifiers(SDL_KMOD_CTRL | SDL_KMOD_SHIFT
                | SDL_KMOD_ALT | SDL_KMOD_GUI | SDL_KMOD_CAPS | SDL_KMOD_NUM));
        assertEquals(0, SdlOverlayInput.modifiers(SDL_KMOD_LEVEL5 | SDL_KMOD_SCROLL));
    }

    @Test
    void mouseButtonsPreserveLeftRightMiddleAndExtraButtons() {
        assertEquals(0, SdlOverlayInput.mouseButton(1));
        assertEquals(1, SdlOverlayInput.mouseButton(3));
        assertEquals(2, SdlOverlayInput.mouseButton(2));
        assertEquals(3, SdlOverlayInput.mouseButton(4));
        assertEquals(4, SdlOverlayInput.mouseButton(5));
        assertEquals(-1, SdlOverlayInput.mouseButton(0));
        assertEquals(-1, SdlOverlayInput.mouseButton(6));
    }
}
