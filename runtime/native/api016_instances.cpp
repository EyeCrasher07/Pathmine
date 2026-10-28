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
#include <initializer_list>
#include <limits>
#include <utility>
#include <vector>

#include "api016_error.h"
#include "api016_extensions.h"
#include "api016_instances.h"
#include "api016_meshes.h"
#include "runtime_host.h"
#include "dxvk/rtx_render/rtx_context.h"

namespace dxvk::native {
  namespace {
    namespace api = api016;

    [[noreturn]] void invalid(const char* pMessage) {
      throw ApiError(api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, pMessage);
    }

    void finite(std::initializer_list<float> values) {
      for (const auto value : values) {
        if (!std::isfinite(value)) {
          invalid("Non-finite API instance parameter");
        }
      }
    }

    Vector3 toVector(const api::remixapi_Float3D& value) {
      finite({ value.x, value.y, value.z });
      return Vector3(value.x, value.y, value.z);
    }

    Matrix4 toMatrix(const api::remixapi_Transform& value) {
      const auto& m = value.matrix;
      for (const auto& row : m) {
        for (const auto item : row) {
          finite({ item });
        }
      }
      return Matrix4 {
        m[0][0], m[1][0], m[2][0], 0.f,
        m[0][1], m[1][1], m[2][1], 0.f,
        m[0][2], m[1][2], m[2][2], 0.f,
        m[0][3], m[1][3], m[2][3], 1.f
      };
    }

    std::vector<float> toAnimatedFloat1D(const api::remixapi_AnimatedFloat1D& animated) {
      if (animated.numberElements && !animated.pData) {
        invalid("Missing API particle curve values");
      }
      std::vector<float> result;
      result.reserve(animated.numberElements);
      for (uint32_t i = 0; i < animated.numberElements; ++i) {
        finite({ animated.pData[i] });
        result.push_back(animated.pData[i]);
      }
      return result;
    }

    std::vector<vec2> toAnimatedFloat2D(const api::remixapi_AnimatedFloat2D& animated) {
      if (animated.numberElements && !animated.pData) {
        invalid("Missing API particle curve values");
      }
      std::vector<vec2> result;
      result.reserve(animated.numberElements);
      for (uint32_t i = 0; i < animated.numberElements; ++i) {
        finite({ animated.pData[i].x, animated.pData[i].y });
        result.push_back(vec2(animated.pData[i].x, animated.pData[i].y));
      }
      return result;
    }

    std::vector<vec3> toAnimatedFloat3D(const api::remixapi_AnimatedFloat3D& animated) {
      if (animated.numberElements && !animated.pData) {
        invalid("Missing API particle curve values");
      }
      std::vector<vec3> result;
      result.reserve(animated.numberElements);
      for (uint32_t i = 0; i < animated.numberElements; ++i) {
        finite({ animated.pData[i].x, animated.pData[i].y, animated.pData[i].z });
        result.push_back(vec3(animated.pData[i].x, animated.pData[i].y, animated.pData[i].z));
      }
      return result;
    }

