/*
 * Copyright (c) 2021-2026, NVIDIA CORPORATION. All rights reserved.
 * Copyright (c) 2026 EyeCrasher.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <cmath>
#include <cstring>

#include "api016_cameras.h"
#include "api016_extensions.h"
#include "api016_materials.h"
#include "runtime_host.h"
#include "dxvk/rtx_render/rtx_context.h"
#include "dxvk/rtx_render/rtx_matrix_helpers.h"

namespace dxvk::native {
  namespace {
    namespace api = api016;

    [[noreturn]] void invalid(const char* pMessage) {
      throw ApiError(api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, pMessage);
    }

    CameraType::Enum cameraType(api::remixapi_CameraType type) {
      switch (type) {
      case api::REMIXAPI_CAMERA_TYPE_WORLD: return CameraType::Main;
      case api::REMIXAPI_CAMERA_TYPE_VIEW_MODEL: return CameraType::ViewModel;
      case api::REMIXAPI_CAMERA_TYPE_SKY: return CameraType::Sky;
      default: invalid("Unknown API camera type");
      }
    }

    Vector3d toVector(const api::remixapi_Float3D& value) {
      if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)) {
        invalid("Non-finite API camera vector");
      }
      return Vector3d(value.x, value.y, value.z);
    }

    Vector3d direction(const api::remixapi_Float3D& value) {
      const auto result = toVector(value);
      if (lengthSqr(result) < 1e-12) {
        invalid("Zero-length API camera direction");
      }
      return normalize(result);
    }

    void requireFinite(const Matrix4& matrix) {
      for (uint32_t column = 0; column < 4; ++column) {
        for (uint32_t row = 0; row < 4; ++row) {
          if (!std::isfinite(matrix[column][row])) {
            invalid("Non-finite API camera matrix");
          }
        }
      }
    }
  }

  ApiCameras::ApiCameras(RuntimeHost& host, const ApiMaterials& materials)
    : m_host(host), m_materials(materials) { }

  void ApiCameras::setup(const api016::remixapi_CameraInfo& info) {
    if (info.sType != api::REMIXAPI_STRUCT_TYPE_CAMERA_INFO) {
      invalid("Invalid API camera structure type");
    }
    const auto type = cameraType(info.type);
    const api::remixapi_CameraInfoParameterizedEXT* pParameters = nullptr;
    visitApiExtensions(info.pNext, [&](api::remixapi_StructType extensionType, const void* pNext) {
      if (extensionType == api::REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT) {
        if (pParameters) {
          invalid("Repeated API camera parameters");
        }
        pParameters = static_cast<const api::remixapi_CameraInfoParameterizedEXT*>(pNext);
      }
    });
    Matrix4 view;
    Matrix4 projection;
    if (pParameters) {
      const auto& params = *pParameters;
      if (!std::isfinite(params.fovYInDegrees) || params.fovYInDegrees <= 0 || params.fovYInDegrees >= 180
          || !std::isfinite(params.aspect) || params.aspect <= 0
          || !std::isfinite(params.nearPlane) || params.nearPlane <= 0
          || !std::isfinite(params.farPlane) || params.farPlane <= params.nearPlane) {
        invalid("Invalid API camera projection parameters");
      }
      const auto right = direction(params.right);
      const auto up = direction(params.up);
      const auto forward = direction(params.forward);
      if (std::abs(dot(cross(right, up), forward)) < 1e-6) {
        invalid("Degenerate API camera basis");
      }
      const Matrix4d viewToWorld(Vector4d(right, 0), Vector4d(up, 0), Vector4d(forward, 0),
        Vector4d(toVector(params.position), 1));
      view = inverse(viewToWorld);
      float4x4 convertedProjection;
      convertedProjection.SetupByHalfFovy(DegToRad(params.fovYInDegrees) / 2, params.aspect,
        params.nearPlane, params.farPlane, PROJ_LEFT_HANDED);
      static_assert(sizeof(projection) == sizeof(convertedProjection));
      std::memcpy(&projection, &convertedProjection, sizeof(projection));
    } else {
      view = Matrix4(info.view);
      projection = Matrix4(info.projection);
    }
    requireFinite(view);
    requireFinite(projection);
    if (determinant(view) == 0 || determinant(projection) == 0) {
      invalid("Singular API camera matrix");
    }

    // Client depth reprojection requires the unmodified near plane supplied by the API.
    const_cast<bool&>(RtxOptions::enableNearPlaneOverride()) = false;
    m_host.context()->prepareScreenResolution();
    m_host.context()->getSceneManager().getCameraManager().processExternalCamera(type, view, projection);
  }

  void ApiCameras::setMedium(const api016::remixapi_CameraMediumInfo& info) {
    if (info.sType != api::REMIXAPI_STRUCT_TYPE_CAMERA_MEDIUM_INFO) {
      invalid("Invalid API camera medium structure type");
    }
    auto& scene = m_host.context()->getSceneManager();
    if (!info.medium) {
      scene.clearExternalStartInMediumMaterial();
      return;
    }
    const auto material = m_materials.resolve(info.medium);
    if (material.getType() != MaterialDataType::Translucent) {
      invalid("API camera medium must be a translucent material");
    }
    scene.setExternalStartInMediumMaterial(material);
  }
}
