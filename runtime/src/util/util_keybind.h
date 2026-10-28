/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#pragma once

#include "../util/xxHash/xxhash.h"

#include <cstdint>
#include <vector>

#include <array>
#include <unordered_map>
#include <sstream>
#include <string>

namespace dxvk {
    
using VkValue = uint8_t;
struct VirtualKey {
    VkValue val = kInvalidVal;
    static constexpr VkValue kInvalidVal = 0xFF;

    bool operator==(const VirtualKey& other) const {
        return val == other.val;
    }
};
using VirtualKeys = std::vector<VirtualKey>;

constexpr const char kVirtualKeyDelimiter = ',';

class KeyBind {
public:
    static std::string getName(const VirtualKey vk) {
        const auto itr = get().m_vkValToKey.find(vk.val);
        if(itr == get().m_vkValToKey.cend()) {
            return Key::kInvalidName;
        }
        return itr->second->names.at(0);
    }
    static VirtualKey getVk(const std::string name) {
        const auto itr = get().m_nameToKey.find(name);
        if(itr == get().m_nameToKey.cend()) {
            return VirtualKey(); // Invalid
        }
        return VirtualKey{itr->second->vkVal};
    }
    static bool isValidVk(const VirtualKey& vk) {
        return vk.val != VirtualKey::kInvalidVal && 
               get().m_vkValToKey.find(vk.val) != get().m_vkValToKey.cend();
    }

private:
    static const KeyBind& get() {
        const static KeyBind single;
        return single;
    }

    struct Key {
        std::vector<std::string> names = {kInvalidName};
        VkValue                  vkVal = VirtualKey::kInvalidVal;

        static constexpr size_t kDefaultNameIdx = 0;
        inline static const std::string kInvalidName = "INVALID";
    };
    static constexpr size_t kNumKeys = 256;
    using KeyArray = std::array<Key, kNumKeys>;
    const KeyArray m_keys;
    using NameToKey = std::unordered_map<std::string, const Key* const>;
    const NameToKey m_nameToKey;
    using VkValToKey = std::unordered_map<VkValue, const Key* const>;
    const VkValToKey m_vkValToKey;
    
