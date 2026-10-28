// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include <memory>

#include "api016.h"

namespace dxvk {
  struct MaterialData;
}

namespace dxvk::native {
  class ApiTextures;
  class RuntimeHost;

  // Calls are serialized by the API host. Material snapshots retain their texture resources.
  class ApiMaterials {
  public:
    ApiMaterials(RuntimeHost& host, const ApiTextures& textures);
    ~ApiMaterials();
    ApiMaterials(const ApiMaterials&) = delete;
    ApiMaterials& operator=(const ApiMaterials&) = delete;

    api016::remixapi_MaterialHandle create(const api016::remixapi_MaterialInfo& info);
    void update(api016::remixapi_MaterialHandle handle, const api016::remixapi_MaterialInfo& info);
    void destroy(api016::remixapi_MaterialHandle handle);
    MaterialData resolve(api016::remixapi_MaterialHandle handle) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_pImpl;
  };
}
