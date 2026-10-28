// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include "api016.h"

namespace dxvk::native {
  class ApiMaterials;
  class RuntimeHost;

  class ApiCameras {
  public:
    ApiCameras(RuntimeHost& host, const ApiMaterials& materials);

    // The host sets the frame extent before camera setup.
    void setup(const api016::remixapi_CameraInfo& info);
    void setMedium(const api016::remixapi_CameraMediumInfo& info);

  private:
    RuntimeHost& m_host;
    const ApiMaterials& m_materials;
  };
}
