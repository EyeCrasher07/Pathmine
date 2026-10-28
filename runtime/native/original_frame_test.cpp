// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "dxvk/pch/dxvk.pch.h"
#include "dxvk/rtx_render/rtx_device_features.h"
#include "dxvk/rtx_render/rtx_dlss.h"
#include "gl_output_reader.h"
#include "offscreen_output.h"
#include "original_scene_fixture.h"
#include "util/util_filesys.h"

namespace {
  struct DeviceLifetime {
    dxvk::Rc<dxvk::DxvkDevice> device;

    ~DeviceLifetime() {
      if (device != nullptr) {
        device->waitForIdle();
        device->getCommon()->onDestroy();
        dxvk::g_dxvkDeviceNative = nullptr;
      }
    }
  };

  double renderScene(const dxvk::Rc<dxvk::DxvkDevice>& device,
      const dxvk::Rc<dxvk::RtxContext>& context, remixapi_MeshHandle mesh,
      VkExtent3D extent, float cameraX, const char* pOutputPath,
      dxvk::native::OffscreenOutput* pSharedOutput, dxvk::native::test::Reader* pReader) {
    dxvk::DxvkImageCreateInfo imageInfo { };
    imageInfo.type = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = extent;
    imageInfo.numLayers = 1;
    imageInfo.mipLevels = 1;
    imageInfo.sampleCount = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
      | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageInfo.stages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    imageInfo.access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.layout = VK_IMAGE_LAYOUT_GENERAL;
    dxvk::Rc<dxvk::DxvkImage> image;
    if (!pSharedOutput) {
      image = device->createImage(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        dxvk::DxvkMemoryStats::Category::AppTexture, "native original RTX output");
      context->initImage(image, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }, VK_IMAGE_LAYOUT_UNDEFINED);
    }
    dxvk::DxvkBufferCreateInfo bufferInfo { };
    bufferInfo.size = extent.width * extent.height * 4;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.stages = VK_PIPELINE_STAGE_HOST_BIT;
    bufferInfo.access = VK_ACCESS_HOST_READ_BIT;
    const auto readback = device->createBuffer(bufferInfo,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      dxvk::DxvkMemoryStats::Category::AppBuffer, "native original RTX readback");
    std::vector<unsigned char> sharedPixels;
    context->resetScreenResolution(extent);
    context->prepareScreenResolution();
    const auto depthExtent = context->getResourceManager().getDownscaleDimensions();
    auto depthBufferInfo = bufferInfo;
    depthBufferInfo.size = depthExtent.width * depthExtent.height * sizeof(float);
    const auto depthReadback = device->createBuffer(depthBufferInfo,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      dxvk::DxvkMemoryStats::Category::AppBuffer, "native original RTX depth readback");
    constexpr float kNear = 0.1f;
    constexpr float kFar = 100.0f;
    const float aspect = float(extent.width) / extent.height;
    const dxvk::Matrix4 projection(
      dxvk::Vector4(1.7320508f / aspect, 0, 0, 0),
      dxvk::Vector4(0, 1.7320508f, 0, 0),
      dxvk::Vector4(0, 0, kFar / (kFar - kNear), 1),
      dxvk::Vector4(0, 0, -kNear * kFar / (kFar - kNear), 0));
    const dxvk::Matrix4 view(dxvk::Vector3(-cameraX, 0, 0));
    for (uint32_t frame = 0; frame < 4; frame++) {
      if (pSharedOutput) {
        image = pSharedOutput->beginFrame(extent, depthExtent);
      }
      const auto frameId = device->getCurrentFrameId();
      context->getSceneManager().getCameraManager().processExternalCamera(
        dxvk::CameraType::Main, view, projection);
      auto state = std::make_unique<dxvk::ExternalDrawState>();
      state->mesh = mesh;
      state->cameraType = dxvk::CameraType::Main;
      state->drawCall.cameraType = dxvk::CameraType::Main;
      state->doubleSided = true;
      context->commitExternalGeometryToRT(std::move(state));
      context->endFrame(frameId, image);
      if (frame == 3) {
        context->copyImageToBuffer(readback, 0, 4, 4, image,
          { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, extent);
        context->copyImageToBuffer(depthReadback, 0, 4, 4,
          context->getResourceManager().getRaytracingOutput().m_primaryDepth.image,
          { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, depthExtent);
      }
      if (pSharedOutput) {
        pSharedOutput->endFrame(context, context->getResourceManager().getRaytracingOutput().m_primaryDepth.image);
        sharedPixels = pReader->readAndPresent(*pSharedOutput);
      } else {
        context->flushCommandList();
      }
      device->waitForIdle();
      device->incrementPresentCount();
      std::cout << "Original RTX frame " << frameId << " complete\n" << std::flush;
    }

    const auto* pPixels = static_cast<const unsigned char*>(readback->mapPtr(0));
    if (pSharedOutput && (sharedPixels.size() != bufferInfo.size
        || std::memcmp(sharedPixels.data(), pPixels, sharedPixels.size()))) {
      throw std::runtime_error("original path-traced pixels changed during OpenGL FD presentation");
    }
    const auto* pDepth = static_cast<const float*>(depthReadback->mapPtr(0));
    if (pSharedOutput && (pReader->depthPixels().size() * sizeof(float) != depthBufferInfo.size
        || std::memcmp(pReader->depthPixels().data(), pDepth, depthBufferInfo.size))) {
      throw std::runtime_error("original path-traced depth changed during OpenGL FD presentation");
    }
    const double expectedCentroid = 0.5 - cameraX * 1.7320508 / (aspect * 3.0) * 0.5;
    const uint32_t depthX = uint32_t(expectedCentroid * depthExtent.width);
    const uint32_t depthY = uint32_t((0.5 + 1.7320508 / 18.0) * depthExtent.height);
    const float centerDepth = pDepth[depthY * depthExtent.width + depthX];
    if (!std::isfinite(centerDepth) || centerDepth == pDepth[0]) {
      throw std::runtime_error("original path-traced depth does not separate geometry from background");
    }
    std::ofstream output(pOutputPath, std::ios::binary);
    output << "P6\n" << extent.width << ' ' << extent.height << "\n255\n";
    size_t redPixels = 0;
    uint64_t redPixelX = 0;
    for (size_t pixel = 0; pixel < extent.width * extent.height; pixel++) {
      output.write(reinterpret_cast<const char*>(pPixels + pixel * 4), 3);
      if (pPixels[pixel * 4] > 180
          && pPixels[pixel * 4] > pPixels[pixel * 4 + 1] + 20
          && pPixels[pixel * 4] > pPixels[pixel * 4 + 2] + 20) {
        ++redPixels;
        redPixelX += pixel % extent.width;
      }
    }
    if (!output || redPixels < 100 || redPixels > extent.width * extent.height / 2
        || pPixels[0] > 100) {
      throw std::runtime_error("original path-traced emissive triangle is absent or covers the background");
    }
    const double centroid = (double(redPixelX) / redPixels + 0.5) / extent.width;
    if (std::abs(centroid - expectedCentroid) > 0.035) {
      throw std::runtime_error("original path-traced geometry does not follow the camera projection");
    }
    std::cout << "Verified " << extent.width << 'x' << extent.height << ", camera x=" << cameraX
              << ", triangle pixels=" << redPixels << ", centroid=" << centroid << '\n';
    return centroid;
  }
}

int main(int argc, char** ppArgs) {
  dxvk::native::test::Window window;
  const bool shareOutput = argc == 2 && std::strcmp(ppArgs[1], "--gl-output") == 0;
  const bool useDlss = argc == 2 && std::strcmp(ppArgs[1], "--dlss") == 0;
  if (argc > 1 && !shareOutput && !useDlss) {
    std::cerr << "FAIL unexpected original frame test argument\n";
    return 1;
  }
  if (shareOutput) {
    if (!glfwInit()) {
      return 77;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    window.pHandle = glfwCreateWindow(128, 96, "Original Remix path-traced output verification", nullptr, nullptr);
    if (!window.pHandle) {
      return 77;
    }
    glfwMakeContextCurrent(window.pHandle);
  }
  try {
    dxvk::util::RtxFileSys::init(std::filesystem::current_path().u8string());
    dxvk::Logger::initRtxLog();
    dxvk::Rc<dxvk::DxvkInstance> instance = new dxvk::DxvkInstance();
    const auto adapter = instance->enumAdapters(0);
    if (adapter == nullptr || adapter->deviceProperties().deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
      return 77;
    }
    DeviceLifetime lifetime { adapter->createDevice(instance, dxvk::getRtxDeviceFeatures(adapter)) };
    const auto& device = lifetime.device;
    dxvk::g_dxvkDeviceNative = device.ptr();
    if (useDlss) {
      device->getCommon()->metaNGXContext().waitForInitialization();
      if (!device->getCommon()->metaDLSS().supportsDLSS()) {
        return 77;
      }
    }
    if (useDlss && !dxvk::RtxOptions::isDLSSEnabled()) {
      throw std::runtime_error("DLSS verification did not enable DLSS Super Resolution");
    }
    const auto context = device->createRtxContext();
    std::unique_ptr<dxvk::native::OffscreenOutput> sharedOutput;
    std::unique_ptr<dxvk::native::test::Reader> reader;
    if (shareOutput) {
      sharedOutput = std::make_unique<dxvk::native::OffscreenOutput>(device);
      reader = std::make_unique<dxvk::native::test::Reader>(sharedOutput->interopInfo());
    }
    const auto& assets = context->getSceneManager().getAssetReplacer();
    const auto mesh = reinterpret_cast<remixapi_MeshHandle>(uintptr_t(1));
    const auto material = reinterpret_cast<remixapi_MaterialHandle>(uintptr_t(2));
    dxvk::OpaqueMaterialData opaque;
    opaque.setAlbedoConstant(dxvk::Vector3(0.8f, 0.05f, 0.02f));
    opaque.setEnableEmission(true);
    opaque.setEmissiveColorConstant(dxvk::Vector3(1.0f, 0.015f, 0.005f));
    opaque.setEmissiveIntensity(4.0f);
    context->beginRecording(device->createCommandList());
    assets->makeMaterialWithTexturePreload(*context, material, dxvk::MaterialData(opaque));
    std::vector<dxvk::RasterGeometry> geometry;
    geometry.push_back(dxvk::native::test::createTriangle(device, material));
    assets->registerExternalMesh(mesh, std::move(geometry));

    const double initial = renderScene(device, context, mesh, { 96, 64, 1 }, 0.0f,
      "original-rtx-frame.ppm", sharedOutput.get(), reader.get());
    const auto fdCount = [] {
      return std::distance(std::filesystem::directory_iterator("/proc/self/fd"),
        std::filesystem::directory_iterator());
    };
    const auto baselineFds = fdCount();
    renderScene(device, context, mesh, { 128, 80, 1 }, 0.65f,
      "original-rtx-frame-right.ppm", sharedOutput.get(), reader.get());
    renderScene(device, context, mesh, { 80, 96, 1 }, -0.45f,
      "original-rtx-frame-left.ppm", sharedOutput.get(), reader.get());
    const double restored = renderScene(device, context, mesh, { 96, 64, 1 }, 0.0f,
      "original-rtx-frame-restored.ppm", sharedOutput.get(), reader.get());
    if (std::abs(initial - restored) > 0.02) {
      throw std::runtime_error("original camera/image state did not recover after resize");
    }
    if (shareOutput && fdCount() != baselineFds) {
      throw std::runtime_error("original color/depth output leaked descriptors across resize");
    }
    context->getSceneManager().destroyExternalMesh(context, mesh);
    assets->destroyExternalMaterial(material);
    context->flushCommandList();
    device->waitForIdle();
    std::cout << "PASS original Remix GPU path tracing, camera movement, resize and readback on "
              << adapter->deviceProperties().deviceName
              << (shareOutput ? ", with identical OpenGL FD color/depth\n" : "\n");
    return 0;
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "FAIL " << error.message() << '\n';
    return 1;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
