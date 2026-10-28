// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>

#include "api016_error.h"
#include "api016_materials.h"
#include "api016_textures.h"
#include "dxvk/pch/dxvk.pch.h"
#include "gl_output_reader.h"
#include "original_scene_fixture.h"
#include "runtime_host.h"

namespace {
  namespace api = dxvk::native::api016;
  using dxvk::native::ApiMaterials;
  using dxvk::native::ApiTextures;
  using dxvk::native::RuntimeHost;
  using dxvk::native::test::require;

  template<typename F>
  void expectInvalid(F&& action) {
    try {
      action();
    } catch (const dxvk::native::ApiError& error) {
      require(error.code() == api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS,
        "invalid API material operation returned the wrong error");
      return;
    }
    throw std::runtime_error("invalid API material operation succeeded");
  }

  api::remixapi_MaterialInfoOpaqueEXT opaqueInfo() {
    api::remixapi_MaterialInfoOpaqueEXT result { };
    result.sType = api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT;
    result.albedoConstant = { 1, 1, 1 };
    result.opacityConstant = 1;
    result.roughnessConstant = .5f;
    result.alphaTestType = 7;
    result.enableDlssControlMask = 1;
    result.dlssControlMaskIntensity = 1;
    result.dlssControlMaskToneStrength = .3f;
    result.dlssControlMaskStructuralStrength = .7f;
    return result;
  }

  api::remixapi_MaterialInfo materialInfo(void* pNext) {
    api::remixapi_MaterialInfo result { };
    result.sType = api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
    result.pNext = pNext;
    result.hash = 0x101010;
    result.emissiveColorConstant = { 1, 1, 1 };
    result.filterMode = 1;
    result.wrapModeU = 1;
    result.wrapModeV = 1;
    return result;
  }

  api::remixapi_TextureHandle texture(ApiTextures& textures, uint64_t hash, uint32_t pixel) {
    api::remixapi_TextureFromMemoryInfo info { };
    info.sType = api::REMIXAPI_STRUCT_TYPE_TEXTURE_FROM_MEMORY_INFO;
    info.hash = hash;
    info.width = info.height = 1;
    info.format = api::REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM;
    info.purpose = api::REMIXAPI_TEXTURE_PURPOSE_DATA;
    info.pixelData = reinterpret_cast<const uint8_t*>(&pixel);
    return textures.create(info);
  }