    // Key identifiers preserve the Windows virtual-key values used by existing Remix configs.
    KeyBind() : 
        m_keys{
            Key{{"LBUTTON"}, 0x01},
            Key{{"RBUTTON"}, 0x02},
            Key{{"CANCEL"}, 0x03},
            Key{{"MBUTTON"}, 0x04},
            Key{{"XBUTTON1"}, 0x05},
            Key{{"XBUTTON2"}, 0x06},
            Key{{"BACK"}, 0x08},
            Key{{"TAB"}, 0x09},
            Key{{"CLEAR"}, 0x0C},
            Key{{"ENTER","RETURN"}, 0x0D},
            Key{{"SHFT","SHIFT"}, 0x10},
            Key{{"CTRL","CONTROL"}, 0x11},
            Key{{"ALT","MENU"}, 0x12},
            Key{{"PAUSE"}, 0x13},
            Key{{"CAPITAL"}, 0x14},
            Key{{"KANA"}, 0x15},
            Key{{"IME_ON"}, 0x16},
            Key{{"JUNJA"}, 0x17},
            Key{{"FINAL"}, 0x18},
            Key{{"HANJA"}, 0x19},
            Key{{"IME_OFF"}, 0x1A},
            Key{{"ESCAPE"}, 0x1B},
            Key{{"CONVERT"}, 0x1C},
            Key{{"NONCONVERT"}, 0x1D},
            Key{{"ACCEPT"}, 0x1E},
            Key{{"MODECHANGE"}, 0x1F},
            Key{{"SPACE"}, 0x20},
            Key{{"PRIOR"}, 0x21},
            Key{{"NEXT"}, 0x22},
            Key{{"END"}, 0x23},
            Key{{"HOME"}, 0x24},
            Key{{"LEFT"}, 0x25},
            Key{{"UP"}, 0x26},
            Key{{"RIGHT"}, 0x27},
            Key{{"DOWN"}, 0x28},
            Key{{"SELECT"}, 0x29},
            Key{{"PRINT"}, 0x2A},
            Key{{"EXECUTE"}, 0x2B},
            Key{{"SNAPSHOT"}, 0x2C},
            Key{{"INSERT"}, 0x2D},
            Key{{"DELETE"}, 0x2E},
            Key{{"HELP"}, 0x2F},
            Key{{"0"}, '0'},
            Key{{"1"}, '1'},
            Key{{"2"}, '2'},
            Key{{"3"}, '3'},
            Key{{"4"}, '4'},
            Key{{"5"}, '5'},
            Key{{"6"}, '6'},
            Key{{"7"}, '7'},
            Key{{"8"}, '8'},
            Key{{"9"}, '9'},
            Key{{"A"}, 'A'},
            Key{{"B"}, 'B'},
            Key{{"C"}, 'C'},
            Key{{"D"}, 'D'},
            Key{{"E"}, 'E'},
            Key{{"F"}, 'F'},
            Key{{"G"}, 'G'},
            Key{{"H"}, 'H'},
            Key{{"I"}, 'I'},
            Key{{"J"}, 'J'},
            Key{{"K"}, 'K'},
            Key{{"L"}, 'L'},
            Key{{"M"}, 'M'},
            Key{{"N"}, 'N'},
            Key{{"O"}, 'O'},
            Key{{"P"}, 'P'},
            Key{{"Q"}, 'Q'},
            Key{{"R"}, 'R'},
            Key{{"S"}, 'S'},
            Key{{"T"}, 'T'},
            Key{{"U"}, 'U'},
            Key{{"V"}, 'V'},
            Key{{"W"}, 'W'},
            Key{{"X"}, 'X'},
            Key{{"Y"}, 'Y'},
            Key{{"Z"}, 'Z'},
            Key{{"LWIN"}, 0x5B},
            Key{{"RWIN"}, 0x5C},
            Key{{"APPS"}, 0x5D},
            Key{{"SLEEP"}, 0x5F},
            Key{{"NUMPAD0"}, 0x60},
            Key{{"NUMPAD1"}, 0x61},
            Key{{"NUMPAD2"}, 0x62},
            Key{{"NUMPAD3"}, 0x63},
            Key{{"NUMPAD4"}, 0x64},
            Key{{"NUMPAD5"}, 0x65},
            Key{{"NUMPAD6"}, 0x66},
            Key{{"NUMPAD7"}, 0x67},
            Key{{"NUMPAD8"}, 0x68},
            Key{{"NUMPAD9"}, 0x69},
            Key{{"MULTIPLY"}, 0x6A},
            Key{{"ADD"}, 0x6B},
            Key{{"SEPARATOR"}, 0x6C},
            Key{{"SUBTRACT"}, 0x6D},
            Key{{"DECIMAL"}, 0x6E},
            Key{{"DIVIDE"}, 0x6F},
            Key{{"F1"}, 0x70},
            Key{{"F2"}, 0x71},
            Key{{"F3"}, 0x72},
            Key{{"F4"}, 0x73},
            Key{{"F5"}, 0x74},
            Key{{"F6"}, 0x75},
            Key{{"F7"}, 0x76},
            Key{{"F8"}, 0x77},
            Key{{"F9"}, 0x78},
            Key{{"F10"}, 0x79},
            Key{{"F11"}, 0x7A},
            Key{{"F12"}, 0x7B},
            Key{{"F13"}, 0x7C},
            Key{{"F14"}, 0x7D},
            Key{{"F15"}, 0x7E},
            Key{{"F16"}, 0x7F},
            Key{{"F17"}, 0x80},
            Key{{"F18"}, 0x81},
            Key{{"F19"}, 0x82},
            Key{{"F20"}, 0x83},
            Key{{"F21"}, 0x84},
            Key{{"F22"}, 0x85},
            Key{{"F23"}, 0x86},
            Key{{"F24"}, 0x87},
            Key{{"NAVIGATION_VIEW"}, 0x88},
            Key{{"NAVIGATION_MENU"}, 0x89},
            Key{{"NAVIGATION_UP"}, 0x8A},
            Key{{"NAVIGATION_DOWN"}, 0x8B},
            Key{{"NAVIGATION_LEFT"}, 0x8C},
            Key{{"NAVIGATION_RIGHT"}, 0x8D},
            Key{{"NAVIGATION_ACCEPT"}, 0x8E},
            Key{{"NAVIGATION_CANCEL"}, 0x8F},
            Key{{"NUMLOCK"}, 0x90},
            Key{{"SCROLL"}, 0x91},
            Key{{"OEM_NEC_EQUAL"}, 0x92},
            Key{{"OEM_FJ_MASSHOU"}, 0x93},
            Key{{"OEM_FJ_TOUROKU"}, 0x94},
            Key{{"OEM_FJ_LOYA"}, 0x95},
            Key{{"OEM_FJ_ROYA"}, 0x96},
            Key{{"LSHIFT"}, 0xA0},
            Key{{"RSHIFT"}, 0xA1},
            Key{{"LCONTROL"}, 0xA2},
            Key{{"RCONTROL"}, 0xA3},
            Key{{"LMENU"}, 0xA4},
            Key{{"RMENU"}, 0xA5},
            Key{{"BROWSER_BACK"}, 0xA6},
            Key{{"BROWSER_FORWARD"}, 0xA7},
            Key{{"BROWSER_REFRESH"}, 0xA8},
            Key{{"BROWSER_STOP"}, 0xA9},
            Key{{"BROWSER_SEARCH"}, 0xAA},
            Key{{"BROWSER_FAVORITES"}, 0xAB},
            Key{{"BROWSER_HOME"}, 0xAC},
            Key{{"VOLUME_MUTE"}, 0xAD},
            Key{{"VOLUME_DOWN"}, 0xAE},
            Key{{"VOLUME_UP"}, 0xAF},
            Key{{"MEDIA_NEXT_TRACK"}, 0xB0},
            Key{{"MEDIA_PREV_TRACK"}, 0xB1},
            Key{{"MEDIA_STOP"}, 0xB2},
            Key{{"MEDIA_PLAY_PAUSE"}, 0xB3},
            Key{{"LAUNCH_MAIL"}, 0xB4},
            Key{{"LAUNCH_MEDIA_SELECT"}, 0xB5},
            Key{{"LAUNCH_APP1"}, 0xB6},
            Key{{"LAUNCH_APP2"}, 0xB7},
            Key{{"OEM_1"}, 0xBA},
            Key{{"OEM_PLUS"}, 0xBB},
            Key{{"OEM_COMMA"}, 0xBC},
            Key{{"OEM_MINUS"}, 0xBD},
            Key{{"OEM_PERIOD"}, 0xBE},
            Key{{"OEM_2"}, 0xBF},
            Key{{"OEM_3"}, 0xC0},
            Key{{"GAMEPAD_A"}, 0xC3},
            Key{{"GAMEPAD_B"}, 0xC4},
            Key{{"GAMEPAD_X"}, 0xC5},
            Key{{"GAMEPAD_Y"}, 0xC6},
            Key{{"GAMEPAD_RIGHT_SHOULDER"}, 0xC7},
            Key{{"GAMEPAD_LEFT_SHOULDER"}, 0xC8},
            Key{{"GAMEPAD_LEFT_TRIGGER"}, 0xC9},
            Key{{"GAMEPAD_RIGHT_TRIGGER"}, 0xCA},
            Key{{"GAMEPAD_DPAD_UP"}, 0xCB},
            Key{{"GAMEPAD_DPAD_DOWN"}, 0xCC},
            Key{{"GAMEPAD_DPAD_LEFT"}, 0xCD},
            Key{{"GAMEPAD_DPAD_RIGHT"}, 0xCE},
            Key{{"GAMEPAD_MENU"}, 0xCF},
            Key{{"GAMEPAD_VIEW"}, 0xD0},
            Key{{"GAMEPAD_LEFT_THUMBSTICK_BUTTON"}, 0xD1},
            Key{{"GAMEPAD_RIGHT_THUMBSTICK_BUTTON"}, 0xD2},
            Key{{"GAMEPAD_LEFT_THUMBSTICK_UP"}, 0xD3},
            Key{{"GAMEPAD_LEFT_THUMBSTICK_DOWN"}, 0xD4},
            Key{{"GAMEPAD_LEFT_THUMBSTICK_RIGHT"}, 0xD5},
            Key{{"GAMEPAD_LEFT_THUMBSTICK_LEFT"}, 0xD6},
            Key{{"GAMEPAD_RIGHT_THUMBSTICK_UP"}, 0xD7},
            Key{{"GAMEPAD_RIGHT_THUMBSTICK_DOWN"}, 0xD8},
            Key{{"GAMEPAD_RIGHT_THUMBSTICK_RIGHT"}, 0xD9},
            Key{{"GAMEPAD_RIGHT_THUMBSTICK_LEFT"}, 0xDA},
            Key{{"OEM_4"}, 0xDB},
            Key{{"OEM_5"}, 0xDC},
            Key{{"OEM_6"}, 0xDD},
            Key{{"OEM_7"}, 0xDE},
            Key{{"OEM_8"}, 0xDF},
            Key{{"OEM_AX"}, 0xE1},
            Key{{"OEM_102"}, 0xE2},
            Key{{"ICO_HELP"}, 0xE3},
            Key{{"ICO_00"}, 0xE4},
            Key{{"PROCESSKEY"}, 0xE5},
            Key{{"ICO_CLEAR"}, 0xE6},
            Key{{"PACKET"}, 0xE7},
            Key{{"OEM_RESET"}, 0xE9},
            Key{{"OEM_JUMP"}, 0xEA},
            Key{{"OEM_PA1"}, 0xEB},
            Key{{"OEM_PA2"}, 0xEC},
            Key{{"OEM_PA3"}, 0xED},
            Key{{"OEM_WSCTRL"}, 0xEE},
            Key{{"OEM_CUSEL"}, 0xEF},
            Key{{"OEM_ATTN"}, 0xF0},
            Key{{"OEM_FINISH"}, 0xF1},
            Key{{"OEM_COPY"}, 0xF2},
            Key{{"OEM_AUTO"}, 0xF3},
            Key{{"OEM_ENLW"}, 0xF4},
            Key{{"OEM_BACKTAB"}, 0xF5},
            Key{{"ATTN"}, 0xF6},
            Key{{"CRSEL"}, 0xF7},
            Key{{"EXSEL"}, 0xF8},
            Key{{"EREOF"}, 0xF9},
            Key{{"PLAY"}, 0xFA},
            Key{{"ZOOM"}, 0xFB},
            Key{{"NONAME"}, 0xFC},
            Key{{"PA1"}, 0xFD},
            Key{{"OEM_CLEAR"}, 0xFE},
            Key{{"INVALID"}, 0xFF}
        },
        m_nameToKey(initNameToKey(m_keys)),
        m_vkValToKey(initVkValToKey(m_keys))
    {}
    ~KeyBind() {}
    KeyBind(const KeyBind& other) = delete;
    KeyBind(const KeyBind&& other) = delete;

