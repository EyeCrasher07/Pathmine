// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <memory>

#include "rtx_types.h"

namespace dxvk {
  class DxvkContext;
  class SceneManager;
  struct ExternalDrawState;

  // The caller serializes mutations with scene submission on the context thread.
  class ExternalScene {
  public:
    explicit ExternalScene(SceneManager& scene);
    ~ExternalScene();
    ExternalScene(const ExternalScene&) = delete;
    ExternalScene& operator=(const ExternalScene&) = delete;

    bool add(uint64_t id, std::unique_ptr<ExternalDrawState> state);
    bool update(uint64_t id, std::unique_ptr<ExternalDrawState> state);
    bool remove(uint64_t id);
    void invalidateMesh(remixapi_MeshHandle mesh);
    // Releases renderer instances while retaining API descriptions for scene recovery.
    void reset();
    void submit(const Rc<DxvkContext>& ctx);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_pImpl;
  };
}
