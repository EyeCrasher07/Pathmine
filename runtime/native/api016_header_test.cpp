// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <iostream>
#include <type_traits>

#include "api016.h"

namespace api = dxvk::native::api016;

static_assert(REMIXAPI_VERSION_MINOR == 6 && REMIXAPI_VERSION_PATCH == 6);
static_assert(sizeof(remixapi_Interface) == 176);
static_assert(api::kVersion == REMIXAPI_VERSION_MAKE(0, 16, 0));
static_assert(!std::is_same_v<remixapi_MaterialHandle, api::remixapi_MaterialHandle>);
static_assert(!std::is_same_v<remixapi_MaterialInfo, api::remixapi_MaterialInfo>);
static_assert(offsetof(api::remixapi_Interface, SetCameraMediumMaterial) == 0xc0);
static_assert(offsetof(api::remixapi_Interface, UpdateTextureFromMemory) == 0xc8);
static_assert(offsetof(api::remixapi_Interface, GetConfigVariableResolved) == 0x118);
static_assert(offsetof(api::remixapi_HardcodedVertex, texcoordBounds) == 36);

int main() {
  const auto info = api::remixapi_SubmitInfo_init();
  if (info.sType != api::REMIXAPI_STRUCT_TYPE_SUBMIT_INFO || info.pNext || info.interopFlags) {
    return 1;
  }
  std::cout << "PASS isolated immutable API 0.16 types and table offsets alongside the original API 0.6\n";
}
