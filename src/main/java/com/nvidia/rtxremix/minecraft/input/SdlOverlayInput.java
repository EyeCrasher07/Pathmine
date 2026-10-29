/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 EyeCrasher.
 * SPDX-License-Identifier: GPL-3.0-only
 */
package com.nvidia.rtxremix.minecraft.input;

import static org.lwjgl.sdl.SDLKeycode.*;
import static org.lwjgl.sdl.SDLScancode.*;

/**
 * Translates SDL input into the existing native overlay protocol. The runtime's
 * key codes and modifier bits use GLFW conventions, independently of the
 * window library Minecraft uses. Text input is forwarded separately as Unicode.
 */
public final class SdlOverlayInput {
    private SdlOverlayInput() {}

    /** Returns the native overlay key, or -1 for an unsupported SDL scancode. */
    public static int key(int scancode) {
        if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) {
            return 'A' + scancode - SDL_SCANCODE_A;
        }
        if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9) {
            return '1' + scancode - SDL_SCANCODE_1;
        }
        if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12) {
            return 290 + scancode - SDL_SCANCODE_F1;
        }
        if (scancode >= SDL_SCANCODE_F13 && scancode <= SDL_SCANCODE_F24) {
            return 302 + scancode - SDL_SCANCODE_F13;
        }
        if (scancode >= SDL_SCANCODE_KP_1 && scancode <= SDL_SCANCODE_KP_9) {
            return 321 + scancode - SDL_SCANCODE_KP_1;
        }
        return switch (scancode) {
            case SDL_SCANCODE_0 -> '0';
            case SDL_SCANCODE_SPACE -> ' ';
            case SDL_SCANCODE_APOSTROPHE -> '\'';
            case SDL_SCANCODE_COMMA -> ',';
            case SDL_SCANCODE_MINUS -> '-';
            case SDL_SCANCODE_PERIOD -> '.';
            case SDL_SCANCODE_SLASH -> '/';
            case SDL_SCANCODE_SEMICOLON -> ';';
            case SDL_SCANCODE_EQUALS -> '=';
            case SDL_SCANCODE_LEFTBRACKET -> '[';
            case SDL_SCANCODE_BACKSLASH -> '\\';
            case SDL_SCANCODE_RIGHTBRACKET -> ']';
            case SDL_SCANCODE_GRAVE -> '`';
            case SDL_SCANCODE_ESCAPE -> 256;
            case SDL_SCANCODE_RETURN -> 257;
            case SDL_SCANCODE_TAB -> 258;
            case SDL_SCANCODE_BACKSPACE -> 259;
            case SDL_SCANCODE_INSERT -> 260;
            case SDL_SCANCODE_DELETE -> 261;
            case SDL_SCANCODE_RIGHT -> 262;
            case SDL_SCANCODE_LEFT -> 263;
            case SDL_SCANCODE_DOWN -> 264;
            case SDL_SCANCODE_UP -> 265;
            case SDL_SCANCODE_PAGEUP -> 266;
            case SDL_SCANCODE_PAGEDOWN -> 267;
            case SDL_SCANCODE_HOME -> 268;
            case SDL_SCANCODE_END -> 269;
            case SDL_SCANCODE_CAPSLOCK -> 280;
            case SDL_SCANCODE_SCROLLLOCK -> 281;
            case SDL_SCANCODE_NUMLOCKCLEAR -> 282;
            case SDL_SCANCODE_PRINTSCREEN -> 283;
            case SDL_SCANCODE_PAUSE -> 284;
            case SDL_SCANCODE_KP_0 -> 320;
            case SDL_SCANCODE_KP_PERIOD -> 330;
            case SDL_SCANCODE_KP_DIVIDE -> 331;
            case SDL_SCANCODE_KP_MULTIPLY -> 332;
            case SDL_SCANCODE_KP_MINUS -> 333;
            case SDL_SCANCODE_KP_PLUS -> 334;
            case SDL_SCANCODE_KP_ENTER -> 335;
            case SDL_SCANCODE_KP_EQUALS -> 336;
            case SDL_SCANCODE_LSHIFT -> 340;
            case SDL_SCANCODE_LCTRL -> 341;
            case SDL_SCANCODE_LALT -> 342;
            case SDL_SCANCODE_LGUI -> 343;
            case SDL_SCANCODE_RSHIFT -> 344;
            case SDL_SCANCODE_RCTRL -> 345;
            case SDL_SCANCODE_RALT -> 346;
            case SDL_SCANCODE_RGUI -> 347;
            case SDL_SCANCODE_APPLICATION, SDL_SCANCODE_MENU -> 348;
            default -> -1;
        };
    }

    /** Combines the left and right SDL modifiers into the native protocol bits. */
    public static int modifiers(int modifiers) {
        return ((modifiers & SDL_KMOD_SHIFT) != 0 ? 1 : 0)
                | ((modifiers & SDL_KMOD_CTRL) != 0 ? 2 : 0)
                | ((modifiers & SDL_KMOD_ALT) != 0 ? 4 : 0)
                | ((modifiers & SDL_KMOD_GUI) != 0 ? 8 : 0)
                | ((modifiers & SDL_KMOD_CAPS) != 0 ? 16 : 0)
                | ((modifiers & SDL_KMOD_NUM) != 0 ? 32 : 0);
    }

    /** SDL orders left/middle/right; the overlay orders left/right/middle. */
    public static int mouseButton(int button) {
        return switch (button) {
            case 1 -> 0;
            case 2 -> 2;
            case 3 -> 1;
            case 4 -> 3;
            case 5 -> 4;
            default -> -1;
        };
    }
}
