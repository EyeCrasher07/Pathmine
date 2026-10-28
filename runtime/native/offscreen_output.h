// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <memory>

#include "dxvk/dxvk_device.h"
#include "dxvk/dxvk_image.h"
#include "interop_info.h"

namespace dxvk::native {
  class OffscreenOutput {
  public:
    explicit OffscreenOutput(const Rc<DxvkDevice>& device);
    ~OffscreenOutput();
    OffscreenOutput(const OffscreenOutput&) = delete;
    OffscreenOutput& operator=(const OffscreenOutput&) = delete;

    const Rc<DxvkImage>& beginFrame(VkExtent3D extent, VkExtent3D depthExtent = { 0, 0, 1 }, bool overlay = false);
    const Rc<DxvkImage>& overlayImage() const;
    // A missing depth source clears the requested shared depth to the far plane.
    void endFrame(const Rc<DxvkContext>& context, const Rc<DxvkImage>& depthSource = nullptr);
    void present(uint64_t clientDoneValue, bool waitForClient = false);

    // Returned FDs remain borrowed until resize or destruction.
    OutputInteropInfo interopInfo() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_pImpl;
  };
}
