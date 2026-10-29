// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

#include "api016_cameras.h"
#include "api016_error.h"
#include "api016_materials.h"
#include "api016_textures.h"
#include "dxvk/pch/dxvk.pch.h"
#include "gl_output_reader.h"
#include "original_scene_fixture.h"
#include "runtime_host.h"

namespace {
  namespace api = dxvk::native::api016;
  using dxvk::native::ApiCameras;
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
        "invalid API camera operation returned the wrong error");
      return;
    }
    throw std::runtime_error("invalid API camera operation succeeded");
  }

  api::remixapi_CameraInfoParameterizedEXT parameters(float x, float aspect) {
    api::remixapi_CameraInfoParameterizedEXT result { };
    result.sType = api::REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT;
    result.position = { x, 0, 0 };
    result.forward = { 0, 0, 2 };
    result.up = { 0, 3, 0 };
    result.right = { 4, 0, 0 };
    result.fovYInDegrees = 60;
    result.aspect = aspect;
    result.nearPlane = .05f;
    result.farPlane = 1000;
    return result;
  }

  api::remixapi_CameraInfo cameraInfo(void* pNext) {
    api::remixapi_CameraInfo result { };
    result.sType = api::REMIXAPI_STRUCT_TYPE_CAMERA_INFO;
    result.type = api::REMIXAPI_CAMERA_TYPE_WORLD;
    result.pNext = pNext;
    return result;
  }

  api::remixapi_CameraMediumInfo mediumInfo(api::remixapi_MaterialHandle medium) {
    api::remixapi_CameraMediumInfo result { };
    result.sType = api::REMIXAPI_STRUCT_TYPE_CAMERA_MEDIUM_INFO;
    result.medium = medium;
    return result;
  }

  api::remixapi_MaterialInfo materialInfo(void* pNext) {
    api::remixapi_MaterialInfo result { };
    result.sType = api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
    result.pNext = pNext;
    result.hash = 0x500;
    result.emissiveIntensity = 4;
    result.emissiveColorConstant = { 1, 0, 0 };
    return result;
  }

  struct CameraPose {
    api::remixapi_CameraInfoParameterizedEXT parameters;
    dxvk::Matrix4 view;
  };

  std::vector<uint8_t> render(RuntimeHost& host, ApiCameras& cameras,
      dxvk::native::test::Reader& reader, remixapi_MeshHandle mesh, VkExtent3D extent,
      const CameraPose& pose, bool matrixCamera = false, api::remixapi_MaterialHandle medium = nullptr) {
    std::vector<uint8_t> pixels;
    for (uint32_t frame = 0; frame < 4; ++frame) {
      require(host.beginFrame(extent), "API camera frame was skipped");
      auto params = pose.parameters;
      auto info = cameraInfo(&params);
      info.view[0][0] = std::numeric_limits<float>::quiet_NaN();
      if (matrixCamera) {
        info.pNext = nullptr;
        const float z = params.farPlane / (params.farPlane - params.nearPlane);
        const dxvk::Matrix4 projection(dxvk::Vector4(1.7320508f / params.aspect, 0, 0, 0),
          dxvk::Vector4(0, 1.7320508f, 0, 0), dxvk::Vector4(0, 0, z, 1),
          dxvk::Vector4(0, 0, -params.nearPlane * z, 0));
        std::memcpy(info.view, &pose.view, sizeof(info.view));
        std::memcpy(info.projection, &projection, sizeof(info.projection));
      }
      cameras.setup(info);
      info.type = api::REMIXAPI_CAMERA_TYPE_VIEW_MODEL;
      cameras.setup(info);
      info.type = api::REMIXAPI_CAMERA_TYPE_SKY;
      cameras.setup(info);
      const auto& manager = host.context()->getSceneManager().getCameraManager();
      const auto& mainCamera = manager.getMainCamera();
      const auto position = mainCamera.getPosition(false);
      require(std::abs(position.x - params.position.x) < .0001f
          && std::abs(position.y - params.position.y) < .0001f
          && std::abs(position.z - params.position.z) < .0001f
          && std::abs(mainCamera.getNearPlane() - params.nearPlane) < .00001f,
        "API camera position or near plane changed during conversion");
      for (uint32_t column = 0; column < 4; ++column) {
        for (uint32_t row = 0; row < 4; ++row) {
          require(std::abs(mainCamera.getWorldToViewf(false)[column][row] - pose.view[column][row]) < .00001f,
            "API camera basis or matrix layout changed its expected world-to-view transform");
        }
      }
      require(std::abs(manager.getCamera(dxvk::CameraType::ViewModel).getPosition(false).x - params.position.x) < .0001f,
        "view-model camera did not receive the world camera pose");
      require(!dxvk::RtxOptions::enableNearPlaneOverride(), "API camera left the near-plane override enabled");
      params.position = { 100, 100, 100 };
      params.fovYInDegrees = 5;
      cameras.setMedium(mediumInfo(medium));
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

  double centroid(const std::vector<float>& pixels, VkExtent3D extent, bool horizontal = true) {
    require(pixels.size() == size_t(extent.width) * extent.height, "API camera depth image has the wrong extent");
    size_t count = 0;
    uint64_t sumCoordinates = 0;
    for (size_t offset = 0; offset < pixels.size(); ++offset) {
      if (std::isfinite(pixels[offset]) && pixels[offset] != pixels[0]) {
        ++count;
        sumCoordinates += horizontal ? offset % extent.width : offset / extent.width;
      }
    }
    require(count > 100, "API camera did not produce a visible original path-traced triangle");
    return (double(sumCoordinates) / count + .5) / (horizontal ? extent.width : extent.height);
  }

  void checkCameraTypes(RuntimeHost& host, ApiCameras& cameras, dxvk::native::test::Reader& reader) {
    require(host.beginFrame({ 96, 64, 1 }), "cannot begin camera routing frame");
    const std::array<api::remixapi_CameraType, 3> types {
      api::REMIXAPI_CAMERA_TYPE_WORLD, api::REMIXAPI_CAMERA_TYPE_VIEW_MODEL, api::REMIXAPI_CAMERA_TYPE_SKY };
    const std::array<dxvk::CameraType::Enum, 3> camerasByType {
      dxvk::CameraType::Main, dxvk::CameraType::ViewModel, dxvk::CameraType::Sky };
    const std::array<api::remixapi_Float3D, 3> positions {{ { .25f, .5f, .75f }, { -.25f, -.5f, -.75f }, { 7, 8, 9 } }};
    auto params = parameters(0, 1.5f);
    auto info = cameraInfo(&params);
    for (size_t index = 0; index < types.size(); ++index) {
      info.type = types[index];
      params.position = positions[index];
      cameras.setup(info);
    }
    params.position = { 100, 100, 100 };
    const auto& manager = host.context()->getSceneManager().getCameraManager();
    for (size_t index = 0; index < types.size(); ++index) {
      const auto position = manager.getCamera(camerasByType[index]).getPosition(false);
      require(std::abs(position.x - positions[index].x) < .0001f
          && std::abs(position.y - positions[index].y) < .0001f
          && std::abs(position.z - positions[index].z) < .0001f,
        "world, view-model or sky camera overwrote another camera's owned pose");
    }
    host.submit();
    reader.readAndPresent(host);
  }

  void checkRotatedPoses(RuntimeHost& host, ApiCameras& cameras,
      dxvk::native::test::Reader& reader, remixapi_MeshHandle mesh) {
    constexpr float diagonal = .7071067812f;
    constexpr float sinPitch = .5f;
    constexpr float cosPitch = .8660254038f;
    std::array<CameraPose, 3> poses {{
      { parameters(0, 1.5f), dxvk::Matrix4(dxvk::Vector4(diagonal, 0, diagonal, 0), dxvk::Vector4(0, 1, 0, 0),
          dxvk::Vector4(-diagonal, 0, diagonal, 0), dxvk::Vector4(3 * diagonal, 0, 3 - 3 * diagonal, 1)) },
      { parameters(0, 1.5f), dxvk::Matrix4(dxvk::Vector4(1, 0, 0, 0), dxvk::Vector4(0, cosPitch, -sinPitch, 0),
          dxvk::Vector4(0, sinPitch, cosPitch, 0), dxvk::Vector4(0, -1.5f, 3 - 3 * cosPitch, 1)) },
      { parameters(0, 1.5f), dxvk::Matrix4(dxvk::Vector4(diagonal, -diagonal, 0, 0), dxvk::Vector4(diagonal, diagonal, 0, 0),
          dxvk::Vector4(0, 0, 1, 0), dxvk::Vector4(0, 0, 0, 1)) }
    }};
    // Rotated views keep the fixture center three units along the forward axis.
    poses[0].parameters.position = { -3 * diagonal, 0, 3 - 3 * diagonal };
    poses[0].parameters.right = { 4 * diagonal, 0, -4 * diagonal };
    poses[0].parameters.forward = { 2 * diagonal, 0, 2 * diagonal };
    poses[1].parameters.position = { 0, 1.5f, 3 - 3 * cosPitch };
    poses[1].parameters.up = { 0, 3 * cosPitch, 3 * sinPitch };
    poses[1].parameters.forward = { 0, -2 * sinPitch, 2 * cosPitch };
    poses[2].parameters.right = { 4 * diagonal, 4 * diagonal, 0 };
    poses[2].parameters.up = { -3 * diagonal, 3 * diagonal, 0 };
    for (size_t index = 0; index < poses.size(); ++index) {
      const VkExtent3D extent { 96, 64, 1 };
      render(host, cameras, reader, mesh, extent, poses[index]);
      const auto depthExtent = host.interopInfo().depthExtent;
      const auto parameterizedDepth = reader.depthPixels();
      render(host, cameras, reader, mesh, extent, poses[index], true);
      for (bool horizontal : { false, true }) {
        require(std::abs(centroid(parameterizedDepth, depthExtent, horizontal)
            - centroid(reader.depthPixels(), depthExtent, horizontal)) < .035,
          "yaw, pitch or roll changed between parameterized and explicit camera rendering");
      }
      std::cout << "API rotated camera pose=" << index << ", parameterized/matrix output agrees\n";
    }
  }

  void saveImage(const char* pPath, const std::vector<uint8_t>& pixels, VkExtent3D extent = { 96, 64, 1 }) {
    std::ofstream file(pPath, std::ios::binary);
    file << "P6\n" << extent.width << ' ' << extent.height << "\n255\n";
    for (size_t offset = 0; offset < pixels.size(); offset += 4) {
      file.write(reinterpret_cast<const char*>(pixels.data() + offset), 3);
    }
    require(bool(file), "cannot save API medium render fixture");
  }

  void checkBadInputs(ApiCameras& cameras) {
    auto params = parameters(0, 1.5f);
    auto info = cameraInfo(&params);
    info.type = static_cast<api::remixapi_CameraType>(99);
    expectInvalid([&] { cameras.setup(info); });
    info.type = api::REMIXAPI_CAMERA_TYPE_WORLD;
    params.forward = { 0, 0, 0 };
    expectInvalid([&] { cameras.setup(info); });
    params = parameters(0, 1.5f);
    params.right = params.up;
    expectInvalid([&] { cameras.setup(info); });
    params = parameters(0, 1.5f);
    params.fovYInDegrees = 180;
    expectInvalid([&] { cameras.setup(info); });
    params = parameters(0, 1.5f);
    params.nearPlane = params.farPlane;
    expectInvalid([&] { cameras.setup(info); });
    params = parameters(0, 1.5f);
    params.pNext = &params;
    expectInvalid([&] { cameras.setup(info); });
    params.pNext = nullptr;
    info.pNext = nullptr;
    info.view[0][0] = std::numeric_limits<float>::infinity();
    expectInvalid([&] { cameras.setup(info); });
    info.view[0][0] = 0;
    expectInvalid([&] { cameras.setup(info); });
    auto medium = mediumInfo(nullptr);
    medium.sType = api::REMIXAPI_STRUCT_TYPE_CAMERA_INFO;
    expectInvalid([&] { cameras.setMedium(medium); });
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
  window.pHandle = glfwCreateWindow(128, 96, "Original Remix API camera verification", nullptr, nullptr);
  if (!window.pHandle) {
    return 77;
  }
  glfwMakeContextCurrent(window.pHandle);
  try {
    RuntimeHost host(std::filesystem::current_path().u8string());
    dxvk::native::test::Reader reader(host.interopInfo());
    ApiTextures textures(host);
    ApiMaterials materials(host, textures);
    ApiCameras cameras(host, materials);
    checkBadInputs(cameras);
    checkCameraTypes(host, cameras, reader);
    api::remixapi_MaterialInfoOpaqueEXT opaque { };
    opaque.sType = api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT;
    opaque.albedoConstant = { 1, 1, 1 };
    opaque.opacityConstant = 1;
    opaque.roughnessConstant = .5f;
    opaque.alphaTestType = 7;
    auto info = materialInfo(&opaque);
    const auto material = materials.create(info);
    expectInvalid([&] { cameras.setMedium(mediumInfo(material)); });
    const auto mesh = reinterpret_cast<remixapi_MeshHandle>(uintptr_t(0x501));
    std::vector<dxvk::RasterGeometry> geometry;
    geometry.push_back(dxvk::native::test::createTriangle(host.device(),
      reinterpret_cast<remixapi_MaterialHandle>(material)));
    auto& scene = host.context()->getSceneManager();
    scene.getAssetReplacer()->registerExternalMesh(mesh, std::move(geometry));
    const std::array<VkExtent3D, 4> extents {{ { 96, 64, 1 }, { 128, 80, 1 }, { 80, 96, 1 }, { 96, 64, 1 } }};
    const std::array<float, 4> positions {{ 0, .65f, -.45f, 0 }};
    for (size_t index = 0; index < extents.size(); ++index) {
      const CameraPose pose { parameters(positions[index], float(extents[index].width) / extents[index].height),
        dxvk::Matrix4(dxvk::Vector3(-positions[index], 0, 0)) };
      const auto pixels = render(host, cameras, reader, mesh, extents[index], pose, index == 3);
      const double measured = centroid(reader.depthPixels(), host.interopInfo().depthExtent);
      const double expected = .5 - positions[index] * 1.7320508 / (3 * double(extents[index].width) / extents[index].height) * .5;
      std::cout << "API camera x=" << positions[index] << ", centroid=" << measured << ", expected=" << expected << '\n';
      saveImage("api-camera-pose.ppm", pixels, extents[index]);
      require(std::abs(measured - expected) < .035,
        "parameterized/matrix API camera changed its handedness, projection or input ownership");
    }
    checkRotatedPoses(host, cameras, reader, mesh);

    info.emissiveColorConstant = { 1, 1, 1 };
    materials.update(material, info);
    const VkExtent3D extent { 96, 64, 1 };
    const CameraPose origin { parameters(0, 1.5f), dxvk::Matrix4() };
    const auto dry = render(host, cameras, reader, mesh, extent, origin);
    api::remixapi_MaterialInfoTranslucentEXT water { };
    water.sType = api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_TRANSLUCENT_EXT;
    water.refractiveIndex = 1.333f;
    water.transmittanceColor = { .03f, .3f, .9f };
    water.transmittanceMeasurementDistance = .1f;
    auto waterInfo = materialInfo(&water);
    waterInfo.emissiveIntensity = 0;
    const auto waterMaterial = materials.create(waterInfo);
    const auto wet = render(host, cameras, reader, mesh, extent, origin, false, waterMaterial);
    double red = 0;
    double blue = 0;
    size_t samples = 0;
    for (size_t offset = 0; offset < dry.size(); offset += 4) {
      if (dry[offset] > 180 && dry[offset + 1] > 180 && dry[offset + 2] > 180) {
        red += wet[offset];
        blue += wet[offset + 2];
        ++samples;
      }
    }
    require(samples > 100, "API camera medium fixture did not render its white source");
    red /= samples;
    blue /= samples;
    std::cout << "API camera medium: " << samples << " samples, red=" << red << ", blue=" << blue << '\n';
    saveImage("api-camera-dry.ppm", dry);
    saveImage("api-camera-water.ppm", wet);
    require(blue > red + 25 && blue > 50, "API camera medium did not attenuate the original path-traced light");
    materials.destroy(waterMaterial);
    expectInvalid([&] { cameras.setMedium(mediumInfo(waterMaterial)); });
    const auto restored = render(host, cameras, reader, mesh, extent, origin);
    size_t whitePixels = 0;
    for (size_t offset = 0; offset < restored.size(); offset += 4) {
      whitePixels += restored[offset] > 180 && restored[offset + 1] > 180 && restored[offset + 2] > 180;
    }
    saveImage("api-camera-restored.ppm", restored);
    require(whitePixels > 100, "API camera kept a stale underwater material after clearing its medium");
    scene.destroyExternalMesh(host.context(), mesh);
    materials.destroy(material);
    std::cout << "PASS native API cameras, owned poses, resize, view model, sky and original underwater attenuation\n";
    return 0;
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "FAIL " << error.message() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
  }
  return 1;
}
