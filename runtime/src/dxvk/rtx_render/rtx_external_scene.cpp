// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <unordered_map>
#include <unordered_set>

#include "../dxvk_device.h"
#include "rtx_external_scene.h"
#include "rtx_scene_manager.h"
#include "rtx_option_manager.h"
#include "rtx_options.h"
#include "rtx_texture_manager.h"

namespace dxvk {
  struct ExternalScene::Impl {
    struct Entry {
      std::unique_ptr<ExternalDrawState> state;
      std::unique_ptr<ReplacementInstance> instance;
      XXH64_hash_t materialHash = 0;
      uint64_t materialGeneration = 0;
      bool dirty = true;
    };

    SceneManager& scene;
    std::unordered_map<uint64_t, Entry> entries;
    std::unordered_map<remixapi_MeshHandle, std::unordered_set<uint64_t>> meshes;

    explicit Impl(SceneManager& value) : scene(value) { }

    void unlinkMesh(remixapi_MeshHandle mesh, uint64_t id) {
      const auto found = meshes.find(mesh);
      if (found != meshes.end()) {
        found->second.erase(id);
        if (found->second.empty()) {
          meshes.erase(found);
        }
      }
    }

    static XXH64_hash_t materialHash(const AssetReplacer& assets, const std::vector<RasterGeometry>& surfaces) {
      XXH64_hash_t hash = 0;
      for (const auto& surface : surfaces) {
        const auto* pMaterial = assets.accessExternalMaterial(surface.externalMaterial);
        const auto material = pMaterial ? pMaterial->getHash() : 0;
        const auto resources = pMaterial ? pMaterial->getTextureResourceHash() : 0;
        hash = XXH64(&material, sizeof(material), hash);
        hash = XXH64(&resources, sizeof(resources), hash);
      }
      return hash;
    }
  };

  ExternalScene::ExternalScene(SceneManager& scene) : m_pImpl(std::make_unique<Impl>(scene)) { }
  ExternalScene::~ExternalScene() = default;

  bool ExternalScene::add(uint64_t id, std::unique_ptr<ExternalDrawState> state) {
    auto& data = *m_pImpl;
    if (!id || !state || data.entries.count(id)) {
      return false;
    }
    const auto mesh = state->mesh;
    auto [entry, inserted] = data.entries.try_emplace(id);
    try {
      data.meshes[mesh].insert(id);
    } catch (...) {
      data.entries.erase(entry);
      const auto bucket = data.meshes.find(mesh);
      if (bucket != data.meshes.end() && bucket->second.empty()) {
        data.meshes.erase(bucket);
      }
      throw;
    }
    entry->second.state = std::move(state);
    return true;
  }

  bool ExternalScene::update(uint64_t id, std::unique_ptr<ExternalDrawState> state) {
    auto& data = *m_pImpl;
    const auto found = data.entries.find(id);
    if (!state || found == data.entries.end()) {
      return false;
    }
    auto& entry = found->second;
    if (entry.state->mesh != state->mesh) {
      data.meshes[state->mesh].insert(id);
      data.unlinkMesh(entry.state->mesh, id);
    }
    entry.state = std::move(state);
    entry.dirty = true;
    return true;
  }

  bool ExternalScene::remove(uint64_t id) {
    auto& data = *m_pImpl;
    const auto found = data.entries.find(id);
    if (found == data.entries.end()) {
      return false;
    }
    data.unlinkMesh(found->second.state->mesh, id);
    data.entries.erase(found);
    return true;
  }

  void ExternalScene::invalidateMesh(remixapi_MeshHandle mesh) {
    auto& data = *m_pImpl;
    const auto found = data.meshes.find(mesh);
    if (found == data.meshes.end()) {
      return;
    }
    for (const auto id : found->second) {
      auto& entry = data.entries.at(id);
      entry.instance.reset();
      entry.state->drawCall.overrideGeometryData(nullptr);
      entry.dirty = true;
    }
  }

  void ExternalScene::reset() {
    for (auto& [id, entry] : m_pImpl->entries) {
      entry.instance.reset();
      entry.dirty = true;
    }
  }

  void ExternalScene::submit(const Rc<DxvkContext>& ctx) {
    ScopedCpuProfileZone();
    auto& data = *m_pImpl;
    auto& scene = data.scene;
    auto& assets = *scene.m_pReplacer;
    const uint32_t frame = scene.device()->getCurrentFrameId();
    const auto generation = assets.getExternalMaterialGeneration();
    const bool texturesValid = scene.device()->getCommon()->getTextureManager().getTextureCacheGeneration()
      == scene.m_textureCacheGenerationValidForPreserve;
    for (auto& [id, entry] : data.entries) {
      const auto surfaces = assets.accessExternalMesh(entry.state->mesh);
      if (surfaces->empty()) {
        continue;
      }
      if (entry.dirty || entry.materialGeneration != generation) {
        const auto hash = Impl::materialHash(assets, *surfaces);
        entry.dirty |= hash != entry.materialHash;
        entry.materialHash = hash;
        entry.materialGeneration = generation;
      }
      if (!entry.instance) {
        const auto& transform = entry.state->drawCall.getTransformData().objectToWorld;
        const ReplacementInstance::LookupKey key { id, 0, 0, 0, transform[3].xyz(), transform };
        entry.instance = std::make_unique<ReplacementInstance>(key, uint32_t(id), frame);
      }
      auto& instance = *entry.instance;
      bool preserve = RtxOptions::enablePreservePath() && !entry.dirty && texturesValid
        && instance.prims.size() == surfaces->size() && !RtxOptionManager::isDrawcallTranslationInvalid()
        && !entry.state->optionalParticleDesc
        && !instance.dirtyFlags.test(ReplacementInstance::DirtyFlag::ParticleSystem)
        && !entry.state->categories.test(InstanceCategories::ParticleEmitter)
        && !RtxOptions::shouldConvertToLight(entry.state->drawCall.getMaterialData().getHash());
      for (const auto& prim : instance.prims) {
        const auto* pInstance = prim.getInstance();
        preserve &= !pInstance || !pInstance->getBlas() || pInstance->getBlas()->frameLastUpdated != frame;
      }
      scene.prepareExternalDraw(*entry.state);
      scene.processExternalDraw(ctx, *entry.state, instance, preserve);
      instance.dirtyFlags.clr(ReplacementInstance::kLookupDriftMask);
      entry.dirty = false;
    }
  }
}
