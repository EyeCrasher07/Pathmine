// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

#include <glm/gtc/packing.hpp>

#include "api016_cameras.h"
#include "api016_error.h"
#include "api016_instances.h"
#include "api016_lights.h"
#include "api016_materials.h"
#include "api016_meshes.h"
#include "api016_options.h"
#include "api016_textures.h"
#include "dxvk/pch/dxvk.pch.h"
#include "dxvk/rtx_render/rtx_texture_manager.h"
#include "gl_output_reader.h"
#include "runtime_host.h"

namespace {
  namespace api = dxvk::native::api016;
  using dxvk::native::ApiLights;
  using dxvk::native::RuntimeHost;
  using dxvk::native::test::require;

  template<typename F>
  void expectInvalid(F&& action) {
    try {
      action();
    } catch (const dxvk::native::ApiError& error) {
      require(error.code() == api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, "wrong API light validation error");
      return;
    }
    throw std::runtime_error("invalid API light request succeeded");
  }

  api::remixapi_Transform transform() {
    api::remixapi_Transform value { };
    value.matrix[0][0] = value.matrix[1][1] = value.matrix[2][2] = 1;
    return value;
  }

  api::remixapi_LightInfo lightInfo(void* pShape, uint64_t hash = 0x701) {
    api::remixapi_LightInfo info { };
    info.sType = api::REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
    info.pNext = pShape;
    info.hash = hash;
    info.radiance = { 0, 40, 0 };
    return info;
  }

  api::remixapi_LightInfoSphereEXT sphere() {
    api::remixapi_LightInfoSphereEXT value { };
    value.sType = api::REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
    value.position = { 0, 0, 1 };
    value.radius = .5f;
    value.volumetricRadianceScale = 1;
    return value;
  }

  const dxvk::RtLight& resolve(RuntimeHost& host, api::remixapi_LightHandle handle) {
    return host.context()->getSceneManager().getLightManager().getExternalLightTable()
      .at(reinterpret_cast<remixapi_LightHandle>(handle));
  }

  struct Frames {
    RuntimeHost& host;
    dxvk::native::ApiCameras& cameras;
    dxvk::native::test::Reader& reader;
    std::vector<uint8_t> pixels;

    void stepLayers(ApiLights& lights, const std::vector<api::remixapi_LightHandle>& handles, uint32_t count = 1) {
      for (uint32_t frame = 0; frame < count; ++frame) {
        require(host.beginFrame({ 96, 64, 1 }), "cannot begin light frame");
        api::remixapi_CameraInfoParameterizedEXT parameters { };
        parameters.sType = api::REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT;
        parameters.forward = { 0, 0, 1 };
        parameters.up = { 0, 1, 0 };
        parameters.right = { 1, 0, 0 };
        parameters.fovYInDegrees = 60;
        parameters.aspect = 1.5f;
        parameters.nearPlane = .05f;
        parameters.farPlane = 100;
        api::remixapi_CameraInfo info { };
        info.sType = api::REMIXAPI_STRUCT_TYPE_CAMERA_INFO;
        info.pNext = &parameters;
        cameras.setup(info);
        for (const auto handle : handles) {
          if (handle) {
            lights.draw(handle);
            lights.draw(handle);
          }
        }
        host.submit();
        pixels = reader.readAndPresent(host);
      }
    }

    void step(ApiLights& lights, api::remixapi_LightHandle handle = nullptr, uint32_t count = 1) {
      stepLayers(lights, { handle }, count);
    }

    size_t greenPixels() const {
      size_t count = 0;
      for (size_t index = 0; index < pixels.size(); index += 4) {
        count += pixels[index + 1] > 45 && pixels[index + 1] > 2 * unsigned(pixels[index])
          && pixels[index + 1] > 2 * unsigned(pixels[index + 2]);
      }
      return count;
    }

    void save(const char* pPath) const {
      std::ofstream file(pPath, std::ios::binary);
      file << "P6\n96 64\n255\n";
      for (size_t index = 0; index < pixels.size(); index += 4) {
        file.write(reinterpret_cast<const char*>(pixels.data() + index), 3);
      }
      require(bool(file), "cannot save API light capture");
    }
  };

