// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <array>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "dxvk/pch/dxvk.pch.h"
#include "util/util_scoped_fd.h"

namespace {
  PFN_vkAllocateMemory s_pAllocateMemory;
  PFN_vkCreateImage s_pCreateImage;
  PFN_vkDestroyImage s_pDestroyImage;
  std::atomic<uint32_t> s_importFailures { 0 };
  std::atomic<bool> s_failAllImports { false };
  std::atomic<uint32_t> s_failedImports { 0 };
  std::atomic<int> s_liveImages { 0 };

  VkResult VKAPI_CALL allocateMemory(VkDevice device, const VkMemoryAllocateInfo* pInfo,
      const VkAllocationCallbacks* pCallbacks, VkDeviceMemory* pMemory) {
    auto* pNext = static_cast<const VkBaseInStructure*>(pInfo->pNext);
    while (pNext && pNext->sType != VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR) {
      pNext = pNext->pNext;
    }
    if (pNext && (s_failAllImports || s_importFailures)) {
      if (s_importFailures) {
        --s_importFailures;
      }
      ++s_failedImports;
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    return s_pAllocateMemory(device, pInfo, pCallbacks, pMemory);
  }

  VkResult VKAPI_CALL createImage(VkDevice device, const VkImageCreateInfo* pInfo,
      const VkAllocationCallbacks* pCallbacks, VkImage* pImage) {
    const auto result = s_pCreateImage(device, pInfo, pCallbacks, pImage);
    if (result == VK_SUCCESS) {
      ++s_liveImages;
    }
    return result;
  }

  void VKAPI_CALL destroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pCallbacks) {
    s_pDestroyImage(device, image, pCallbacks);
    --s_liveImages;
  }

  struct AllocationFaults {
    dxvk::Rc<dxvk::vk::DeviceFn> vkd;
    explicit AllocationFaults(const dxvk::Rc<dxvk::vk::DeviceFn>& functions) : vkd(functions) {
      s_pAllocateMemory = vkd->vkAllocateMemory;
      s_pCreateImage = vkd->vkCreateImage;
      s_pDestroyImage = vkd->vkDestroyImage;
      s_failedImports = 0;
      s_liveImages = 0;
      vkd->vkAllocateMemory = allocateMemory;
      vkd->vkCreateImage = createImage;
      vkd->vkDestroyImage = destroyImage;
    }
    ~AllocationFaults() {
      vkd->vkAllocateMemory = s_pAllocateMemory;
      vkd->vkCreateImage = s_pCreateImage;
      vkd->vkDestroyImage = s_pDestroyImage;
      s_failAllImports = false;
      s_importFailures = 0;
    }
  };

  size_t fdCount() {
    return std::distance(std::filesystem::directory_iterator("/proc/self/fd"),
      std::filesystem::directory_iterator());
  }

