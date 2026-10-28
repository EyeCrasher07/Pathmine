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
#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <unordered_set>
#include <variant>

#include "api016_error.h"
#include "api016_extensions.h"
#include "api016_lights.h"
#include "api016_textures.h"
#include "runtime_host.h"
#include "dxvk/rtx_render/rtx_context.h"
#include "dxvk/rtx_render/rtx_lights_data.h"

namespace dxvk::native {
  namespace {
    namespace api = api016;

    [[noreturn]] void invalid(const char* pMessage) {
      throw ApiError(api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, pMessage);
    }

    void finite(std::initializer_list<float> values) {
      for (float value : values) {
        if (!std::isfinite(value)) {
          invalid("Non-finite API light parameter");
        }
      }
    }

    Vector3 toVector(const api::remixapi_Float3D& value) {
      finite({ value.x, value.y, value.z });
      return Vector3(value.x, value.y, value.z);
    }

    void volumeScale(float value) {
      finite({ value });
      if (value < 0.f || value >= FLOAT16_MAX) {
        invalid("API volumetric radiance scale is out of range");
      }
    }

    Matrix4 toMatrix(const api::remixapi_Transform& value) {
      const auto& m = value.matrix;
      for (const auto& row : m) {
        for (const auto component : row) {
          finite({ component });
        }
      }
      return Matrix4 {
        m[0][0], m[1][0], m[2][0], 0.f,
        m[0][1], m[1][1], m[2][1], 0.f,
        m[0][2], m[1][2], m[2][2], 0.f,
        m[0][3], m[1][3], m[2][3], 1.f
      };
    }

    struct Extensions {
      const api::remixapi_LightInfoSphereEXT* pSphere = nullptr;
      const api::remixapi_LightInfoRectEXT* pRect = nullptr;
      const api::remixapi_LightInfoDiskEXT* pDisk = nullptr;
      const api::remixapi_LightInfoCylinderEXT* pCylinder = nullptr;
      const api::remixapi_LightInfoDistantEXT* pDistant = nullptr;
      const api::remixapi_LightInfoDomeEXT* pDome = nullptr;
      const api::remixapi_LightInfoUSDEXT* pUsd = nullptr;

      explicit Extensions(const api::remixapi_LightInfo& info) {
        if (info.sType != api::REMIXAPI_STRUCT_TYPE_LIGHT_INFO || !info.hash) {
          invalid("Invalid API light description or hash");
        }
        visitApiExtensions(info.pNext, [&](api::remixapi_StructType type, const void* pExtension) {
#define LIGHT_EXTENSION(name, constant) \
          case api::REMIXAPI_STRUCT_TYPE_LIGHT_INFO_##constant##_EXT: \
            if (p##name) { invalid("Repeated API light extension"); } \
            p##name = static_cast<const api::remixapi_LightInfo##name##EXT*>(pExtension); \
            break;
          switch (type) {
            LIGHT_EXTENSION(Sphere, SPHERE)
            LIGHT_EXTENSION(Rect, RECT)
            LIGHT_EXTENSION(Disk, DISK)
            LIGHT_EXTENSION(Cylinder, CYLINDER)
            LIGHT_EXTENSION(Distant, DISTANT)
            LIGHT_EXTENSION(Dome, DOME)
          case api::REMIXAPI_STRUCT_TYPE_LIGHT_INFO_USD_EXT:
            if (pUsd) {
              invalid("Repeated USD light extension");
            }
            pUsd = static_cast<const api::remixapi_LightInfoUSDEXT*>(pExtension);
            break;
          default:
            break;
          }
#undef LIGHT_EXTENSION
        });
        const unsigned shapes = unsigned(pSphere != nullptr) + unsigned(pRect != nullptr)
          + unsigned(pDisk != nullptr) + unsigned(pCylinder != nullptr) + unsigned(pDistant != nullptr)
          + unsigned(pDome != nullptr);
        if (!pUsd && shapes != 1) {
          invalid("API light requires one light shape extension");
        }
      }
    };