  api::remixapi_SceneInstanceHandle plane(dxvk::native::ApiMaterials& materials, dxvk::native::ApiMeshes& meshes,
      dxvk::native::ApiInstances& instances) {
    api::remixapi_MaterialInfoOpaqueEXT opaque { };
    opaque.sType = api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT;
    opaque.albedoConstant = { .6f, .6f, .6f };
    opaque.opacityConstant = 1;
    opaque.roughnessConstant = .8f;
    opaque.alphaTestType = 7;
    api::remixapi_MaterialInfo material { };
    material.sType = api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
    material.pNext = &opaque;
    material.hash = 0x700;
    material.spriteSheetCol = material.spriteSheetRow = 1;
    material.wrapModeU = material.wrapModeV = 1;
    const auto mat = materials.create(material);
    std::array<api::remixapi_HardcodedVertex, 6> vertices { };
    const float positions[6][3] = { { -2, -1.4f, 3 }, { 2, -1.4f, 3 }, { 2, 1.4f, 3 },
      { -2, -1.4f, 3 }, { 2, 1.4f, 3 }, { -2, 1.4f, 3 } };
    for (size_t index = 0; index < vertices.size(); ++index) {
      std::copy_n(positions[index], 3, vertices[index].position);
      vertices[index].normal[2] = -1;
      vertices[index].color = 0xffffffff;
    }
    api::remixapi_MeshInfoSurfaceTriangles surface { };
    surface.material = mat;
    surface.vertices_values = vertices.data();
    surface.vertices_count = vertices.size();
    api::remixapi_MeshInfo mesh { };
    mesh.sType = api::REMIXAPI_STRUCT_TYPE_MESH_INFO;
    mesh.surfaces_values = &surface;
    mesh.surfaces_count = 1;
    mesh.hash = 0x700;
    api::remixapi_InstanceInfo instance { };
    instance.sType = api::REMIXAPI_STRUCT_TYPE_INSTANCE_INFO;
    instance.mesh = meshes.create(mesh);
    instance.transform = transform();
    instance.doubleSided = 1;
    return instances.add(instance);
  }

  void badInputs(ApiLights& lights) {
    auto shape = sphere();
    auto info = lightInfo(&shape);
    info.sType = api::REMIXAPI_STRUCT_TYPE_MESH_INFO;
    expectInvalid([&] { lights.create(info); });
    info = lightInfo(&shape, 0);
    expectInvalid([&] { lights.create(info); });
    info = lightInfo(nullptr);
    expectInvalid([&] { lights.create(info); });
    info = lightInfo(&shape);
    shape.radius = -1;
    expectInvalid([&] { lights.create(info); });
    shape = sphere();
    shape.position.x = std::numeric_limits<float>::quiet_NaN();
    expectInvalid([&] { lights.create(info); });
    shape = sphere();
    info.radiance.y = std::numeric_limits<float>::infinity();
    expectInvalid([&] { lights.create(info); });
    info.radiance = { 0, -1, 0 };
    expectInvalid([&] { lights.create(info); });
    info.radiance = { 0, 40, 0 };
    shape.shaping_hasvalue = 1;
    shape.shaping_value.direction = { 0, 0, 2 };
    expectInvalid([&] { lights.create(info); });
    shape = sphere();
    shape.volumetricRadianceScale = -1;
    expectInvalid([&] { lights.create(info); });
    shape = sphere();
    shape.pNext = &shape;
    expectInvalid([&] { lights.create(info); });
    shape.pNext = nullptr;
    auto duplicate = shape;
    shape.pNext = &duplicate;
    expectInvalid([&] { lights.create(info); });
    expectInvalid([&] { lights.draw(nullptr); });
    expectInvalid([&] { lights.destroy(reinterpret_cast<api::remixapi_LightHandle>(0xbad)); });
  }