  void checkDescriptions(RuntimeHost& host, ApiTextures& textures, ApiMaterials& materials) {
    auto opaque = opaqueInfo();
    auto info = materialInfo(&opaque);
    std::array<api::remixapi_TextureHandle, 11> handles { };
    for (size_t index = 0; index < handles.size(); ++index) {
      handles[index] = texture(textures, 0x100 + index, 0xffffffff);
    }
    info.albedoTexture = handles[0];
    info.normalTexture = handles[1];
    info.tangentTexture = handles[2];
    info.emissiveTexture = handles[3];
    info.emissiveIntensity = 3;
    opaque.heightTexture = handles[4];
    opaque.roughnessTexture = handles[5];
    opaque.metallicTexture = handles[6];
    opaque.thinFilmThickness_hasvalue = 1;
    opaque.thinFilmThickness_value = 375;
    opaque.blendType_hasvalue = 1;
    opaque.blendType_value = 4;
    opaque.alphaTestType = 4;
    opaque.alphaReferenceValue = 127;
    opaque.displaceIn = .04f;
    opaque.displaceOut = .02f;
    api::remixapi_MaterialInfoOpaqueSubsurfaceEXT subsurface { };
    subsurface.sType = api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_SUBSURFACE_EXT;
    subsurface.subsurfaceTransmittanceTexture = handles[7];
    subsurface.subsurfaceThicknessTexture = handles[8];
    subsurface.subsurfaceSingleScatteringAlbedoTexture = handles[9];
    subsurface.subsurfaceRadiusTexture = handles[10];
    subsurface.subsurfaceTransmittanceColor = { .2f, .4f, .6f };
    subsurface.subsurfaceSingleScatteringAlbedo = { .7f, .8f, .9f };
    subsurface.subsurfaceRadius = { 1, 2, 3 };
    subsurface.subsurfaceRadiusScale = 2;
    subsurface.subsurfaceMeasurementDistance = .3f;
    subsurface.subsurfaceVolumetricAnisotropy = -.2f;
    subsurface.subsurfaceDiffusionProfile = 1;
    subsurface.subsurfaceMaxSampleRadius = 12;
    opaque.pNext = &subsurface;
    const auto handle = materials.create(info);
    const auto snapshot = materials.resolve(handle);
    const auto& data = snapshot.getOpaqueMaterialData();
    const std::array<dxvk::TextureRef, 11> references {{ data.getAlbedoOpacityTexture(),
      data.getNormalTexture(), data.getTangentTexture(), data.getEmissiveColorTexture(),
      data.getHeightTexture(), data.getRoughnessTexture(), data.getMetallicTexture(),
      data.getSubsurfaceTransmittanceTexture(), data.getSubsurfaceThicknessTexture(),
      data.getSubsurfaceSingleScatteringAlbedoTexture(), data.getSubsurfaceRadiusTexture() }};
    for (size_t index = 0; index < handles.size(); ++index) {
      require(references[index].getImageHash() == 0x100 + index,
        "API material lost or interchanged a PBR/subsurface texture");
      textures.destroy(handles[index]);
    }
    require(data.getThinFilmThicknessConstant() == 375 && data.getBlendType() == BlendType::kColorEmissive
        && data.getAlphaTestReferenceValue() == 127 && data.getDisplaceOut() == .02f
        && data.getSubsurfaceRadius().z == 3 && data.getSubsurfaceRadiusScale() == 2
        && data.getSubsurfaceDiffusionProfile() && data.getDLSSControlMaskToneStrength() == .3f,
      "API material lost thin-film, alpha, displacement, subsurface or DLSS controls");
    const auto originalHash = snapshot.getHash();
    opaque.albedoConstant = { 0, 0, 0 };
    subsurface.subsurfaceRadius = { 0, 0, 0 };
    require(materials.resolve(handle).getHash() == originalHash,
      "API material kept pointers into caller-owned extension data");
    expectInvalid([&] { materials.update(handle, info); });
    require(materials.resolve(handle).getHash() == originalHash,
      "failed API material update partially changed the registered material");
    materials.destroy(handle);
    require(!data.getAlbedoOpacityTexture().isImageEmpty(),
      "material snapshot did not retain a destroyed texture resource");
    expectInvalid([&] { materials.resolve(handle); });
    expectInvalid([&] { materials.destroy(handle); });

    opaque = opaqueInfo();
    info = materialInfo(&opaque);
    const auto replacement = materials.create(info);
    require(replacement != handle && uint32_t(reinterpret_cast<uintptr_t>(replacement))
        == uint32_t(reinterpret_cast<uintptr_t>(handle)),
      "reused API material slot did not advance its generation");
    expectInvalid([&] { materials.update(handle, info); });
    const auto duplicate = materials.create(info);
    require(duplicate != replacement, "API resident material handles were derived from the asset hash");
    info.hash = 0;
    const auto unhashed = materials.create(info);
    require(unhashed != nullptr, "GPU-resident material incorrectly required an asset hash");
    const auto engineHandle = reinterpret_cast<remixapi_MaterialHandle>(unhashed);
    require(host.context()->getSceneManager().getAssetReplacer()->accessExternalMaterial(engineHandle) != nullptr,
      "API material was not registered with the original renderer");
    materials.destroy(unhashed);
    require(host.context()->getSceneManager().getAssetReplacer()->accessExternalMaterial(engineHandle) == nullptr,
      "API material destruction left an external registry entry");
    materials.destroy(duplicate);
    materials.destroy(replacement);

    api::remixapi_MaterialInfoTranslucentEXT translucent { };
    translucent.sType = api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_TRANSLUCENT_EXT;
    translucent.refractiveIndex = 1.333f;
    translucent.transmittanceColor = { .7f, .8f, .9f };
    translucent.transmittanceMeasurementDistance = 2;
    translucent.thinWallThickness_hasvalue = 1;
    translucent.thinWallThickness_value = .05f;
    translucent.useDiffuseLayer = 1;
    translucent.transmittanceTexture = texture(textures, 0x200, 0xffffffff);
    info = materialInfo(&translucent);
    const auto glass = materials.create(info);
    const auto glassData = materials.resolve(glass).getTranslucentMaterialData();
    require(glassData.getRefractiveIndex() == 1.333f && glassData.getThinWallThickness() == .05f
        && glassData.getEnableThinWalled() && glassData.getEnableDiffuseLayer()
        && glassData.getTransmittanceTexture().getImageHash() == 0x200,
      "API translucent material lost refraction or transmission properties");
    textures.destroy(translucent.transmittanceTexture);
    materials.destroy(glass);

    api::remixapi_MaterialInfoPortalEXT portal { };
    portal.sType = api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_PORTAL_EXT;
    portal.rayPortalIndex = 2;
    portal.rotationSpeed = .5f;
    info = materialInfo(&portal);
    info.emissiveTexture = texture(textures, 0x201, 0xffffffff);
    info.emissiveIntensity = 2;
    const auto portalHandle = materials.create(info);
    const auto portalData = materials.resolve(portalHandle).getRayPortalMaterialData();
    require(portalData.getRayPortalIndex() == 2 && portalData.getRotationSpeed() == .5f
        && portalData.getMaskTexture().getImageHash() == 0x201,
      "API portal material lost its mask, pair index or animation");
    textures.destroy(info.emissiveTexture);
    materials.destroy(portalHandle);
  }

