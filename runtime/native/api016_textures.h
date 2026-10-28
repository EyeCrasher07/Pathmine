// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <memory>

#include "api016.h"
#include "dxvk/rtx_render/rtx_texture.h"

namespace dxvk::native {
  class RuntimeHost;

  // The API host serializes calls. Returned TextureRefs retain GPU resources after handle destruction.
  class ApiTextures {
  public:
    explicit ApiTextures(RuntimeHost& host);
    ~ApiTextures();
    ApiTextures(const ApiTextures&) = delete;
    ApiTextures& operator=(const ApiTextures&) = delete;

    api016::remixapi_TextureHandle create(const api016::remixapi_TextureFromMemoryInfo& info);
    api016::remixapi_TextureHandle createFile(const std::filesystem::path& path);
    void update(const api016::remixapi_TexturePatchInfo& info);
    void destroy(api016::remixapi_TextureHandle handle);
    TextureRef resolve(api016::remixapi_TextureHandle handle) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_pImpl;
  };
}
