// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>

#include <remix/remix_c.h>

// Keep the published Minecraft header unchanged and its types separate from API 0.6.
#pragma push_macro("REMIX_C_H_")
#pragma push_macro("REMIX_WINAPI_NO_INCLUDE")
#pragma push_macro("REMIX_WINAPI_NO_LIBRARY_LOADER")
#pragma push_macro("REMIX_ALLOW_X86")
#pragma push_macro("REMIXAPI_VERSION_MAJOR")
#pragma push_macro("REMIXAPI_VERSION_MINOR")
#pragma push_macro("REMIXAPI_VERSION_PATCH")
#pragma push_macro("REMIXAPI_INLINE")
#pragma push_macro("REMIXAPI_HELPER")
#pragma push_macro("remixapi_InitializeLibrary")
#pragma push_macro("__stdcall")
#undef REMIX_C_H_
#undef REMIXAPI_VERSION_MAJOR
#undef REMIXAPI_VERSION_MINOR
#undef REMIXAPI_VERSION_PATCH
#undef __stdcall
#define REMIX_WINAPI_NO_INCLUDE
#define REMIX_WINAPI_NO_LIBRARY_LOADER
#define REMIX_ALLOW_X86
#define __stdcall
#define remixapi_InitializeLibrary remixapi016_InitializeLibraryDeclaration

namespace dxvk::native::api016 {
  struct remixapi_MaterialHandle_T;
  struct remixapi_MeshHandle_T;
  struct remixapi_LightHandle_T;
  struct remixapi_TextureHandle_T;
  struct remixapi_SceneInstanceHandle_T;
  struct remixapi_ConfigLayerHandle_T;
  struct IDirect3D9Ex;
  struct IDirect3DDevice9Ex;
  struct IDirect3DSurface9;
  struct HWND__;
  struct HINSTANCE__;
#include <remix_api016/remix_c.h>
  constexpr uint64_t kVersion = REMIXAPI_VERSION_MAKE(
    REMIXAPI_VERSION_MAJOR, REMIXAPI_VERSION_MINOR, REMIXAPI_VERSION_PATCH);
}

#pragma pop_macro("__stdcall")
#pragma pop_macro("remixapi_InitializeLibrary")
#pragma pop_macro("REMIXAPI_HELPER")
#pragma pop_macro("REMIXAPI_INLINE")
#pragma pop_macro("REMIXAPI_VERSION_PATCH")
#pragma pop_macro("REMIXAPI_VERSION_MINOR")
#pragma pop_macro("REMIXAPI_VERSION_MAJOR")
#pragma pop_macro("REMIX_ALLOW_X86")
#pragma pop_macro("REMIX_WINAPI_NO_LIBRARY_LOADER")
#pragma pop_macro("REMIX_WINAPI_NO_INCLUDE")
#pragma pop_macro("REMIX_C_H_")

static_assert(sizeof(void*) == 8, "The native Minecraft API requires 64-bit pointers");
static_assert(sizeof(dxvk::native::api016::remixapi_Interface) == 288, "Unexpected Minecraft API table");
static_assert(sizeof(dxvk::native::api016::remixapi_HardcodedVertex) == 64, "Unexpected Minecraft vertex layout");