  void checkBadInputs(ApiMaterials& materials) {
    auto opaque = opaqueInfo();
    auto info = materialInfo(&opaque);
    info.sType = api::REMIXAPI_STRUCT_TYPE_MESH_INFO;
    expectInvalid([&] { materials.create(info); });
    info = materialInfo(nullptr);
    expectInvalid([&] { materials.create(info); });
    info.pNext = &opaque;
    opaque.pNext = &opaque;
    expectInvalid([&] { materials.create(info); });
    auto second = opaqueInfo();
    opaque.pNext = &second;
    expectInvalid([&] { materials.create(info); });
    opaque.pNext = nullptr;
    opaque.albedoConstant.x = std::numeric_limits<float>::quiet_NaN();
    expectInvalid([&] { materials.create(info); });
    opaque = opaqueInfo();
    opaque.blendType_hasvalue = 1;
    opaque.blendType_value = 256;
    expectInvalid([&] { materials.create(info); });
    expectInvalid([&] { materials.resolve(nullptr); });
  }

  std::vector<uint8_t> render(RuntimeHost& host, dxvk::native::test::Reader& reader, remixapi_MeshHandle mesh) {
    std::vector<uint8_t> pixels;
    for (uint32_t frame = 0; frame < 4; ++frame) {
      require(host.beginFrame({ 96, 64, 1 }), "API material test frame was skipped");
      const dxvk::Matrix4 projection(dxvk::Vector4(1.1547005f, 0, 0, 0), dxvk::Vector4(0, 1.7320508f, 0, 0),
        dxvk::Vector4(0, 0, 1.001001f, 1), dxvk::Vector4(0, 0, -.1001001f, 0));
      host.context()->getSceneManager().getCameraManager().processExternalCamera(
        dxvk::CameraType::Main, dxvk::Matrix4(), projection);
      auto state = std::make_unique<dxvk::ExternalDrawState>();
      state->mesh = mesh;
      state->cameraType = dxvk::CameraType::Main;
      state->drawCall.cameraType = dxvk::CameraType::Main;
      state->doubleSided = true;
      host.context()->commitExternalGeometryToRT(std::move(state));
      host.submit();
      pixels = reader.readAndPresent(host);
    }
    return pixels;
  }

  size_t countColor(const std::vector<uint8_t>& pixels, size_t color) {
    size_t result = 0;
    for (size_t offset = 0; offset < pixels.size(); offset += 4) {
      if (pixels[offset + color] > 180
          && pixels[offset + color] > pixels[offset + (color + 1) % 3] + 20
          && pixels[offset + color] > pixels[offset + (color + 2) % 3] + 20) {
        ++result;
      }
    }
    return result;
  }

