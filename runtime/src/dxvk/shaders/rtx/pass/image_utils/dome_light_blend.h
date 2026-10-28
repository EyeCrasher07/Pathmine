// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include "rtx/utility/shader_types.h"
#ifdef __cplusplus
#include "rtx/utility/cpu_gpu_compat.h"
#endif

#define DOME_LIGHT_BLEND_SOURCE 0
#define DOME_LIGHT_BLEND_DESTINATION 1
#define DOME_LIGHT_BLEND_OUTPUT 2

struct DomeLightBlendArgs {
  mat4 worldToLight;
  vec4 radianceAndOpacity;
  uvec4 extentBlendAndFlags;
};

#include "rtx/utility/cpu_gpu_compat_undef.h"
