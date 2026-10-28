// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include <memory>

#include "api016.h"

namespace dxvk::native {
  class ApiMeshes;
  class RuntimeHost;

  // Retained descriptions are consumed on submission. Calls use the host's context thread.
  class ApiInstances {
  public:
    ApiInstances(RuntimeHost& host, const ApiMeshes& meshes);
    ~ApiInstances();
    ApiInstances(const ApiInstances&) = delete;
    ApiInstances& operator=(const ApiInstances&) = delete;

    api016::remixapi_SceneInstanceHandle add(const api016::remixapi_InstanceInfo& info);
    void update(api016::remixapi_SceneInstanceHandle handle, const api016::remixapi_InstanceInfo& info);
    void remove(api016::remixapi_SceneInstanceHandle handle);
    void draw(const api016::remixapi_InstanceInfo& info);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_pImpl;
  };
}