  void checkSharedImage(const dxvk::Rc<dxvk::DxvkDevice>& device,
      const dxvk::Rc<dxvk::DxvkContext>& context, dxvk::DxvkImageCreateInfo info) {
    info.flags |= VK_IMAGE_CREATE_ALIAS_BIT;
    info.sharing.mode = dxvk::DxvkSharedHandleMode::Export;
    info.sharing.type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    const auto source = device->createImage(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      dxvk::DxvkMemoryStats::Category::AppTexture, "native shared-image source");
    dxvk::util::ScopedFd fd(source->sharedFd());
    if (fd.get() < 3 || !(::fcntl(fd.get(), F_GETFD) & FD_CLOEXEC)) {
      throw std::runtime_error("original image did not export an owned CLOEXEC FD");
    }
    info.sharing.mode = dxvk::DxvkSharedHandleMode::Import;
    info.sharing.fd = fd.get();
    const auto imported = device->createImage(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      dxvk::DxvkMemoryStats::Category::AppTexture, "native shared-image import");
    if (::fcntl(fd.get(), F_GETFD) < 0) {
      throw std::runtime_error("original image import consumed the caller's borrowed FD");
    }
    dxvk::DxvkBufferCreateInfo bufferInfo { };
    bufferInfo.size = info.extent.width * info.extent.height * 4;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.stages = VK_PIPELINE_STAGE_HOST_BIT;
    bufferInfo.access = VK_ACCESS_HOST_READ_BIT;
    const auto readback = device->createBuffer(bufferInfo,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      dxvk::DxvkMemoryStats::Category::AppBuffer, "native shared-image verification");
    const VkImageSubresourceRange range { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    context->initImage(imported, range, VK_IMAGE_LAYOUT_UNDEFINED);
    VkClearColorValue color { };
    color.float32[0] = 0.25f;
    color.float32[1] = 0.75f;
    color.float32[2] = 0.5f;
    color.float32[3] = 1.0f;
    context->clearColorImage(source, color, range);
    context->emitMemoryBarrier(0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    context->copyImageToBuffer(readback, 0, 4, 4, imported,
      { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, info.extent);
    context->flushCommandList();
    device->waitForIdle();
    const auto* pPixels = static_cast<const unsigned char*>(readback->mapPtr(0));
    const std::array<int, 4> expected { 64, 191, 128, 255 };
    for (size_t pixel = 0; pixel < info.extent.width * info.extent.height; pixel++) {
      for (size_t channel = 0; channel < expected.size(); channel++) {
        if (std::abs(static_cast<int>(pPixels[pixel * 4 + channel]) - expected[channel]) > 1) {
          throw std::runtime_error("FD-imported image does not contain the source GPU pixels");
        }
      }
    }
  }
}

void verifyOriginalImageSharing(const dxvk::Rc<dxvk::DxvkDevice>& device,
    const dxvk::Rc<dxvk::DxvkContext>& context, dxvk::DxvkImageCreateInfo info) {
  // NGX uses driver FDs and allocations during discovery. Finish it before counting FDs or injecting allocation faults.
  device->getCommon()->metaNGXContext().waitForInitialization();
  checkSharedImage(device, context, info);
  const auto baseline = fdCount();
  for (uint32_t iteration = 0; iteration < 24; iteration++) {
    info.extent = { 17 + iteration, 9 + iteration % 3, 1 };
    checkSharedImage(device, context, info);
  }
  if (fdCount() != baseline) {
    throw std::runtime_error("original external image exchange leaked file descriptors");
  }
  std::cout << "PASS original external image exchange: " << baseline << " FDs before and after 24 exchanges\n";
  info.sharing.mode = dxvk::DxvkSharedHandleMode::Export;
  info.sharing.type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
  const auto source = device->createImage(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    dxvk::DxvkMemoryStats::Category::AppTexture, "native image allocation failure source");
  dxvk::util::ScopedFd fd(source->sharedFd());
  info.sharing.mode = dxvk::DxvkSharedHandleMode::Import;
  info.sharing.fd = fd.get();
  device->waitForIdle();
  {
    AllocationFaults faults(device->vkd());
    s_importFailures = 1;
    {
      const auto imported = device->createImage(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        dxvk::DxvkMemoryStats::Category::AppTexture, "native import allocation retry");
    }
    if (s_failedImports != 1 || s_liveImages || ::fcntl(fd.get(), F_GETFD) < 0) {
      throw std::runtime_error("native image allocation retry lost ownership or leaked an image");
    }
    s_failAllImports = true;
    bool rejected = false;
    try {
      device->createImage(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        dxvk::DxvkMemoryStats::Category::AppTexture, "native failed import allocation");
    } catch (const dxvk::DxvkError&) {
      rejected = true;
    }
    if (!rejected || s_liveImages || ::fcntl(fd.get(), F_GETFD) < 0) {
      throw std::runtime_error("failed native imports allocated private memory or leaked resources");
    }
  }
  std::cout << "PASS 24 original image FD exchanges with GPU pixel readback, allocation retry and failure cleanup\n";
}
