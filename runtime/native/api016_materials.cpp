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
#include <initializer_list>
#include <limits>
#include <vector>

#include "api016_error.h"
#include "api016_extensions.h"
#include "api016_materials.h"
#include "api016_textures.h"
#include "runtime_host.h"
#include "dxvk/rtx_render/rtx_context.h"

namespace dxvk::native {
  namespace {
    namespace api = api016;

    [[noreturn]] void invalid(const char* pMessage) {
      throw ApiError(api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, pMessage);
    }

    Vector3 toVector(const api::remixapi_Float3D& value) {
      if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)) {
        invalid("Non-finite API material color");
      }
      return Vector3(value.x, value.y, value.z);
    }

    void requireFinite(std::initializer_list<float> values) {
      for (float value : values) {
        if (!std::isfinite(value)) {
          invalid("Non-finite API material parameter");
        }
      }
    }

    struct Extensions {
      const api::remixapi_MaterialInfoOpaqueEXT* pOpaque = nullptr;
      const api::remixapi_MaterialInfoTranslucentEXT* pTranslucent = nullptr;
      const api::remixapi_MaterialInfoPortalEXT* pPortal = nullptr;
      const api::remixapi_MaterialInfoOpaqueSubsurfaceEXT* pSubsurface = nullptr;

      explicit Extensions(const api::remixapi_MaterialInfo& info) {
        if (info.sType != api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO) {
          invalid("Invalid API material structure type");
        }
        visitApiExtensions(info.pNext, [&](api::remixapi_StructType type, const void* pNext) {
          switch (type) {
          case api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT:
            if (pOpaque) {
              invalid("Repeated opaque material extension");
            }
            pOpaque = static_cast<const api::remixapi_MaterialInfoOpaqueEXT*>(pNext);
            break;
          case api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_TRANSLUCENT_EXT:
            if (pTranslucent) {
              invalid("Repeated translucent material extension");
            }
            pTranslucent = static_cast<const api::remixapi_MaterialInfoTranslucentEXT*>(pNext);
            break;
          case api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_PORTAL_EXT:
            if (pPortal) {
              invalid("Repeated portal material extension");
            }
            pPortal = static_cast<const api::remixapi_MaterialInfoPortalEXT*>(pNext);
            break;
          case api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_SUBSURFACE_EXT:
            if (pSubsurface) {
              invalid("Repeated subsurface material extension");
            }
            pSubsurface = static_cast<const api::remixapi_MaterialInfoOpaqueSubsurfaceEXT*>(pNext);
            break;
          default:
            break;
          }
        });
        if (unsigned(bool(pOpaque)) + unsigned(bool(pTranslucent)) + unsigned(bool(pPortal)) != 1
            || (pSubsurface && !pOpaque)) {
          invalid("API material requires one material kind and matching extensions");
        }
      }
    };

    MaterialData convert(const api::remixapi_MaterialInfo& info, const ApiTextures& textures) {
      const Extensions ext(info);
      requireFinite({ info.emissiveIntensity });
      const auto emissiveColor = toVector(info.emissiveColorConstant);
      if (ext.pOpaque) {
        const auto& opaque = *ext.pOpaque;
        const auto* pSubsurface = ext.pSubsurface;
        requireFinite({ opaque.anisotropy, opaque.opacityConstant, opaque.roughnessConstant,
          opaque.metallicConstant, opaque.displaceIn, opaque.displaceOut,
          opaque.dlssControlMaskIntensity, opaque.dlssControlMaskToneStrength,
          opaque.dlssControlMaskStructuralStrength });
        if (opaque.thinFilmThickness_hasvalue) {
          requireFinite({ opaque.thinFilmThickness_value });
        }
        if (opaque.alphaTestType < int(AlphaTestType::kMinValue)
            || opaque.alphaTestType > int(AlphaTestType::kMaxValue)
            || (opaque.blendType_hasvalue && (opaque.blendType_value < int(BlendType::kMinValue)
              || opaque.blendType_value > int(BlendType::kMaxValue)))) {
          invalid("Invalid API material alpha or blend mode");
        }
        if (pSubsurface) {
          requireFinite({ pSubsurface->subsurfaceMeasurementDistance, pSubsurface->subsurfaceVolumetricAnisotropy,
            pSubsurface->subsurfaceRadiusScale, pSubsurface->subsurfaceMaxSampleRadius });
        }
        return MaterialData { OpaqueMaterialData {
          textures.resolve(info.albedoTexture),
          textures.resolve(info.normalTexture),
          textures.resolve(info.tangentTexture),
          textures.resolve(opaque.heightTexture),
          textures.resolve(opaque.roughnessTexture),
          textures.resolve(opaque.metallicTexture),
          textures.resolve(info.emissiveTexture),
          textures.resolve(pSubsurface ? pSubsurface->subsurfaceTransmittanceTexture : nullptr),
          textures.resolve(pSubsurface ? pSubsurface->subsurfaceThicknessTexture : nullptr),
          textures.resolve(pSubsurface ? pSubsurface->subsurfaceSingleScatteringAlbedoTexture : nullptr),
          textures.resolve(pSubsurface ? pSubsurface->subsurfaceRadiusTexture : nullptr),
          {},
          opaque.anisotropy,
          info.emissiveIntensity,
          toVector(opaque.albedoConstant),
          opaque.opacityConstant,
          opaque.roughnessConstant,
          opaque.metallicConstant,
          emissiveColor,
          info.emissiveIntensity > 0.f,
          info.spriteSheetRow,
          info.spriteSheetCol,
          info.spriteSheetFps,
          bool(opaque.thinFilmThickness_hasvalue),
          bool(opaque.alphaIsThinFilmThickness),
          opaque.thinFilmThickness_hasvalue ? opaque.thinFilmThickness_value : 200.f,
          bool(opaque.useDrawCallAlphaState),
          bool(opaque.blendType_hasvalue),
          opaque.blendType_hasvalue ? static_cast<BlendType>(opaque.blendType_value) : BlendType::kAlpha,
          bool(opaque.invertedBlend),
          static_cast<AlphaTestType>(opaque.alphaTestType),
          opaque.alphaReferenceValue,
          opaque.displaceIn,
          opaque.displaceOut,
          pSubsurface ? toVector(pSubsurface->subsurfaceTransmittanceColor) : Vector3(.5f),
          pSubsurface ? pSubsurface->subsurfaceMeasurementDistance : 0.f,
          pSubsurface ? toVector(pSubsurface->subsurfaceSingleScatteringAlbedo) : Vector3(.5f),
          pSubsurface ? pSubsurface->subsurfaceVolumetricAnisotropy : 0.f,
          pSubsurface ? bool(pSubsurface->subsurfaceDiffusionProfile) : false,
          pSubsurface ? toVector(pSubsurface->subsurfaceRadius) : Vector3(.5f),
          pSubsurface ? pSubsurface->subsurfaceRadiusScale : 0.f,
          pSubsurface ? pSubsurface->subsurfaceMaxSampleRadius : 0.f,
          info.filterMode,
          info.wrapModeU,
          info.wrapModeV,
          bool(opaque.enableDlssControlMask),
          opaque.dlssControlMaskIntensity,
          opaque.dlssControlMaskToneStrength,
          opaque.dlssControlMaskStructuralStrength,
          0.f, 0.f, Vector2(1.f), Vector2(0.f), 0.f
        } };
      }
      if (ext.pTranslucent) {
        const auto& translucent = *ext.pTranslucent;
        requireFinite({ translucent.refractiveIndex, translucent.transmittanceMeasurementDistance });
        if (translucent.thinWallThickness_hasvalue) {
          requireFinite({ translucent.thinWallThickness_value });
        }
        return MaterialData { TranslucentMaterialData {
          textures.resolve(info.normalTexture),
          textures.resolve(translucent.transmittanceTexture),
          textures.resolve(info.emissiveTexture),
          translucent.refractiveIndex,
          toVector(translucent.transmittanceColor),
          translucent.transmittanceMeasurementDistance,
          info.emissiveIntensity > 0.f,
          info.emissiveIntensity,
          emissiveColor,
          info.spriteSheetRow,
          info.spriteSheetCol,
          info.spriteSheetFps,
          bool(translucent.thinWallThickness_hasvalue),
          translucent.thinWallThickness_hasvalue ? translucent.thinWallThickness_value : .001f,
          bool(translucent.useDiffuseLayer),
          info.filterMode,
          info.wrapModeU,
          info.wrapModeV,
          0.f, 0.f, Vector2(1.f), Vector2(0.f), 0.f
        } };
      }
      requireFinite({ ext.pPortal->rotationSpeed });
      return MaterialData { RayPortalMaterialData {
        textures.resolve(info.emissiveTexture), {},
        ext.pPortal->rayPortalIndex,
        info.spriteSheetRow,
        info.spriteSheetCol,
        info.spriteSheetFps,
        ext.pPortal->rotationSpeed,
        info.emissiveIntensity > 0.f,
        info.emissiveIntensity,
        info.filterMode,
        info.wrapModeU,
        info.wrapModeV
      } };
    }

    remixapi_MaterialHandle toEngineHandle(api::remixapi_MaterialHandle handle) {
      return reinterpret_cast<remixapi_MaterialHandle>(handle);
    }
  }

  struct ApiMaterials::Impl {
    static constexpr uint32_t kNoSlot = std::numeric_limits<uint32_t>::max();

    struct Slot {
      uint32_t generation = 1;
      uint32_t nextFree = kNoSlot;
      bool active = false;
    };

    RuntimeHost& host;
    const ApiTextures& textures;
    std::vector<Slot> slots;
    uint32_t firstFree = kNoSlot;

    Impl(RuntimeHost& value, const ApiTextures& inputTextures) : host(value), textures(inputTextures) { }

    AssetReplacer& assets() const {
      return *host.context()->getSceneManager().getAssetReplacer();
    }

    api::remixapi_MaterialHandle handleFor(uint32_t index) const {
      return reinterpret_cast<api::remixapi_MaterialHandle>(
        uint64_t(slots[index].generation) << 32 | uint64_t(index + 1));
    }

    uint32_t find(api::remixapi_MaterialHandle handle) const {
      const auto bits = reinterpret_cast<uintptr_t>(handle);
      const uint32_t index = uint32_t(bits) - 1;
      if (!uint32_t(bits) || index >= slots.size() || !slots[index].active
          || slots[index].generation != uint32_t(bits >> 32)) {
        invalid("Unknown or destroyed API material handle");
      }
      return index;
    }

    void free(uint32_t index) {
      auto& slot = slots[index];
      slot.active = false;
      if (slot.generation < std::numeric_limits<uint32_t>::max()) {
        ++slot.generation;
        slot.nextFree = firstFree;
        firstFree = index;
      }
    }
  };

  ApiMaterials::ApiMaterials(RuntimeHost& host, const ApiTextures& textures)
    : m_pImpl(std::make_unique<Impl>(host, textures)) { }

  ApiMaterials::~ApiMaterials() {
    auto& state = *m_pImpl;
    for (uint32_t index = 0; index < state.slots.size(); ++index) {
      if (state.slots[index].active) {
        state.assets().destroyExternalMaterial(toEngineHandle(state.handleFor(index)));
      }
    }
  }

  api016::remixapi_MaterialHandle ApiMaterials::create(const api016::remixapi_MaterialInfo& info) {
    auto& state = *m_pImpl;
    auto material = convert(info, state.textures);
    uint32_t index = state.firstFree;
    if (index == Impl::kNoSlot) {
      if (state.slots.size() == Impl::kNoSlot) {
        invalid("API material handle capacity exhausted");
      }
      index = uint32_t(state.slots.size());
      state.slots.emplace_back();
    } else {
      state.firstFree = state.slots[index].nextFree;
    }
    const auto handle = state.handleFor(index);
    try {
      state.assets().makeMaterialWithTexturePreload(*state.host.context(), toEngineHandle(handle), std::move(material));
    } catch (...) {
      state.free(index);
      throw;
    }
    state.slots[index].active = true;
    return handle;
  }

  void ApiMaterials::update(api016::remixapi_MaterialHandle handle, const api016::remixapi_MaterialInfo& info) {
    auto& state = *m_pImpl;
    state.find(handle);
    auto material = convert(info, state.textures);
    if (!state.assets().updateExternalMaterial(toEngineHandle(handle), std::move(material))) {
      invalid("API material is no longer registered in the renderer");
    }
  }

  void ApiMaterials::destroy(api016::remixapi_MaterialHandle handle) {
    auto& state = *m_pImpl;
    const auto index = state.find(handle);
    state.assets().destroyExternalMaterial(toEngineHandle(handle));
    state.free(index);
  }

  MaterialData ApiMaterials::resolve(api016::remixapi_MaterialHandle handle) const {
    const auto& state = *m_pImpl;
    state.find(handle);
    const auto* pMaterial = state.assets().accessExternalMaterial(toEngineHandle(handle));
    if (!pMaterial) {
      invalid("API material is no longer registered in the renderer");
    }
    return *pMaterial;
  }
}
