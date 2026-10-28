// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "offscreen_output.h"

namespace dxvk {
  class RtxContext;
}

namespace dxvk::native {
  // Calls are serialized by the API host. The client finishes its GL work before destruction.
  class RuntimeHost {
  public:
    // Startup configuration is resolved after system layers load and before device presets.
    explicit RuntimeHost(const std::string& rootPath, const std::function<void()>& configure = {});
    ~RuntimeHost();
    RuntimeHost(const RuntimeHost&) = delete;
    RuntimeHost& operator=(const RuntimeHost&) = delete;

    const Rc<DxvkDevice>& device() const;
    const Rc<RtxContext>& context() const;
    void enableOverlay();
    void overlayInput(uint32_t type, int32_t code, double x, double y);

    // Sets camera resolution before scene capture. Zero-sized windows skip capture and submission.
    bool beginFrame(VkExtent3D extent);
    void submit();
    void present(uint64_t clientDoneValue, bool waitForClient = false);
    OutputInteropInfo interopInfo() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_pImpl;
  };
}