    std::vector<vec4> toAnimatedFloat4D(const api::remixapi_AnimatedFloat4D& animated) {
      if (animated.numberElements && !animated.pData) {
        invalid("Missing API particle curve values");
      }
      std::vector<vec4> result;
      result.reserve(animated.numberElements);
      for (uint32_t i = 0; i < animated.numberElements; ++i) {
        finite({ animated.pData[i].x, animated.pData[i].y, animated.pData[i].z, animated.pData[i].w });
        result.push_back(vec4(animated.pData[i].x, animated.pData[i].y, animated.pData[i].z, animated.pData[i].w));
      }
      return result;
    }
    CameraType::Enum categoryToCameraType(api::remixapi_InstanceCategoryFlags flags) {
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_VIEW_MODEL) {
        return CameraType::ViewModel;
      }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_SKY) {
        return CameraType::Sky;
      }
      return CameraType::Main;
    }

    CategoryFlags toRtCategories(api::remixapi_InstanceCategoryFlags flags) {
      CategoryFlags result { 0 };
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_WORLD_UI                 ){ result.set(InstanceCategories::WorldUI               ); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_WORLD_MATTE              ){ result.set(InstanceCategories::WorldMatte            ); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_SKY                      ){ result.set(InstanceCategories::Sky                   ); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_IGNORE                   ){ result.set(InstanceCategories::Ignore                ); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_IGNORE_LIGHTS            ){ result.set(InstanceCategories::IgnoreLights          ); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_IGNORE_ANTI_CULLING      ){ result.set(InstanceCategories::IgnoreAntiCulling     ); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_IGNORE_MOTION_BLUR       ){ result.set(InstanceCategories::IgnoreMotionBlur      ); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_IGNORE_OPACITY_MICROMAP  ){ result.set(InstanceCategories::IgnoreOpacityMicromap ); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_IGNORE_ALPHA_CHANNEL     ){ result.set(InstanceCategories::IgnoreAlphaChannel    ); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_HIDDEN                   ){ result.set(InstanceCategories::Hidden                ); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_PARTICLE                 ){ result.set(InstanceCategories::Particle              ); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_BEAM                     ){ result.set(InstanceCategories::Beam                  ); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_DECAL_STATIC             ){ result.set(InstanceCategories::DecalStatic           ); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_DECAL_DYNAMIC            ){ result.set(InstanceCategories::DecalDynamic          ); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_DECAL_SINGLE_OFFSET      ){ result.set(InstanceCategories::DecalSingleOffset     ); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_DECAL_NO_OFFSET          ){ result.set(InstanceCategories::DecalNoOffset         ); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_ALPHA_BLEND_TO_CUTOUT    ){ result.set(InstanceCategories::AlphaBlendToCutout    ); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_TERRAIN                  ){ result.set(InstanceCategories::Terrain               ); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_ANIMATED_WATER           ){ result.set(InstanceCategories::AnimatedWater         ); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_THIRD_PERSON_PLAYER_MODEL){ result.set(InstanceCategories::ThirdPersonPlayerModel); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_THIRD_PERSON_PLAYER_BODY ){ result.set(InstanceCategories::ThirdPersonPlayerBody ); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_IGNORE_BAKED_LIGHTING    ){ result.set(InstanceCategories::IgnoreBakedLighting   ); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_PARTICLE_EMITTER)         { result.set(InstanceCategories::ParticleEmitter); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_SMOOTH_NORMALS)            { result.set(InstanceCategories::SmoothNormals); }
      if (flags & api::REMIXAPI_INSTANCE_CATEGORY_BIT_HAIR_CARDS)                { result.set(InstanceCategories::HairCards); }

      static_assert((int)InstanceCategories::Count == 25, "Instance categories changed, please update Remix SDK");
      return result;
    }

    RtxParticleSystemDesc toParticles(const api::remixapi_InstanceInfoParticleSystemEXT& info) {
      RtxParticleSystemDesc desc;
      finite({ info.minTimeToLive, info.maxTimeToLive, info.initialVelocityFromNormal,
        info.initialVelocityConeAngleDegrees, info.initialVelocityFromMotion, info.turbulenceFrequency,
        info.turbulenceForce, info.motionTrailMultiplier, info.spawnRatePerSecond, info.collisionThickness,
        info.collisionRestitution, info.gravityForce, info.dragCoefficient, info.initialRotationDeviationDegrees,
        info.spawnBurstDuration, info.attractorRadius, info.attractorForce });
      if (info.billboardType > ParticleBillboardType::FaceWorldUp
          || info.spriteSheetMode > ParticleSpriteSheetMode::OverrideMaterial_Random
          || info.collisionMode > ParticleCollisionMode::Kill || info.randomFlipAxis > ParticleRandomFlipAxis::Both) {
        invalid("Invalid API particle enumeration");
      }
      desc.maxNumParticles = info.maxNumParticles;

      // Animated float conversions
      desc.minColor = toAnimatedFloat4D(info.minColor);
      desc.maxColor = toAnimatedFloat4D(info.maxColor);
      desc.minRotationSpeed = toAnimatedFloat1D(info.minRotationSpeed);
      desc.maxRotationSpeed = toAnimatedFloat1D(info.maxRotationSpeed);
      desc.minSize = toAnimatedFloat2D(info.minSize);
      desc.maxSize = toAnimatedFloat2D(info.maxSize);
      desc.maxVelocity = toAnimatedFloat3D(info.maxVelocity);

      desc.minTimeToLive = info.minTimeToLive;
      desc.maxTimeToLive = info.maxTimeToLive;
      desc.initialVelocityFromNormal = info.initialVelocityFromNormal;
      desc.initialVelocityConeAngleDegrees = info.initialVelocityConeAngleDegrees;
      desc.initialVelocityFromMotion = info.initialVelocityFromMotion;
      desc.turbulenceFrequency = info.turbulenceFrequency;
      desc.turbulenceForce = info.turbulenceForce;
      desc.motionTrailMultiplier = info.motionTrailMultiplier;
      desc.spawnRatePerSecond = info.spawnRatePerSecond;
      desc.collisionThickness = info.collisionThickness;
      desc.collisionRestitution = info.collisionRestitution;
      desc.gravityForce = info.gravityForce;
      desc.billboardType = static_cast<ParticleBillboardType>(info.billboardType);
      desc.hideEmitter = bool(info.hideEmitter);
      desc.enableMotionTrail = bool(info.enableMotionTrail);
      desc.useTurbulence = bool(info.useTurbulence);
      desc.alignParticlesToVelocity = bool(info.alignParticlesToVelocity);
      desc.useSpawnTexcoords = bool(info.useSpawnTexcoords);
      desc.enableCollisionDetection = bool(info.enableCollisionDetection);
      desc.dragCoefficient = info.dragCoefficient;
      desc.initialRotationDeviationDegrees = info.initialRotationDeviationDegrees;
      desc.spawnBurstDuration = info.spawnBurstDuration;
      desc.attractorRadius = info.attractorRadius;
      desc.attractorPosition = toVector(info.attractorPosition);
      desc.attractorForce = info.attractorForce;
      desc.spriteSheetMode = static_cast<ParticleSpriteSheetMode>(info.spriteSheetMode);
      desc.collisionMode = static_cast<ParticleCollisionMode>(info.collisionMode);
      desc.randomFlipAxis = static_cast<ParticleRandomFlipAxis>(info.randomFlipAxis);
      desc.restrictVelocityX = bool(info.restrictVelocityX);
      desc.restrictVelocityY = bool(info.restrictVelocityY);
      desc.restrictVelocityZ = bool(info.restrictVelocityZ);

      return desc;
    }


    void applyBlend(DrawCallState& state, const api::remixapi_InstanceInfoBlendEXT& blend) {
      if (blend.alphaTestCompareOp > VK_COMPARE_OP_ALWAYS || blend.srcColorBlendFactor > VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA
          || blend.dstColorBlendFactor > VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA
          || blend.srcAlphaBlendFactor > VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA
          || blend.dstAlphaBlendFactor > VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA
          || blend.colorBlendOp > VK_BLEND_OP_MAX || blend.alphaBlendOp > VK_BLEND_OP_MAX || (blend.writeMask & ~0xfu)
          || blend.textureColorArg1Source > uint32_t(RtTextureArgSource::TFactor)
          || blend.textureColorArg2Source > uint32_t(RtTextureArgSource::TFactor)
          || blend.textureAlphaArg1Source > uint32_t(RtTextureArgSource::TFactor)
          || blend.textureAlphaArg2Source > uint32_t(RtTextureArgSource::TFactor)
          || blend.textureColorOperation > uint32_t(DxvkRtTextureOperation::Force_Modulate2x)
          || blend.textureAlphaOperation > uint32_t(DxvkRtTextureOperation::Force_Modulate2x)) {
        invalid("Invalid API instance blend state");
      }
      auto& material = state.modifyMaterialData();
      material.alphaTestEnabled = bool(blend.alphaTestEnabled);
      material.alphaTestReferenceValue = blend.alphaTestReferenceValue;
      material.alphaTestCompareOp = static_cast<VkCompareOp>(blend.alphaTestCompareOp);
      material.blendMode.enableBlending = bool(blend.alphaBlendEnabled);
      material.textureColorOperation = static_cast<DxvkRtTextureOperation>(blend.textureColorOperation);
      material.textureColorArg1Source = static_cast<RtTextureArgSource>(blend.textureColorArg1Source);
      material.textureColorArg2Source = static_cast<RtTextureArgSource>(blend.textureColorArg2Source);
      material.textureAlphaOperation = static_cast<DxvkRtTextureOperation>(blend.textureAlphaOperation);
      material.textureAlphaArg1Source = static_cast<RtTextureArgSource>(blend.textureAlphaArg1Source);
      material.textureAlphaArg2Source = static_cast<RtTextureArgSource>(blend.textureAlphaArg2Source);
      material.tFactor = blend.tFactor;
      material.isTextureFactorBlend = bool(blend.isTextureFactorBlend);
      material.isVertexColorBakedLighting = bool(blend.isVertexColorBakedLighting);
      material.blendMode.colorSrcFactor = static_cast<VkBlendFactor>(blend.srcColorBlendFactor);
      material.blendMode.colorDstFactor = static_cast<VkBlendFactor>(blend.dstColorBlendFactor);
      material.blendMode.colorBlendOp = static_cast<VkBlendOp>(blend.colorBlendOp);
      material.blendMode.alphaSrcFactor = static_cast<VkBlendFactor>(blend.srcAlphaBlendFactor);
      material.blendMode.alphaDstFactor = static_cast<VkBlendFactor>(blend.dstAlphaBlendFactor);
      material.blendMode.alphaBlendOp = static_cast<VkBlendOp>(blend.alphaBlendOp);
      material.blendMode.writeMask = blend.writeMask;
    }

    std::unique_ptr<ExternalDrawState> convert(const api::remixapi_InstanceInfo& info, const ApiMeshes& meshes) {
      if (info.sType != api::REMIXAPI_STRUCT_TYPE_INSTANCE_INFO || (info.categoryFlags & ~0x7ffffffu)) {
        invalid("Invalid API instance description");
      }
      meshes.resolve(info.mesh);
      auto state = std::make_unique<ExternalDrawState>();
      state->mesh = reinterpret_cast<remixapi_MeshHandle>(info.mesh);
      state->cameraType = categoryToCameraType(info.categoryFlags);
      state->categories = toRtCategories(info.categoryFlags);
      state->doubleSided = bool(info.doubleSided);
      state->drawCall.cameraType = state->cameraType;
      state->drawCall.modifyCategoryFlags() = state->categories;
      state->drawCall.modifyTransformData().objectToWorld = toMatrix(info.transform);
      state->drawCall.modifyTransformData().texgenMode = TexGenMode::None;
      bool blendSeen = false;
      bool bonesSeen = false;
      bool pickingSeen = false;
      bool particlesSeen = false;
      bool instancingSeen = false;
      visitApiExtensions(info.pNext, [&](api::remixapi_StructType type, const void* pExtension) {
        switch (type) {
        case api::REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BLEND_EXT:
          if (std::exchange(blendSeen, true)) {
            invalid("Repeated API instance blend extension");
          }
          applyBlend(state->drawCall, *static_cast<const api::remixapi_InstanceInfoBlendEXT*>(pExtension));
          break;
        case api::REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BONE_TRANSFORMS_EXT: {
          if (std::exchange(bonesSeen, true)) {
            invalid("Repeated API instance bone extension");
          }
          const auto& ext = *static_cast<const api::remixapi_InstanceInfoBoneTransformsEXT*>(pExtension);
          const auto count = std::min(ext.boneTransforms_count, uint32_t(REMIXAPI_INSTANCE_INFO_MAX_BONES_COUNT));
          if (count && !ext.boneTransforms_values) {
            invalid("Missing API instance bone transforms");
          }
          auto& skinning = state->drawCall.modifySkinningState();
          skinning.minBoneIndex = 0;
          skinning.numBones = count;
          skinning.pBoneMatrices.reserve(count);
          for (uint32_t i = 0; i < count; ++i) {
            skinning.pBoneMatrices.push_back(toMatrix(ext.boneTransforms_values[i]));
          }
          skinning.computeHash();
          break;
        }
        case api::REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_OBJECT_PICKING_EXT:
          if (std::exchange(pickingSeen, true)) {
            invalid("Repeated API instance object-picking extension");
          }
          state->drawCall.drawCallID = static_cast<const api::remixapi_InstanceInfoObjectPickingEXT*>(pExtension)->objectPickingValue;
          break;
        case api::REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_PARTICLE_SYSTEM_EXT:
          if (std::exchange(particlesSeen, true)) {
            invalid("Repeated API instance particle extension");
          }
          state->optionalParticleDesc = toParticles(*static_cast<const api::remixapi_InstanceInfoParticleSystemEXT*>(pExtension));
          break;
        case api::REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_GPU_INSTANCING_EXT: {
          if (std::exchange(instancingSeen, true)) {
            invalid("Repeated API instance GPU-instancing extension");
          }
          const auto& ext = *static_cast<const api::remixapi_InstanceInfoGpuInstancingEXT*>(pExtension);
          if (ext.instanceTransforms_count && !ext.instanceTransforms_values) {
            invalid("Missing API instance GPU transforms");
          }
          state->gpuInstancingTransforms.reserve(ext.instanceTransforms_count);
          for (uint32_t i = 0; i < ext.instanceTransforms_count; ++i) {
            state->gpuInstancingTransforms.push_back(toMatrix(ext.instanceTransforms_values[i]));
          }
          break;
        }
        default:
          break;
        }
      });
      return state;
    }
  }

  struct ApiInstances::Impl {
    static constexpr uint32_t kNoSlot = std::numeric_limits<uint32_t>::max();
    struct Slot {
      uint32_t generation = 1;
      uint32_t nextFree = kNoSlot;
      bool active = false;
    };
    RuntimeHost& host;
    const ApiMeshes& meshes;
    std::vector<Slot> slots;
    uint32_t firstFree = kNoSlot;

    Impl(RuntimeHost& value, const ApiMeshes& inputMeshes) : host(value), meshes(inputMeshes) { }

    uint64_t idFor(uint32_t index) const {
      return uint64_t(slots[index].generation) << 32 | uint64_t(index + 1);
    }

    uint32_t find(api::remixapi_SceneInstanceHandle handle) const {
      const auto id = reinterpret_cast<uintptr_t>(handle);
      const uint32_t index = uint32_t(id) - 1;
      if (!uint32_t(id) || index >= slots.size() || !slots[index].active
          || slots[index].generation != uint32_t(id >> 32)) {
        invalid("Unknown or removed API scene instance");
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

    ExternalScene& scene() const { return host.context()->getSceneManager().getExternalScene(); }
  };

  ApiInstances::ApiInstances(RuntimeHost& host, const ApiMeshes& meshes) : m_pImpl(std::make_unique<Impl>(host, meshes)) { }

  ApiInstances::~ApiInstances() {
    auto& data = *m_pImpl;
    for (uint32_t i = 0; i < data.slots.size(); ++i) {
      if (data.slots[i].active) {
        data.scene().remove(data.idFor(i));
      }
    }
  }

  api016::remixapi_SceneInstanceHandle ApiInstances::add(const api016::remixapi_InstanceInfo& info) {
    auto& data = *m_pImpl;
    auto state = convert(info, data.meshes);
    uint32_t index = data.firstFree;
    if (index == Impl::kNoSlot) {
      if (data.slots.size() == Impl::kNoSlot) {
        invalid("API scene instance handle capacity exhausted");
      }
      index = uint32_t(data.slots.size());
      data.slots.emplace_back();
    } else {
      data.firstFree = data.slots[index].nextFree;
    }
    const uint64_t id = data.idFor(index);
    try {
      if (!data.scene().add(id, std::move(state))) {
        throw ApiError(api::REMIXAPI_ERROR_CODE_GENERAL_FAILURE, "Cannot register API scene instance");
      }
    } catch (...) {
      data.free(index);
      throw;
    }
    data.slots[index].active = true;
    return reinterpret_cast<api::remixapi_SceneInstanceHandle>(id);
  }

  void ApiInstances::update(api016::remixapi_SceneInstanceHandle handle, const api016::remixapi_InstanceInfo& info) {
    auto& data = *m_pImpl;
    data.find(handle);
    auto state = convert(info, data.meshes);
    if (!data.scene().update(reinterpret_cast<uintptr_t>(handle), std::move(state))) {
      invalid("API scene instance is no longer registered");
    }
  }

  void ApiInstances::remove(api016::remixapi_SceneInstanceHandle handle) {
    auto& data = *m_pImpl;
    const auto index = data.find(handle);
    if (!data.scene().remove(data.idFor(index))) {
      invalid("API scene instance is no longer registered");
    }
    data.free(index);
  }

  void ApiInstances::draw(const api016::remixapi_InstanceInfo& info) {
    auto& data = *m_pImpl;
    data.host.context()->commitExternalGeometryToRT(convert(info, data.meshes));
  }
}
