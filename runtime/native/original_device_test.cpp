// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <new>
#include <stdexcept>

#include "dxvk/pch/dxvk.pch.h"
#include "dxvk/dxvk_state_cache.h"
#include "dxvk/rtx_render/rtx_device_features.h"
#include "util/util_filesys.h"

void verifyOriginalImageSharing(const dxvk::Rc<dxvk::DxvkDevice>& device,
  const dxvk::Rc<dxvk::DxvkContext>& context, dxvk::DxvkImageCreateInfo info);

namespace {
  void verifyEmptyCompilationCount(const dxvk::Rc<dxvk::DxvkDevice>& device) {
    alignas(dxvk::DxvkStateCache) std::array<unsigned char, sizeof(dxvk::DxvkStateCache)> storage;
    storage.fill(0xa5);
    auto* pCache = new (storage.data()) dxvk::DxvkStateCache(device.ptr(),
      &device->getCommon()->pipelineManager(), &device->getCommon()->renderPassPool());
    const auto count = pCache->remixShaderCompilationCount();
    pCache->~DxvkStateCache();
    if (count) {
      throw std::runtime_error("empty shader compiler inherited allocator bytes: " + std::to_string(count));
    }
  }

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
}

int main() {
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
    verifyEmptyCompilationCount(device);
    const auto context = device->createContext();

    constexpr uint32_t kWidth = 37;
    constexpr uint32_t kHeight = 23;
    dxvk::DxvkImageCreateInfo imageInfo { };
    imageInfo.type = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = { kWidth, kHeight, 1 };
    imageInfo.numLayers = 1;
    imageInfo.mipLevels = 1;
    imageInfo.sampleCount = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
    imageInfo.access = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.layout = VK_IMAGE_LAYOUT_GENERAL;
    const auto image = device->createImage(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      dxvk::DxvkMemoryStats::Category::AppTexture, "native original-core verification image");
    dxvk::DxvkBufferCreateInfo bufferInfo { };
    bufferInfo.size = kWidth * kHeight * 4;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.stages = VK_PIPELINE_STAGE_HOST_BIT;
    bufferInfo.access = VK_ACCESS_HOST_READ_BIT;
    const auto readback = device->createBuffer(bufferInfo,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      dxvk::DxvkMemoryStats::Category::AppBuffer, "native original-core verification readback");

    context->beginRecording(device->createCommandList());
    VkClearColorValue color { };
    color.float32[0] = 0.125f;
    color.float32[1] = 0.5f;
    color.float32[2] = 0.875f;
    color.float32[3] = 1.0f;
    context->clearColorImage(image, color, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
    context->copyImageToBuffer(readback, 0, 4, 4, image,
      { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, imageInfo.extent);
    context->flushCommandList();
    device->waitForIdle();
    const auto* pPixels = static_cast<const unsigned char*>(readback->mapPtr(0));
    const std::array<int, 4> expected { 32, 128, 223, 255 };
    for (size_t pixel = 0; pixel < kWidth * kHeight; pixel++) {
      for (size_t channel = 0; channel < 4; channel++) {
        if (std::abs(static_cast<int>(pPixels[pixel * 4 + channel]) - expected[channel]) > 1) {
          throw std::runtime_error("original DxvkContext GPU image/readback pixel mismatch");
        }
      }
    }
    verifyOriginalImageSharing(device, context, imageInfo);
    std::cout << "PASS original DxvkInstance, DxvkDevice, scene/pass objects and GPU image readback on "
              << adapter->deviceProperties().deviceName << '\n';
    return 0;
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "FAIL " << error.message() << '\n';
    return 1;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