  void analytical(RuntimeHost& host, ApiLights& lights, Frames& frames) {
    auto& manager = host.context()->getSceneManager().getLightManager();
    auto check = [&](api::remixapi_LightInfo info, dxvk::RtLightType type, const char* pCapture) {
      const auto handle = lights.create(info);
      require(reinterpret_cast<uintptr_t>(handle) == info.hash, "API light hash did not identify its resource");
      require(resolve(host, handle).getType() == type, "API light reached the wrong original light class");
      frames.step(lights, handle, 10);
      frames.save(pCapture);
      std::cout << pCapture << ": green=" << frames.greenPixels() << '\n';
      require(manager.getActiveCount() == 1, "duplicate API light draws created multiple GPU lights");
      require(frames.greenPixels() > 300, "original light did not illuminate non-emissive geometry");
      const auto identity = resolve(host, handle).getStableIdentity();
      const auto bufferIndex = resolve(host, handle).getBufferIdx();
      require(identity != 0, "active API light has no stable identity");
      require(lights.create(info) == handle, "same-hash API light update changed the handle");
      require(resolve(host, handle).getStableIdentity() == identity
          && resolve(host, handle).getBufferIdx() == bufferIndex,
        "same-hash light update discarded GPU temporal identity or the previous buffer index");
      const auto saved = info.radiance;
      info.radiance.x = std::numeric_limits<float>::quiet_NaN();
      expectInvalid([&] { lights.create(info); });
      info.radiance = saved;
      require(resolve(host, handle).getRadiance().y == saved.y, "failed light update mutated the registered light");
      lights.destroy(handle);
      expectInvalid([&] { lights.draw(handle); });
      expectInvalid([&] { lights.destroy(handle); });
      frames.step(lights, nullptr, 2);
      require(manager.getActiveCount() == 0, "destroyed API light remains in the GPU light table");
    };

    auto ball = sphere();
    check(lightInfo(&ball), dxvk::RtLightType::Sphere, "api-light-sphere.ppm");
    ball.shaping_hasvalue = 1;
    ball.shaping_value = { { 0, 0, 1 }, 35, .1f, 0 };
    ball.volumetricRadianceScale = 0;
    check(lightInfo(&ball), dxvk::RtLightType::Sphere, "api-light-spot.ppm");

    api::remixapi_LightInfoRectEXT rect { };
    rect.sType = api::REMIXAPI_STRUCT_TYPE_LIGHT_INFO_RECT_EXT;
    rect.position = { 0, 0, 1 };
    rect.xAxis = { 1, 0, 0 };
    rect.yAxis = { 0, 1, 0 };
    rect.direction = { 0, 0, 1 };
    rect.xSize = rect.ySize = 1;
    rect.volumetricRadianceScale = 1;
    check(lightInfo(&rect), dxvk::RtLightType::Rect, "api-light-rect.ppm");
    rect.xAxis = { 0, 0, 1 };
    expectInvalid([&] { lights.create(lightInfo(&rect)); });

    api::remixapi_LightInfoDiskEXT disk { };
    disk.sType = api::REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISK_EXT;
    disk.position = { 0, 0, 1 };
    disk.xAxis = { 1, 0, 0 };
    disk.yAxis = { 0, 1, 0 };
    disk.direction = { 0, 0, 1 };
    disk.xRadius = disk.yRadius = .5f;
    disk.volumetricRadianceScale = 1;
    check(lightInfo(&disk), dxvk::RtLightType::Disk, "api-light-disk.ppm");
    disk.volumetricRadianceScale = -1;
    expectInvalid([&] { lights.create(lightInfo(&disk)); });

    api::remixapi_LightInfoCylinderEXT cylinder { };
    cylinder.sType = api::REMIXAPI_STRUCT_TYPE_LIGHT_INFO_CYLINDER_EXT;
    cylinder.position = { 0, 0, 1 };
    cylinder.axis = { 1, 0, 0 };
    cylinder.radius = .25f;
    cylinder.axisLength = 1;
    cylinder.volumetricRadianceScale = 1;
    check(lightInfo(&cylinder), dxvk::RtLightType::Cylinder, "api-light-cylinder.ppm");

    api::remixapi_LightInfoDistantEXT sun { };
    sun.sType = api::REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT;
    sun.direction = { 0, 0, 1 };
    sun.angularDiameterDegrees = 1;
    sun.volumetricRadianceScale = 1;
    auto info = lightInfo(&sun);
    info.radiance = { 0, 10000, 0 };
    check(info, dxvk::RtLightType::Distant, "api-light-distant.ppm");

    ball = sphere();
    const auto handle = lights.create(lightInfo(&ball));
    ball.position = { 1000, 1000, 1000 };
    frames.step(lights, handle, 8);
    require(resolve(host, handle).getSphereLight().getPosition().z == 1 && frames.greenPixels() > 300,
      "API light retained a pointer to the caller's shape description");
    frames.step(lights, nullptr, 12);
    require(manager.getActiveCount() == 0 && frames.greenPixels() < 100,
      "a light culled by omission continues illuminating the frame");
    frames.step(lights, handle, 8);
    require(manager.getActiveCount() == 1 && frames.greenPixels() > 300, "culled light could not be drawn again");
    lights.destroy(handle);
    ball = sphere();
    const auto queued = lights.create(lightInfo(&ball));
    lights.draw(queued);
    lights.destroy(queued);
    const auto recreated = lights.create(lightInfo(&ball));
    frames.step(lights);
    require(manager.getActiveCount() == 0, "destroy/recreate inherited a removed light's pending draw");
    lights.destroy(recreated);
  }

