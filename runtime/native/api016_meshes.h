// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include <memory>
#include <vector>

#include "api016.h"

namespace dxvk {
  struct RasterGeometry;
}

namespace dxvk::native {
  class ApiMaterials;
  class RuntimeHost;

  // Calls are serialized by the API host. Snapshots own their uploaded buffers.
  class ApiMeshes {
  public:
    ApiMeshes(RuntimeHost& host, const ApiMaterials& materials);
    ~ApiMeshes();
    ApiMeshes(const ApiMeshes&) = delete;
    ApiMeshes& operator=(const ApiMeshes&) = delete;

    api016::remixapi_MeshHandle create(const api016::remixapi_MeshInfo& info);
    void destroy(api016::remixapi_MeshHandle handle);
    std::shared_ptr<const std::vector<RasterGeometry>> resolve(api016::remixapi_MeshHandle handle) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_pImpl;
  };
}
