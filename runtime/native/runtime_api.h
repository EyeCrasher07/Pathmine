// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include "api016.h"

namespace dxvk::native {
  // The frame extent is supplied before cameras and transient scene capture.
  using BeginFrameFunction = api016::remixapi_ErrorCode(REMIXAPI_CALL*)(uint32_t width, uint32_t height);
  constexpr const char* kBeginFrameSymbol = "remixapi_BeginFrame";
  // Linux overlay images share the frame's timeline semaphores. Input uses GLFW key codes.
  using OverlayInteropFunction = api016::remixapi_ErrorCode(REMIXAPI_CALL*)(api016::remixapi_dxvk_InteropInfo*, uint32_t* visible);
  using OverlayInputFunction = api016::remixapi_ErrorCode(REMIXAPI_CALL*)(uint32_t type, int32_t code, double x, double y);
}