  std::vector<float> readDome(RuntimeHost& host) {
    const auto& args = host.context()->getSceneManager().getLightManager().getDomeLightArgs();
    require(args.active && args.textureIndex != BINDING_INDEX_INVALID, "drawn dome has no current GPU texture");
    const auto& reference = host.context()->getCommonObjects()->getTextureManager().getTextureTable().at(args.textureIndex);
    const auto* pView = reference.getImageView();
    require(pView && pView->info().format == VK_FORMAT_R16G16B16A16_SFLOAT, "unexpected dome composition format");
    const auto image = pView->image();
    const auto extent = pView->mipLevelExtent(0);
    dxvk::DxvkBufferCreateInfo info { };
    info.size = uint64_t(extent.width) * extent.height * 8;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.stages = VK_PIPELINE_STAGE_HOST_BIT;
    info.access = VK_ACCESS_HOST_READ_BIT;
    const auto buffer = host.device()->createBuffer(info,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      dxvk::DxvkMemoryStats::Category::AppBuffer, "Dome composition readback");
    host.context()->copyImageToBuffer(buffer, 0, 1, 1, image,
      { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, extent);
    host.context()->flushCommandList();
    host.device()->waitForIdle();
    const auto* pHalves = static_cast<const uint16_t*>(buffer->mapPtr(0));
    std::vector<float> result(uint64_t(extent.width) * extent.height * 4);
    for (size_t index = 0; index < result.size(); ++index) {
      result[index] = glm::unpackHalf1x16(pHalves[index]);
    }
    return result;
  }

  api::remixapi_TextureHandle texture(dxvk::native::ApiTextures& textures, uint64_t hash,
      uint32_t width, uint32_t height, const uint8_t* pPixels) {
    api::remixapi_TextureFromMemoryInfo info { };
    info.sType = api::REMIXAPI_STRUCT_TYPE_TEXTURE_FROM_MEMORY_INFO;
    info.hash = hash;
    info.width = width;
    info.height = height;
    info.pixelData = pPixels;
    info.format = api::REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM;
    info.purpose = api::REMIXAPI_TEXTURE_PURPOSE_COLOR;
    return textures.create(info);
  }

  void domes(RuntimeHost& host, ApiLights& lights, dxvk::native::ApiTextures& textures, Frames& frames) {
    api::remixapi_LightInfoDomeEXT base { };
    base.sType = api::REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DOME_EXT;
    base.transform = transform();
    base.opacity = 1;
    auto baseInfo = lightInfo(&base, 0x710);
    baseInfo.radiance = { .125f, .25f, .5f };
    const auto baseHandle = lights.create(baseInfo);
    frames.step(lights, baseHandle);
    auto pixels = readDome(host);
    require(pixels.size() == 4 && pixels[0] == .125f && pixels[1] == .25f && pixels[2] == .5f,
      "untextured dome did not illuminate from its constant radiance");

    const std::array<uint8_t, 4> source { 64, 128, 192, 96 };
    api::remixapi_LightInfoDomeEXT overlay = base;
    overlay.colorTexture = texture(textures, 0x711, 1, 1, source.data());
    overlay.opacity = .35f;
    auto overlayInfo = lightInfo(&overlay, 0x711);
    overlayInfo.radiance = { 1, .5f, 1.5f };
    // Reference values follow the shipped SkyDomeRaster fragment's attenuation/addition passes.
    const float expected[11][3] = {
      { .141599769f, .250129181f, .582934256f },
      { .158070358f, .283070358f, .648816609f },
      { .179772780f, .304772780f, .746477509f },
      { .136066513f, .250086121f, .748802768f },
      { .147046905f, .272046905f, .946449827f },
      { .190796232f, .315796232f, .448844291f },
      { .212843137f, .337843137f, .895294118f },
      { .092230392f, .184460784f, .522647059f },
      { .103210784f, .206421569f, .720294118f },
      { .152493368f, .250213956f, .637359862f },
      { .158026624f, .250257017f, .471491349f }
    };
    api::remixapi_LightHandle overlayHandle = nullptr;
    for (int mode = 0; mode < 11; ++mode) {
      overlay.blendType = mode;
      overlayHandle = lights.create(overlayInfo);
      frames.stepLayers(lights, { baseHandle, overlayHandle });
      pixels = readDome(host);
      for (uint32_t channel = 0; channel < 3; ++channel) {
        if (std::abs(pixels[channel] - expected[mode][channel]) >= .002f) {
          std::cout << "Dome mode=" << mode << ", channel=" << channel << ", actual=" << pixels[channel]
            << ", expected=" << expected[mode][channel] << '\n';
        }
        require(std::abs(pixels[channel] - expected[mode][channel]) < .002f,
          "GPU dome blending differs from the shipped fragment's reference values");
      }
    }
    frames.save("api-light-dome-layers.ppm");
    auto ball = sphere();
    require(lights.create(lightInfo(&ball, 0x711)) == overlayHandle, "light type update changed its hash handle");
    frames.step(lights, overlayHandle, 2);
    require(host.context()->getSceneManager().getLightManager().getActiveCount() == 1
        && !host.context()->getSceneManager().getLightManager().getDomeLightArgs().active,
      "changing a dome to an analytical light retained the dome resource");
    lights.create(overlayInfo);
    frames.stepLayers(lights, { baseHandle, overlayHandle });
    require(host.context()->getSceneManager().getLightManager().getActiveCount() == 0,
      "changing an analytical light to a dome retained the analytical resource");
    overlay.blendType = 0;
    overlay.opacity = 0;
    lights.create(overlayInfo);
    frames.stepLayers(lights, { baseHandle, overlayHandle });
    pixels = readDome(host);
    require(pixels[0] == .125f && pixels[1] == .25f && pixels[2] == .5f,
      "zero-opacity dome changed its background");
    overlay.opacity = -2;
    lights.create(overlayInfo);
    overlay.opacity = std::numeric_limits<float>::infinity();
    expectInvalid([&] { lights.create(overlayInfo); });
    overlay.opacity = 1;
    overlay.blendType = 11;
    expectInvalid([&] { lights.create(overlayInfo); });
    overlay.blendType = 0;
    overlay.colorTexture = reinterpret_cast<api::remixapi_TextureHandle>(0xbad);
    expectInvalid([&] { lights.create(overlayInfo); });
    auto& manager = host.context()->getSceneManager().getLightManager();
    dxvk::DomeLight legacy;
    legacy.radiance = dxvk::Vector3(1.f);
    legacy.worldToLight = dxvk::Matrix4();
    legacy.texture = textures.resolve(reinterpret_cast<api::remixapi_TextureHandle>(0x711));
    const auto legacyHandle = reinterpret_cast<remixapi_LightHandle>(0x720);
    const auto secondLegacyHandle = reinterpret_cast<remixapi_LightHandle>(0x721);
    manager.addExternalDomeLight(legacyHandle, legacy);
    legacy.radiance = dxvk::Vector3(0.f);
    manager.addExternalDomeLight(secondLegacyHandle, legacy);
    manager.addExternalLightInstance(legacyHandle);
    manager.addExternalLightInstance(secondLegacyHandle);
    frames.step(lights);
    pixels = readDome(host);
    require(std::abs(pixels[0] - 64 / 255.f) < .002f && std::abs(pixels[1] - 128 / 255.f) < .002f
        && std::abs(pixels[2] - 192 / 255.f) < .002f,
      "legacy dome selection or its opaque RGB sampling changed");
    manager.addExternalLightInstance(legacyHandle);
    const auto analytical = dxvk::RtSphereLight::tryCreate(dxvk::Vector3(0.f), dxvk::Vector3(0.f), .5f,
      dxvk::RtLightShaping { });
    require(analytical.has_value(), "cannot create the legacy type-change fixture");
    manager.addExternalLight(legacyHandle, dxvk::RtLight(*analytical));
    manager.addExternalLightInstance(secondLegacyHandle);
    frames.step(lights);
    pixels = readDome(host);
    require(pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 0,
      "a changed light type blocked another legacy dome's pending draw");
    manager.removeExternalLight(legacyHandle);
    manager.removeExternalLight(secondLegacyHandle);
    lights.destroy(overlayHandle);
    textures.destroy(reinterpret_cast<api::remixapi_TextureHandle>(0x711));

    std::array<uint8_t, 8 * 4 * 4> gradient { };
    for (uint32_t y = 0; y < 4; ++y) {
      for (uint32_t x = 0; x < 8; ++x) {
        const auto offset = (y * 8 + x) * 4;
        gradient[offset] = uint8_t(x * 32);
        gradient[offset + 1] = uint8_t(y * 64);
        gradient[offset + 2] = 64;
        gradient[offset + 3] = 255;
      }
    }
    const auto image = texture(textures, 0x712, 8, 4, gradient.data());
    overlay = base;
    overlay.colorTexture = image;
    overlay.transform.matrix[0][0] = overlay.transform.matrix[1][1] = -1;
    overlay.transform.matrix[0][3] = 50;
    overlayInfo.radiance = { 1, 1, 1 };
    overlayHandle = lights.create(overlayInfo);
    overlay.transform = { };
    frames.step(lights, overlayHandle);
    pixels = readDome(host);
    require(pixels.size() == gradient.size(), "dome composition did not retain texture extent");
    for (uint32_t y = 0; y < 4; ++y) {
      for (uint32_t x = 0; x < 8; ++x) {
        const auto offset = (y * 8 + x) * 4;
        require(std::abs(pixels[offset] - gradient[(y * 8 + (x + 4) % 8) * 4] / 255.f) < .002f
            && std::abs(pixels[offset + 1] - gradient[offset + 1] / 255.f) < .002f,
          "dome rotation, translation cancellation or lat-long sampling changed");
      }
    }
    for (size_t offset = 0; offset < gradient.size(); offset += 4) {
      gradient[offset] = gradient[offset + 2] = 0;
      gradient[offset + 1] = gradient[offset + 3] = 255;
    }
    const api::remixapi_TexturePatch patch { 0, 0, 8, 4, gradient.data(), 32, api::REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM };
    const api::remixapi_TextureBatchEntry entry { image, 1, &patch };
    const api::remixapi_TexturePatchInfo update { api::REMIXAPI_STRUCT_TYPE_TEXTURE_PATCH_INFO, nullptr, 1, &entry };
    textures.update(update);
    textures.destroy(image);
    gradient.fill(0);
    frames.step(lights, overlayHandle, 8);
    pixels = readDome(host);
    for (size_t offset = 0; offset < pixels.size(); offset += 4) {
      require(pixels[offset] == 0 && pixels[offset + 1] == 1 && pixels[offset + 2] == 0,
        "dome lost its patched texture after its API handle was destroyed");
    }
    require(frames.greenPixels() > 300, "composed sky is absent from the rendered frame");
    frames.save("api-light-dome-patched.ppm");
    lights.destroy(overlayHandle);
    lights.destroy(baseHandle);
    frames.step(lights, nullptr, 2);
    require(!host.context()->getSceneManager().getLightManager().getDomeLightArgs().active,
      "destroyed dome remains active");

    const auto path = std::filesystem::current_path() / "native-dome.dds";
    std::array<uint32_t, 32> header { };
    header[0] = 0x20534444;
    header[1] = 124;
    header[2] = 0x100f;
    header[3] = header[4] = 2;
    header[5] = 8;
    header[7] = 1;
    header[19] = header[22] = 32;
    header[20] = 0x41;
    header[23] = 0xff;
    header[24] = 0xff00;
    header[25] = 0xff0000;
    header[26] = 0xff000000;
    header[27] = 0x1000;
    const std::array<uint32_t, 4> data { 0xff00ff00, 0xff00ff00, 0xff00ff00, 0xff00ff00 };
    {
      std::ofstream file(path, std::ios::binary);
      file.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
      file.write(reinterpret_cast<const char*>(data.data()), sizeof(data));
      require(bool(file), "cannot write dome DDS fixture");
    }
    overlay = base;
    overlay.colorTexture = textures.createFile(path);
    overlayHandle = lights.create(overlayInfo);
    textures.destroy(overlay.colorTexture);
    frames.step(lights, overlayHandle);
    pixels = readDome(host);
    require(pixels.size() == 16, "file dome was not fully resident on its first frame");
    for (size_t offset = 0; offset < pixels.size(); offset += 4) {
      require(pixels[offset] == 0 && pixels[offset + 1] == 1 && pixels[offset + 2] == 0,
        "file dome was composed before its first texture upload");
    }
    lights.destroy(overlayHandle);
    std::filesystem::remove(path);
  }

  void usd(RuntimeHost& host, ApiLights& lights, Frames& frames) {
    api::remixapi_LightInfoUSDEXT description { };
    description.sType = api::REMIXAPI_STRUCT_TYPE_LIGHT_INFO_USD_EXT;
    description.lightType = api::REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
    description.transform = transform();
    description.transform.matrix[2][3] = 1;
    float radius = .5f;
    float intensity = 40;
    float exposure = 0;
    float volumeScale = 0;
    api::remixapi_Float3D color { 0, 1, 0 };
    description.pRadius = &radius;
    description.pIntensity = &intensity;
    description.pExposure = &exposure;
    description.pColor = &color;
    description.pVolumetricRadianceScale = &volumeScale;
    auto info = lightInfo(&description, 0x702);
    info.radiance.x = std::numeric_limits<float>::quiet_NaN();
    const auto handle = lights.create(info);
    color = { 1, 0, 0 };
    intensity = 0;
    radius = 0;
    frames.step(lights, handle, 10);
    require(frames.greenPixels() > 300 && resolve(host, handle).getSphereLight().getRadius() == .5f
        && resolve(host, handle).getSphereLight().getVolumetricRadianceScale() == 0,
      "USD light did not copy optional parameters or ignore base radiance");
    frames.save("api-light-usd.ppm");
    exposure = std::numeric_limits<float>::quiet_NaN();
    expectInvalid([&] { lights.create(info); });
    lights.destroy(handle);
  }
}

int main() {
  dxvk::native::test::Window window;
  if (!glfwInit()) {
    return 77;
  }
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
  window.pHandle = glfwCreateWindow(96, 64, "Original native Remix API lights", nullptr, nullptr);
  if (!window.pHandle) {
    return 77;
  }
  glfwMakeContextCurrent(window.pHandle);
  try {
    dxvk::native::ApiOptions options;
    options.set("rtx.fallbackLightMode", "0");
    options.set("rtx.autoExposure.enabled", "False");
    RuntimeHost host(std::filesystem::current_path().u8string());
    options.applyStartupWrites();
    dxvk::native::test::Reader reader(host.interopInfo());
    dxvk::native::ApiTextures textures(host);
    dxvk::native::ApiMaterials materials(host, textures);
    dxvk::native::ApiMeshes meshes(host, materials);
    dxvk::native::ApiInstances instances(host, meshes);
    dxvk::native::ApiCameras cameras(host, materials);
    ApiLights lights(host, textures);
    Frames frames { host, cameras, reader };
    const auto surface = plane(materials, meshes, instances);
    badInputs(lights);
    analytical(host, lights, frames);
    usd(host, lights, frames);
    domes(host, lights, textures, frames);
    instances.remove(surface);
    auto ball = sphere();
    const auto emptyLight = lights.create(lightInfo(&ball));
    frames.step(lights, emptyLight);
    require(host.context()->getSceneManager().getLightManager().getActiveCount() == 1,
      "empty scene did not consume its current light frame");
    frames.step(lights);
    require(host.context()->getSceneManager().getLightManager().getActiveCount() == 0,
      "empty-scene light draws leaked into later frames");
    std::cout << "PASS native API analytical lights, shaping, USD, dome blending and frame visibility\n";
    return 0;
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "FAIL " << error.message() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
  }
  return 1;
}