  void checkSimultaneousMaterials(RuntimeHost& host, ApiTextures& textures, ApiMaterials& materials,
      dxvk::native::test::Reader& reader) {
    auto opaque = opaqueInfo();
    auto info = materialInfo(&opaque);
    info.emissiveIntensity = 4;
    const auto redTexture = texture(textures, 0x320, 0xff0000ff);
    info.albedoTexture = info.emissiveTexture = redTexture;
    const auto redMaterial = materials.create(info);
    textures.destroy(redTexture);
    const auto greenTexture = texture(textures, 0x320, 0xff00ff00);
    info.albedoTexture = info.emissiveTexture = greenTexture;
    const auto greenMaterial = materials.create(info);
    textures.destroy(greenTexture);
    const auto red = materials.resolve(redMaterial);
    const auto green = materials.resolve(greenMaterial);
    require(red.getHash() == green.getHash() && red.getTextureResourceHash() != green.getTextureResourceHash(),
      "API material GPU resource identity changed its asset identity or reused a retired image");
    const std::array<remixapi_MeshHandle, 2> meshes {{
      reinterpret_cast<remixapi_MeshHandle>(uintptr_t(0x521)),
      reinterpret_cast<remixapi_MeshHandle>(uintptr_t(0x522)) }};
    const std::array<api::remixapi_MaterialHandle, 2> handles {{ redMaterial, greenMaterial }};
    auto& scene = host.context()->getSceneManager();
    for (size_t index = 0; index < meshes.size(); ++index) {
      std::vector<dxvk::RasterGeometry> geometry;
      geometry.push_back(dxvk::native::test::createTriangle(host.device(),
        reinterpret_cast<remixapi_MaterialHandle>(handles[index])));
      scene.getAssetReplacer()->registerExternalMesh(meshes[index], std::move(geometry));
    }
    std::vector<uint8_t> pixels;
    for (uint32_t frame = 0; frame < 4; ++frame) {
      require(host.beginFrame({ 96, 64, 1 }), "simultaneous API material frame was skipped");
      const dxvk::Matrix4 projection(dxvk::Vector4(1.1547005f, 0, 0, 0), dxvk::Vector4(0, 1.7320508f, 0, 0),
        dxvk::Vector4(0, 0, 1.001001f, 1), dxvk::Vector4(0, 0, -.1001001f, 0));
      scene.getCameraManager().processExternalCamera(dxvk::CameraType::Main, dxvk::Matrix4(), projection);
      for (size_t index = 0; index < meshes.size(); ++index) {
        auto state = std::make_unique<dxvk::ExternalDrawState>();
        state->mesh = meshes[index];
        state->cameraType = dxvk::CameraType::Main;
        state->drawCall.cameraType = dxvk::CameraType::Main;
        state->doubleSided = true;
        auto& transform = state->drawCall.modifyTransformData().objectToWorld;
        transform[0][0] = transform[1][1] = .4f;
        transform[3][0] = index ? .7f : -.7f;
        host.context()->commitExternalGeometryToRT(std::move(state));
      }
      host.submit();
      pixels = reader.readAndPresent(host);
    }
    require(countColor(pixels, 0) > 50 && countColor(pixels, 1) > 50,
      "same-frame material cache aliased two retained GPU images with the same asset hash");
    for (size_t index = 0; index < meshes.size(); ++index) {
      scene.destroyExternalMesh(host.context(), meshes[index]);
      materials.destroy(handles[index]);
    }
  }