    RtLightShaping shaping(bool enabled, const api::remixapi_LightInfoLightShaping& info) {
      if (!enabled) {
        return RtLightShaping { };
      }
      finite({ info.coneAngleDegrees, info.coneSoftness, info.focusExponent });
      const auto result = RtLightShaping::tryCreate(true, toVector(info.direction),
        std::cos(DegToRad(info.coneAngleDegrees)), info.coneSoftness, info.focusExponent);
      if (!result) {
        invalid("Invalid API light shaping");
      }
      return *result;
    }

    RtLight usdLight(const api::remixapi_LightInfoUSDEXT& input) {
      static_assert(sizeof(input) == sizeof(remixapi_LightInfoUSDEXT));
      static_assert(offsetof(api::remixapi_LightInfoUSDEXT, pColor) == offsetof(remixapi_LightInfoUSDEXT, pColor));
      static_assert(offsetof(api::remixapi_LightInfoUSDEXT, pVolumetricRadianceScale)
        == offsetof(remixapi_LightInfoUSDEXT, pVolumetricRadianceScale));
      static_assert(uint32_t(api::REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT) == REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT
        && uint32_t(api::REMIXAPI_STRUCT_TYPE_LIGHT_INFO_RECT_EXT) == REMIXAPI_STRUCT_TYPE_LIGHT_INFO_RECT_EXT
        && uint32_t(api::REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISK_EXT) == REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISK_EXT
        && uint32_t(api::REMIXAPI_STRUCT_TYPE_LIGHT_INFO_CYLINDER_EXT) == REMIXAPI_STRUCT_TYPE_LIGHT_INFO_CYLINDER_EXT
        && uint32_t(api::REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT) == REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT);
      remixapi_LightInfoUSDEXT info;
      std::memcpy(&info, &input, sizeof(info));
      info.pNext = nullptr;
      remixapi_Float3D color { };
      if (input.pColor) {
        color = { input.pColor->x, input.pColor->y, input.pColor->z };
        info.pColor = &color;
      }
      const auto data = LightData::tryCreate(info);
      const auto result = data ? data->toRtLight() : std::optional<RtLight> { };
      if (!result) {
        invalid("Invalid USD API light");
      }
      return *result;
    }

    std::variant<RtLight, DomeLight> convert(const api::remixapi_LightInfo& info, const ApiTextures& textures) {
      const Extensions ext(info);
      if (ext.pUsd) {
        return usdLight(*ext.pUsd);
      }
      const auto radiance = toVector(info.radiance);
      if (radiance.x < 0 || radiance.y < 0 || radiance.z < 0) {
        invalid("Negative API light radiance");
      }
      if (ext.pDome) {
        const auto& src = *ext.pDome;
        finite({ src.opacity });
        if (src.blendType < 0 || src.blendType > 10) {
          invalid("Invalid API dome blend type");
        }
        const auto matrix = toMatrix(src.transform);
        const auto worldToLight = inverse(matrix);
        for (uint32_t column = 0; column < 4; ++column) {
          for (uint32_t row = 0; row < 4; ++row) {
            finite({ worldToLight[column][row] });
          }
        }
        DomeLight dome;
        dome.radiance = radiance;
        dome.worldToLight = worldToLight;
        dome.texture = textures.resolve(src.colorTexture);
        dome.opacity = std::clamp(src.opacity, 0.f, 1.f);
        dome.blendType = static_cast<BlendType>(src.blendType);
        dome.enableBlending = true;
        return dome;
      }
      std::optional<RtLight> result;
      if (ext.pSphere) {
        const auto& src = *ext.pSphere;
        finite({ src.radius, src.volumetricRadianceScale });
        volumeScale(src.volumetricRadianceScale);
        result = RtSphereLight::tryCreate(toVector(src.position), radiance, src.radius,
          shaping(src.shaping_hasvalue, src.shaping_value), src.volumetricRadianceScale);
      } else if (ext.pRect) {
        const auto& src = *ext.pRect;
        finite({ src.xSize, src.ySize, src.volumetricRadianceScale });
        volumeScale(src.volumetricRadianceScale);
        result = RtRectLight::tryCreate(toVector(src.position), Vector2(src.xSize, src.ySize),
          toVector(src.xAxis), toVector(src.yAxis), toVector(src.direction), radiance,
          shaping(src.shaping_hasvalue, src.shaping_value), src.volumetricRadianceScale);
      } else if (ext.pDisk) {
        const auto& src = *ext.pDisk;
        finite({ src.xRadius, src.yRadius, src.volumetricRadianceScale });
        volumeScale(src.volumetricRadianceScale);
        result = RtDiskLight::tryCreate(toVector(src.position), Vector2(src.xRadius, src.yRadius),
          toVector(src.xAxis), toVector(src.yAxis), toVector(src.direction), radiance,
          shaping(src.shaping_hasvalue, src.shaping_value), src.volumetricRadianceScale);
      } else if (ext.pCylinder) {
        const auto& src = *ext.pCylinder;
        finite({ src.radius, src.axisLength, src.volumetricRadianceScale });
        volumeScale(src.volumetricRadianceScale);
        result = RtCylinderLight::tryCreate(toVector(src.position), src.radius,
          toVector(src.axis), src.axisLength, radiance, src.volumetricRadianceScale);
      } else if (ext.pDistant) {
        const auto& src = *ext.pDistant;
        finite({ src.angularDiameterDegrees, src.volumetricRadianceScale });
        volumeScale(src.volumetricRadianceScale);
        result = RtDistantLight::tryCreate(toVector(src.direction), DegToRad(src.angularDiameterDegrees * .5f),
          radiance, src.volumetricRadianceScale);
      }
      if (!result) {
        invalid("Invalid analytical API light");
      }
      return *result;
    }

