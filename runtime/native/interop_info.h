// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

namespace dxvk::native {
  struct OutputInteropInfo {
    int imageFd = -1;
    int rendererDoneFd = -1;
    int clientDoneFd = -1;
    int depthFd = -1;
    int overlayFd = -1;
    uint64_t rendererDoneValue = 0;
    uint64_t allocationSize = 0;
    uint64_t depthAllocationSize = 0;
    uint64_t overlayAllocationSize = 0;
    VkExtent3D extent { 0, 0, 1 };
    VkExtent3D depthExtent { 0, 0, 1 };
    VkExtent3D overlayExtent { 0, 0, 1 };
    bool overlayVisible = false;
  };
}
