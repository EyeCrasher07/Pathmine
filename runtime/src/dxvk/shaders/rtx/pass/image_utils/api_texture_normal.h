// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include "rtx/utility/shader_types.h"
#ifdef __cplusplus
#include "rtx/utility/cpu_gpu_compat.h"
#endif

#define API_TEXTURE_NORMAL_INPUT 0
#define API_TEXTURE_NORMAL_OUTPUT 1

struct ApiTextureNormalArgs {
  uvec2 offset;
  uvec2 extent;
  uint flipY;
  uint reconstructZ;
  uvec2 padding;
};

#include "rtx/utility/cpu_gpu_compat_undef.h"
