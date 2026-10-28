// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <iostream>
#include <stdexcept>

#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/tf/type.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>

int main(int argc, char** ppArgs) {
  try {
    if (argc != 2) {
      throw std::runtime_error("expected the native USD plugin directory");
    }
    auto& registry = pxr::PlugRegistry::GetInstance();
    registry.RegisterPlugins(ppArgs[1]);
    for (const auto* pName : { "RemixCategories", "Remix" }) {
      const auto plugin = registry.GetPluginWithName(pName);
      if (!plugin || !plugin->Load() || !plugin->IsLoaded()) {
        throw std::runtime_error("original Remix USD schema module did not load");
      }
    }
    for (const auto* pType : { "RemixCategoriesRemixInstanceCategoryAPI", "ParticleSystemAPI" }) {
      if (pxr::TfType::FindByName(pType).IsUnknown()) {
        throw std::runtime_error("native module did not register its original USD type");
      }
    }
    auto stage = pxr::UsdStage::CreateInMemory();
    auto prim = pxr::UsdGeomMesh::Define(stage, pxr::SdfPath("/World/Emitter")).GetPrim();
    const pxr::TfToken categories("RemixInstanceCategoryAPI");
    const pxr::TfToken particles("ParticleSystemAPI");
    if (!prim.ApplyAPI(categories) || !prim.ApplyAPI(particles)
        || !prim.HasAPI(categories) || !prim.HasAPI(particles)) {
      throw std::runtime_error("original Remix USD APIs could not be applied");
    }
    bool sky = true;
    const auto skyAttribute = prim.GetAttribute(pxr::TfToken("remix_category:sky"));
    pxr::VtArray<float> colorTimes;
    if (!skyAttribute || !skyAttribute.Get(&sky) || sky
        || !prim.GetAttribute(pxr::TfToken("primvars:particle:minColor:times")).Get(&colorTimes)
        || !colorTimes.empty()) {
      throw std::runtime_error("original generated schema property defaults are missing");
    }
    if (!skyAttribute.Set(true)) {
      throw std::runtime_error("original category schema property could not be authored");
    }
    std::string text;
    stage->GetRootLayer()->ExportToString(&text);
    auto layer = pxr::SdfLayer::CreateAnonymous("native-schema-roundtrip.usda");
    if (!layer->ImportFromString(text)) {
      throw std::runtime_error("original Remix schema stage could not be reloaded");
    }
    auto reloaded = pxr::UsdStage::Open(layer);
    const auto reloadedPrim = reloaded->GetPrimAtPath(prim.GetPath());
    sky = false;
    if (!reloadedPrim.HasAPI(categories) || !reloadedPrim.HasAPI(particles)
        || !reloadedPrim.GetAttribute(pxr::TfToken("remix_category:sky")).Get(&sky) || !sky) {
      throw std::runtime_error("original Remix schema values did not survive serialization");
    }
    std::cout << "PASS original native USD schema modules, type registration, defaults and round trip\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
