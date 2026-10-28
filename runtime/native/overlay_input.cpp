// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "dxvk/imgui/dxvk_imgui.h"

namespace dxvk {
  namespace {
    ImGuiKey keyFromGlfw(int key) {
      if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
        return ImGuiKey(ImGuiKey_A + key - GLFW_KEY_A);
      }
      if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
        return ImGuiKey(ImGuiKey_0 + key - GLFW_KEY_0);
      }
      if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) {
        return ImGuiKey(ImGuiKey_F1 + key - GLFW_KEY_F1);
      }
      if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9) {
        return ImGuiKey(ImGuiKey_Keypad0 + key - GLFW_KEY_KP_0);
      }
      switch (key) {
      case GLFW_KEY_TAB: return ImGuiKey_Tab;
      case GLFW_KEY_LEFT: return ImGuiKey_LeftArrow;
      case GLFW_KEY_RIGHT: return ImGuiKey_RightArrow;
      case GLFW_KEY_UP: return ImGuiKey_UpArrow;
      case GLFW_KEY_DOWN: return ImGuiKey_DownArrow;
      case GLFW_KEY_PAGE_UP: return ImGuiKey_PageUp;
      case GLFW_KEY_PAGE_DOWN: return ImGuiKey_PageDown;
      case GLFW_KEY_HOME: return ImGuiKey_Home;
      case GLFW_KEY_END: return ImGuiKey_End;
      case GLFW_KEY_INSERT: return ImGuiKey_Insert;
      case GLFW_KEY_DELETE: return ImGuiKey_Delete;
      case GLFW_KEY_BACKSPACE: return ImGuiKey_Backspace;
      case GLFW_KEY_SPACE: return ImGuiKey_Space;
      case GLFW_KEY_ENTER: return ImGuiKey_Enter;
      case GLFW_KEY_ESCAPE: return ImGuiKey_Escape;
      case GLFW_KEY_APOSTROPHE: return ImGuiKey_Apostrophe;
      case GLFW_KEY_COMMA: return ImGuiKey_Comma;
      case GLFW_KEY_MINUS: return ImGuiKey_Minus;
      case GLFW_KEY_PERIOD: return ImGuiKey_Period;
      case GLFW_KEY_SLASH: return ImGuiKey_Slash;
      case GLFW_KEY_SEMICOLON: return ImGuiKey_Semicolon;
      case GLFW_KEY_EQUAL: return ImGuiKey_Equal;
      case GLFW_KEY_LEFT_BRACKET: return ImGuiKey_LeftBracket;
      case GLFW_KEY_BACKSLASH: return ImGuiKey_Backslash;
      case GLFW_KEY_RIGHT_BRACKET: return ImGuiKey_RightBracket;
      case GLFW_KEY_GRAVE_ACCENT: return ImGuiKey_GraveAccent;
      case GLFW_KEY_CAPS_LOCK: return ImGuiKey_CapsLock;
      case GLFW_KEY_SCROLL_LOCK: return ImGuiKey_ScrollLock;
      case GLFW_KEY_NUM_LOCK: return ImGuiKey_NumLock;
      case GLFW_KEY_PRINT_SCREEN: return ImGuiKey_PrintScreen;
      case GLFW_KEY_PAUSE: return ImGuiKey_Pause;
      case GLFW_KEY_KP_DECIMAL: return ImGuiKey_KeypadDecimal;
      case GLFW_KEY_KP_DIVIDE: return ImGuiKey_KeypadDivide;
      case GLFW_KEY_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
      case GLFW_KEY_KP_SUBTRACT: return ImGuiKey_KeypadSubtract;
      case GLFW_KEY_KP_ADD: return ImGuiKey_KeypadAdd;
      case GLFW_KEY_KP_ENTER: return ImGuiKey_KeypadEnter;
      case GLFW_KEY_KP_EQUAL: return ImGuiKey_KeypadEqual;
      case GLFW_KEY_LEFT_CONTROL: return ImGuiKey_LeftCtrl;
      case GLFW_KEY_LEFT_SHIFT: return ImGuiKey_LeftShift;
      case GLFW_KEY_LEFT_ALT: return ImGuiKey_LeftAlt;
      case GLFW_KEY_LEFT_SUPER: return ImGuiKey_LeftSuper;
      case GLFW_KEY_RIGHT_CONTROL: return ImGuiKey_RightCtrl;
      case GLFW_KEY_RIGHT_SHIFT: return ImGuiKey_RightShift;
      case GLFW_KEY_RIGHT_ALT: return ImGuiKey_RightAlt;
      case GLFW_KEY_RIGHT_SUPER: return ImGuiKey_RightSuper;
      case GLFW_KEY_MENU: return ImGuiKey_Menu;
      default: return ImGuiKey_None;
      }
    }

    void modifiers(ImGuiIO& io, int mods) {
      io.AddKeyEvent(ImGuiKey_ModCtrl, mods & GLFW_MOD_CONTROL);
      io.AddKeyEvent(ImGuiKey_ModShift, mods & GLFW_MOD_SHIFT);
      io.AddKeyEvent(ImGuiKey_ModAlt, mods & GLFW_MOD_ALT);
      io.AddKeyEvent(ImGuiKey_ModSuper, mods & GLFW_MOD_SUPER);
    }
  }

  void ImGUI::nativeInput(uint32_t type, int32_t code, double x, double y) {
    ImGui::SetCurrentContext(m_context);
    auto& io = ImGui::GetIO();
    switch (type) {
    case 0: io.AddMousePosEvent(float(x), float(y)); break;
    case 1:
      modifiers(io, int(y));
      if (code >= 0 && code < IM_ARRAYSIZE(io.MouseDown)) {
        io.AddMouseButtonEvent(code, bool(x));
      }
      break;
    case 2: io.AddMouseWheelEvent(float(x), float(y)); break;
    case 3: {
      modifiers(io, int(y));
      const auto key = keyFromGlfw(code);
      if (key != ImGuiKey_None) {
        io.AddKeyEvent(key, bool(x));
      }
      break;
    }
    case 4: io.AddInputCharacter(uint32_t(code)); break;
    case 5: io.AddFocusEvent(bool(code)); break;
    }
  }
}