    static NameToKey initNameToKey(const KeyArray& keyArray) {
        NameToKey nameToKey;
        for(const auto& key : keyArray) {
            for(const auto& name : key.names) {
                nameToKey.emplace(name, &key);
            }
        }
        return nameToKey;
    }
    
    static VkValToKey initVkValToKey(const KeyArray& keyArray) {
        VkValToKey vkValToKey;
        for(const auto& key : keyArray) {
            vkValToKey.emplace(key.vkVal, &key);
        }
        return vkValToKey;
    } 
};

// NV-DXVK begin
// `buildKeyBindDescriptorString`  with no delimiter arg should be used for serialization.
// `buildKeyBindDescriptorStringForDisplay` should be used for displaying keyboard shortcuts in the UI.
static std::string buildKeyBindDescriptorString(const VirtualKeys& virtKeys, const char delimiter = kVirtualKeyDelimiter) {
  std::stringstream ss;
  bool bIsFirst = true;
  for(const auto& virtKey : virtKeys) {
      if(!bIsFirst) {
          ss << delimiter;
      }
      ss << KeyBind::getName(virtKey);
      bIsFirst = false;
  }
  return ss.str();
}

static std::string buildKeyBindDescriptorStringForDisplay(const VirtualKeys& virtKeys) {
    return buildKeyBindDescriptorString(virtKeys, '+');
}
// NV-DXVK end

}