  void checkMaterialFrames(RuntimeHost& host, ApiTextures& textures, ApiMaterials& materials,
      dxvk::native::test::Reader& reader) {
    auto opaque = opaqueInfo();
    auto info = materialInfo(&opaque);
    info.emissiveIntensity = 4;
    auto tex = texture(textures, 0x300, 0xff0000ff);
    info.albedoTexture = info.emissiveTexture = tex;
    const auto material = materials.create(info);
    textures.destroy(tex);
    const auto mesh = reinterpret_cast<remixapi_MeshHandle>(uintptr_t(0x500));
    std::vector<dxvk::RasterGeometry> geometry;
    geometry.push_back(dxvk::native::test::createTriangle(host.device(),
      reinterpret_cast<remixapi_MaterialHandle>(material)));
    auto& scene = host.context()->getSceneManager();
    scene.getAssetReplacer()->registerExternalMesh(mesh, std::move(geometry));
    for (size_t color = 0; color < 3; ++color) {
      if (color) {
        // Asset hashes may be reused while the material still retains the previous image.
        tex = texture(textures, 0x300, 0xff000000 | (0xff << (8 * color)));
        info.albedoTexture = info.emissiveTexture = tex;
        materials.update(material, info);
        textures.destroy(tex);
      }
      const auto pixels = render(host, reader, mesh);
      const auto coloredPixels = countColor(pixels, color);
      std::cout << "API material RGB counts: " << countColor(pixels, 0) << ", "
        << countColor(pixels, 1) << ", " << countColor(pixels, 2) << '\n';
      require(coloredPixels > 100, "API material update did not change the original path-traced surface");
      std::cout << "API material channel " << color << ": " << coloredPixels << " pixels\n";
    }

    api::remixapi_MaterialInfoTranslucentEXT glass { };
    glass.sType = api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_TRANSLUCENT_EXT;
    glass.refractiveIndex = 1.333f;
    glass.transmittanceColor = { 1, 1, 1 };
    glass.transmittanceMeasurementDistance = 1;
    glass.thinWallThickness_hasvalue = 1;
    glass.thinWallThickness_value = .01f;
    info = materialInfo(&glass);
    info.emissiveIntensity = 4;
    info.emissiveColorConstant = { 0, 0, 1 };
    materials.update(material, info);
    const auto glassPixels = render(host, reader, mesh);
    require(countColor(glassPixels, 2) > 100,
      "changing an API material from opaque to translucent lost its rendered emission");
    bool sawGlass = false;
    for (const auto* pInstance : scene.getInstanceTable()) {
      sawGlass |= pInstance && pInstance->getMaterialType() == dxvk::MaterialDataType::Translucent;
    }
    require(sawGlass, "material kind update did not reach the original instance manager");

    info = materialInfo(&opaque);
    info.emissiveIntensity = 4;
    info.emissiveColorConstant = { 1, 0, 0 };
    materials.update(material, info);
    const auto restoredPixels = render(host, reader, mesh);
    std::cout << "Restored opaque RGB counts: " << countColor(restoredPixels, 0) << ", "
      << countColor(restoredPixels, 1) << ", " << countColor(restoredPixels, 2) << '\n';
    require(countColor(restoredPixels, 0) > 100,
      "changing an API translucent material back to opaque left stale surface state");

    tex = texture(textures, 0x310, 0x00ffffff);
    info.albedoTexture = tex;
    opaque.alphaTestType = 4;
    opaque.alphaReferenceValue = 127;
    materials.update(material, info);
    textures.destroy(tex);
    require(countColor(render(host, reader, mesh), 0) < 4,
      "API material alpha-test update left the previous opaque surface visible");
    scene.destroyExternalMesh(host.context(), mesh);
    materials.destroy(material);
  }

