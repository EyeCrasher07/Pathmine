// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include <memory>

#include "api016.h"

namespace dxvk::native {
  class ApiOptions {
  public:
    ApiOptions();
    ~ApiOptions();
    ApiOptions(const ApiOptions&) = delete;
    ApiOptions& operator=(const ApiOptions&) = delete;

    void set(const char* pKey, const char* pValue);
    void read(api016::remixapi_ConfigVariableResolvedInfo& info) const;
    api016::remixapi_ConfigLayerHandle acquire(const char* pPath, float priority, float threshold);
    void blend(api016::remixapi_ConfigLayerHandle handle, float strength, float threshold);
    void release(api016::remixapi_ConfigLayerHandle handle);

    // The API host calls this after system layers load and before device initialization.
    void applyStartupWrites();

  private:
    struct Impl;
    std::unique_ptr<Impl> m_pImpl;
  };
}
