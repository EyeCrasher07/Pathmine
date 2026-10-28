#include <iostream>
#include <stdexcept>

#include "util/util_keybind.h"

int main() {
  try {
    dxvk::VirtualKeys shortcut;
    for (const auto* pName : {"CONTROL", "SHIFT", "MENU", "PAUSE"}) {
      const auto key = dxvk::KeyBind::getVk(pName);
      if (!dxvk::KeyBind::isValidVk(key)) {
        throw std::runtime_error("An existing shortcut alias was lost");
      }
      shortcut.push_back(key);
    }
    if (shortcut[0].val != 0x11 || shortcut[1].val != 0x10
        || shortcut[2].val != 0x12 || shortcut[3].val != 0x13
        || dxvk::buildKeyBindDescriptorString(shortcut) != "CTRL,SHFT,ALT,PAUSE") {
      throw std::runtime_error("Existing Remix shortcut serialization changed");
    }
    if (dxvk::KeyBind::getVk("F11").val != 0x7A
        || dxvk::KeyBind::getVk("LBUTTON").val != 0x01
        || dxvk::KeyBind::isValidVk(dxvk::KeyBind::getVk("UNKNOWN_KEY"))) {
      throw std::runtime_error("Function, mouse or invalid key mapping changed");
    }
    std::cout << "PASS original Remix key aliases and configuration IDs without OS input headers\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