  void checkGlassTransmission(RuntimeHost& host, ApiTextures& textures, ApiMaterials& materials,
      dxvk::native::test::Reader& reader) {
    auto opaque = opaqueInfo();
    auto backingInfo = materialInfo(&opaque);
    backingInfo.emissiveIntensity = .25f;
    const auto backing = materials.create(backingInfo);
    api::remixapi_MaterialInfoTranslucentEXT glass { };
    glass.sType = api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_TRANSLUCENT_EXT;
    glass.refractiveIndex = 1.52f;
    glass.transmittanceColor = { .97f, .97f, .97f };
    glass.transmittanceMeasurementDistance = .35f;
    auto info = materialInfo(&glass);
    const auto material = materials.create(info);
    auto& scene = host.context()->getSceneManager();
    const std::array<remixapi_MeshHandle, 3> meshes {{
      reinterpret_cast<remixapi_MeshHandle>(uintptr_t(0x530)),
      reinterpret_cast<remixapi_MeshHandle>(uintptr_t(0x531)),
      reinterpret_cast<remixapi_MeshHandle>(uintptr_t(0x532)) }};
    for (size_t i = 0; i < meshes.size(); ++i) {
      std::vector<dxvk::RasterGeometry> geometry;
      geometry.push_back(dxvk::native::test::createTriangle(host.device(),
        reinterpret_cast<remixapi_MaterialHandle>(i == 2 ? backing : material)));
      scene.getAssetReplacer()->registerExternalMesh(meshes[i], std::move(geometry));
    }
    const std::array<uint32_t, 2> colors {{ 0x4d2020e0, 0x4de02020 }};
    bool distinctTransmission = true;
    for (size_t mode = 0; mode < 4; ++mode) {
      const size_t color = mode % colors.size();
      glass.thinWallThickness_hasvalue = mode >= 2;
      glass.thinWallThickness_value = 1;
      glass.transmittanceTexture = texture(textures, 0x340, colors[color]);
      info.albedoTexture = glass.transmittanceTexture;
      materials.update(material, info);
      textures.destroy(glass.transmittanceTexture);
      std::vector<uint8_t> pixels;
      for (uint32_t frame = 0; frame < 8; ++frame) {
        require(host.beginFrame({ 96, 64, 1 }), "glass transmission frame was skipped");
        const dxvk::Matrix4 projection(dxvk::Vector4(1.1547005f, 0, 0, 0), dxvk::Vector4(0, 1.7320508f, 0, 0),
          dxvk::Vector4(0, 0, 1.001001f, 1), dxvk::Vector4(0, 0, -.1001001f, 0));
        scene.getCameraManager().processExternalCamera(dxvk::CameraType::Main, dxvk::Matrix4(), projection);
        for (size_t i = 0; i < meshes.size(); ++i) {
          auto state = std::make_unique<dxvk::ExternalDrawState>();
          state->mesh = meshes[i];
          state->cameraType = state->drawCall.cameraType = dxvk::CameraType::Main;
          state->doubleSided = true;
          auto& transform = state->drawCall.modifyTransformData().objectToWorld;
          if (i == 1) {
            transform[2][2] = -1;
            transform[3][2] = 6.35f;
          } else if (i == 2) {
            transform[0][0] = transform[1][1] = 2;
            transform[3][2] = 1;
          }
          host.context()->commitExternalGeometryToRT(std::move(state));
        }
        host.submit();
        pixels = reader.readAndPresent(host);
      }
      std::array<double, 3> average { };
      for (uint32_t y = 26; y < 38; ++y) {
        for (uint32_t x = 42; x < 54; ++x) {
          for (size_t channel = 0; channel < average.size(); ++channel) {
            average[channel] += pixels[(y * 96 + x) * 4 + channel] / 144.0;
          }
        }
      }
      std::cout << "Glass transmission mode " << mode << ": " << average[0] << ", " << average[1]
        << ", " << average[2] << '\n';
      const size_t channel = color ? 2 : 0;
      distinctTransmission &= average[channel] > average[(channel + 1) % 3] + 20
          && average[channel] > average[(channel + 2) % 3] + 20;
    }
    for (auto mesh : meshes) {
      scene.destroyExternalMesh(host.context(), mesh);
    }
    materials.destroy(material);
    materials.destroy(backing);
    require(distinctTransmission, "glass textures lost their distinct transmission colors");
  }
}

int main() {
  if (!glfwInit()) {
    return 77;
  }
  dxvk::native::test::Window window;
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  window.pHandle = glfwCreateWindow(128, 96, "Original Remix API material verification", nullptr, nullptr);
  if (!window.pHandle) {
    return 77;
  }
  glfwMakeContextCurrent(window.pHandle);
  try {
    RuntimeHost host(std::filesystem::current_path().u8string());
    dxvk::native::test::Reader reader(host.interopInfo());
    ApiTextures textures(host);
    ApiMaterials materials(host, textures);
    checkDescriptions(host, textures, materials);
    checkBadInputs(materials);
    checkSimultaneousMaterials(host, textures, materials, reader);
    checkMaterialFrames(host, textures, materials, reader);
    checkGlassTransmission(host, textures, materials, reader);
    std::cout << "PASS original API material ownership, generations, PBR/subsurface/transmission, updates and alpha testing\n";
    return 0;
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "FAIL " << error.message() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
  }
  return 1;
}
