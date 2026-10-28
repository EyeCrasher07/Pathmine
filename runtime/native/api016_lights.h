// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include <memory>

#include "api016.h"

namespace dxvk::native {
  class ApiTextures;
  class RuntimeHost;

  // Descriptions are copied immediately. A light is active only in frames where it is drawn.
  class ApiLights {
  public:
    ApiLights(RuntimeHost& host, const ApiTextures& textures);
    ~ApiLights();
    ApiLights(const ApiLights&) = delete;
    ApiLights& operator=(const ApiLights&) = delete;

    api016::remixapi_LightHandle create(const api016::remixapi_LightInfo& info);
    void destroy(api016::remixapi_LightHandle handle);
    void draw(api016::remixapi_LightHandle handle);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_pImpl;
  };
}