    remixapi_LightHandle toEngineHandle(api::remixapi_LightHandle handle) {
      return reinterpret_cast<remixapi_LightHandle>(handle);
    }
  }

  struct ApiLights::Impl {
    RuntimeHost& host;
    const ApiTextures& textures;
    std::unordered_set<api::remixapi_LightHandle> handles;

    Impl(RuntimeHost& value, const ApiTextures& inputTextures) : host(value), textures(inputTextures) { }

    LightManager& lights() const {
      return host.context()->getSceneManager().getLightManager();
    }

    void find(api::remixapi_LightHandle handle) const {
      if (!handle || !handles.count(handle)) {
        invalid("Unknown or destroyed API light handle");
      }
    }
  };

  ApiLights::ApiLights(RuntimeHost& host, const ApiTextures& textures)
    : m_pImpl(std::make_unique<Impl>(host, textures)) { }

  ApiLights::~ApiLights() {
    for (const auto handle : m_pImpl->handles) {
      m_pImpl->lights().removeExternalLight(toEngineHandle(handle));
    }
  }

  api016::remixapi_LightHandle ApiLights::create(const api016::remixapi_LightInfo& info) {
    auto& state = *m_pImpl;
    const auto light = convert(info, state.textures);
    const auto handle = reinterpret_cast<api::remixapi_LightHandle>(info.hash);
    const auto [it, inserted] = state.handles.insert(handle);
    try {
      if (const auto* pDome = std::get_if<DomeLight>(&light)) {
        uint32_t unused = BINDING_INDEX_INVALID;
        state.host.context()->getSceneManager().trackTexture(pDome->texture, unused, true, false);
        state.lights().addExternalDomeLight(toEngineHandle(handle), *pDome);
      } else {
        state.lights().addExternalLight(toEngineHandle(handle), std::get<RtLight>(light));
      }
    } catch (...) {
      if (inserted) {
        state.handles.erase(it);
      }
      throw;
    }
    return handle;
  }

  void ApiLights::destroy(api016::remixapi_LightHandle handle) {
    auto& state = *m_pImpl;
    state.find(handle);
    state.lights().removeExternalLight(toEngineHandle(handle));
    state.handles.erase(handle);
  }

  void ApiLights::draw(api016::remixapi_LightHandle handle) {
    auto& state = *m_pImpl;
    state.find(handle);
    state.lights().addExternalLightInstance(toEngineHandle(handle));
  }
}
