// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>

#include "dxvk/pch/dxvk.pch.h"
#include "gl_output_reader.h"
#include "original_scene_fixture.h"
#include "runtime_host.h"

namespace {
  using dxvk::native::RuntimeHost;
  using dxvk::native::test::require;
  using dxvk::native::test::requireFailure;

  void checkEmptyFrame(RuntimeHost& host, dxvk::native::test::Reader& reader, VkExtent3D extent) {
    require(host.beginFrame(extent), "a visible native host frame was skipped");
    requireFailure([&] { host.beginFrame(extent); });
    requireFailure([&] { host.interopInfo(); });
    host.submit();
    requireFailure([&] { host.submit(); });
    const auto pixels = reader.readAndPresent(host);
    require(std::all_of(pixels.begin(), pixels.end(), [](unsigned char value) { return value == 0; }),
      "a frame without a valid camera leaked stale or uninitialized color");
    const auto& depth = reader.depthPixels();
    require(!depth.empty() && std::all_of(depth.begin(), depth.end(), [](float value) { return value == 1.0f; }),
      "a frame without a valid camera leaked stale or uninitialized depth");
  }

  dxvk::Rc<dxvk::DxvkImageView> createTexture(RuntimeHost& host) {
    dxvk::DxvkImageCreateInfo info { };
    info.type = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R8G8B8A8_SRGB;
    info.extent = { 1, 1, 1 };
    info.numLayers = 1;
    info.mipLevels = 1;
    info.sampleCount = VK_SAMPLE_COUNT_1_BIT;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
      | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    info.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const auto image = host.device()->createImage(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      dxvk::DxvkMemoryStats::Category::RTXMaterialTexture, "native host animated texture");
    host.context()->initImage(image, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }, VK_IMAGE_LAYOUT_UNDEFINED);
    dxvk::DxvkImageViewCreateInfo viewInfo { };
    viewInfo.type = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = info.format;
    viewInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.numLevels = 1;
    viewInfo.numLayers = 1;
    return host.device()->createImageView(image, viewInfo);
  }

  void checkTexturedFrames(RuntimeHost& host, dxvk::native::test::Reader& reader) {
    const auto mesh = reinterpret_cast<remixapi_MeshHandle>(uintptr_t(101));
    const auto material = reinterpret_cast<remixapi_MaterialHandle>(uintptr_t(102));
    const auto texture = createTexture(host);
    dxvk::OpaqueMaterialData opaque;
    opaque.setAlbedoOpacityTexture(dxvk::TextureRef(texture));
    opaque.setEmissiveColorTexture(dxvk::TextureRef(texture));
    opaque.setAlbedoConstant(dxvk::Vector3(1.0f));
    opaque.setEmissiveColorConstant(dxvk::Vector3(1.0f));
    opaque.setEmissiveIntensity(4.0f);
    opaque.setEnableEmission(true);
    const auto& assets = host.context()->getSceneManager().getAssetReplacer();
    assets->makeMaterialWithTexturePreload(*host.context(), material, dxvk::MaterialData(opaque));
    std::vector<dxvk::RasterGeometry> geometry;
    geometry.push_back(dxvk::native::test::createTriangle(host.device(), material));
    assets->registerExternalMesh(mesh, std::move(geometry));

    const std::array<VkExtent3D, 3> extents {{ { 96, 64, 1 }, { 128, 80, 1 }, { 96, 64, 1 } }};
    for (size_t color = 0; color < extents.size(); color++) {
      std::array<unsigned char, 4> texel { 0, 0, 0, 255 };
      texel[color] = 255;
      host.context()->updateImage(texture->image(), { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        { 0, 0, 0 }, { 1, 1, 1 }, texel.data(), texel.size(), texel.size());
      std::vector<unsigned char> pixels;
      for (uint32_t frame = 0; frame < 4; frame++) {
        const auto extent = extents[color];
        require(host.beginFrame(extent), "a native host scene frame was skipped");
        constexpr float kNear = 0.1f;
        constexpr float kFar = 100.0f;
        const float aspect = float(extent.width) / extent.height;
        const dxvk::Matrix4 projection(
          dxvk::Vector4(1.7320508f / aspect, 0, 0, 0),
          dxvk::Vector4(0, 1.7320508f, 0, 0),
          dxvk::Vector4(0, 0, kFar / (kFar - kNear), 1),
          dxvk::Vector4(0, 0, -kNear * kFar / (kFar - kNear), 0));
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
      size_t coloredPixels = 0;
      for (size_t offset = 0; offset < pixels.size(); offset += 4) {
        if (pixels[offset + color] > 180
            && pixels[offset + color] > pixels[offset + (color + 1) % 3] + 20
            && pixels[offset + color] > pixels[offset + (color + 2) % 3] + 20) {
          ++coloredPixels;
        }
      }
      require(coloredPixels > 100, "an original textured material did not reflect its updated GPU texel");
      std::cout << "Native host texture channel " << color << ": " << coloredPixels << " pixels\n";
    }
    host.context()->getSceneManager().destroyExternalMesh(host.context(), mesh);
    assets->destroyExternalMaterial(material);
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
  window.pHandle = glfwCreateWindow(128, 96, "Original Remix native host verification", nullptr, nullptr);
  if (!window.pHandle) {
    return 77;
  }
  glfwMakeContextCurrent(window.pHandle);
  try {
    const auto rootPath = std::filesystem::current_path().u8string();
    for (uint32_t cycle = 0; cycle < 2; cycle++) {
      RuntimeHost host(rootPath);
      const auto initial = host.interopInfo();
      require(initial.rendererDoneFd >= 3 && initial.clientDoneFd >= 3 && initial.imageFd < 0,
        "native host startup exposed incomplete interop resources");
      requireFailure([&] { RuntimeHost duplicate(rootPath); });
      require(dxvk::g_dxvkDeviceNative == host.device().ptr(), "duplicate startup changed the active native host");
      requireFailure([&] { host.submit(); });
      requireFailure([&] { host.present(1); });
      requireFailure([&] { host.beginFrame({ 16384, 64, 1 }); });
      requireFailure([&] { host.beginFrame({ 96, 64, 2 }); });
      require(!host.beginFrame({ 0, 0, 1 }), "a minimized native host window started a frame");
      dxvk::native::test::Reader reader(initial);
      checkEmptyFrame(host, reader, { 96, 64, 1 });
      checkTexturedFrames(host, reader);
      require(!host.beginFrame({ 0, 64, 1 }), "a zero-width native host window started a frame");
      checkEmptyFrame(host, reader, { 80, 96, 1 });
      checkEmptyFrame(host, reader, { 96, 64, 1 });
    }
    require(dxvk::g_dxvkDeviceNative == nullptr, "native host shutdown left a registered device");
    std::cout << "PASS native host lifecycle, original textured frames, resize and minimized-window recovery\n";
    return 0;
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "FAIL " << error.message() << '\n';
    return 1;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
