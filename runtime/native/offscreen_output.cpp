// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include "offscreen_output.h"

#include <array>

#include "dxvk/dxvk_context.h"
#include "dxvk/rtx_render/rtx_semaphore.h"
#include "util/util_scoped_fd.h"

namespace dxvk::native {
  namespace {
    constexpr uint64_t kWaitTimeout = 10'000'000'000;

    void checkResult(VkResult result, const char* pOperation) {
      if (result != VK_SUCCESS) {
        throw DxvkError(str::format(pOperation, " failed: ", result));
      }
    }

    struct QueueLock {
      DxvkDevice& device;
      explicit QueueLock(DxvkDevice& value) : device(value) {
        device.lockSubmission();
      }
      ~QueueLock() {
        device.unlockSubmission();
      }
    };
  }

  struct OffscreenOutput::Impl {
    Rc<DxvkDevice> device;
    Rc<vk::DeviceFn> vkd;
    std::unique_ptr<RtxSemaphore> rendererDone;
    std::unique_ptr<RtxSemaphore> clientDone;
    Rc<DxvkImage> image;
    util::ScopedFd imageFd;
    Rc<DxvkImage> depth;
    util::ScopedFd depthFd;
    Rc<DxvkImage> overlay;
    util::ScopedFd overlayFd;
    VkCommandPool pool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, 2> commands { };
    std::array<VkFence, 2> fences { };
    uint64_t rendererValue = 0;
    uint64_t clientValue = 0;
    std::array<bool, 2> inFlight { };
    bool active = false;
    bool published = false;
    bool externalOwner = false;
    bool depthExternalOwner = false;
    bool overlayExternalOwner = false;

    explicit Impl(const Rc<DxvkDevice>& value) : device(value), vkd(value->vkd()) {
      rendererDone.reset(RtxSemaphore::createTimeline(vkd, "native renderer done", 0, true));
      clientDone.reset(RtxSemaphore::createTimeline(vkd, "native client done", 0, true));
    }

    void initialize() {
      VkCommandPoolCreateInfo poolInfo { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
      poolInfo.queueFamilyIndex = device->queues().graphics.queueFamily;
      checkResult(vkd->vkCreateCommandPool(device->handle(), &poolInfo, nullptr, &pool), "vkCreateCommandPool");
      VkCommandBufferAllocateInfo allocateInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
      allocateInfo.commandPool = pool;
      allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      allocateInfo.commandBufferCount = commands.size();
      checkResult(vkd->vkAllocateCommandBuffers(device->handle(), &allocateInfo, commands.data()), "vkAllocateCommandBuffers");
      VkFenceCreateInfo fenceInfo { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
      for (auto& fence : fences) {
        checkResult(vkd->vkCreateFence(device->handle(), &fenceInfo, nullptr, &fence), "vkCreateFence");
      }
    }

    ~Impl() {
      for (size_t index = 0; index < fences.size(); index++) {
        if (inFlight[index]) {
          const auto result = vkd->vkWaitForFences(device->handle(), 1, &fences[index], VK_TRUE, kWaitTimeout);
          if (result != VK_SUCCESS) {
            Logger::err(str::format("Native output teardown wait failed: ", result));
            // Vulkan resources must outlive submitted work, including after a bounded wait expires.
            device->waitForIdle();
          }
        }
        if (fences[index] != VK_NULL_HANDLE) {
          vkd->vkDestroyFence(device->handle(), fences[index], nullptr);
        }
      }
      if (pool != VK_NULL_HANDLE) {
        vkd->vkDestroyCommandPool(device->handle(), pool, nullptr);
      }
    }

    void waitClient() {
      if (!clientValue) {
        return;
      }
      const auto semaphore = clientDone->handle();
      VkSemaphoreWaitInfo info { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
      info.semaphoreCount = 1;
      info.pSemaphores = &semaphore;
      info.pValues = &clientValue;
      checkResult(vkd->vkWaitSemaphores(device->handle(), &info, kWaitTimeout), "native client completion wait");
    }

    void validateExtent(VkExtent3D extent, bool optional) const {
      if (optional && !extent.width && !extent.height && extent.depth == 1) {
        return;
      }
      const auto maxExtent = device->properties().core.properties.limits.maxImageDimension2D;
      if (!extent.width || !extent.height || extent.depth != 1
          || extent.width >= (1u << 14) || extent.height >= (1u << 14)
          || extent.width > maxExtent || extent.height > maxExtent) {
        throw DxvkError("Invalid native output extent");
      }
    }

    void ensureImage(Rc<DxvkImage>& target, util::ScopedFd& targetFd, bool& targetExternalOwner,
        VkExtent3D extent, VkFormat format, const char* pName) {
      if (target != nullptr && target->info().extent == extent) {
        return;
      }
      if (!extent.width && target == nullptr) {
        return;
      }
      waitClient();
      if (!extent.width) {
        target = nullptr;
        targetFd.reset();
        targetExternalOwner = false;
        return;
      }
      DxvkImageCreateInfo info { };
      info.type = VK_IMAGE_TYPE_2D;
      info.format = format;
      info.extent = extent;
      info.numLayers = 1;
      info.mipLevels = 1;
      info.sampleCount = VK_SAMPLE_COUNT_1_BIT;
      info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
        | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
      info.stages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      info.access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
      info.tiling = VK_IMAGE_TILING_OPTIMAL;
      info.layout = VK_IMAGE_LAYOUT_GENERAL;
      info.sharing.mode = DxvkSharedHandleMode::Export;
      info.sharing.type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
      auto replacement = device->createImage(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        DxvkMemoryStats::Category::RTXRenderTarget, pName);
      util::ScopedFd replacementFd(replacement->sharedFd());
      if (replacementFd.get() < 0) {
        throw DxvkError("Native output image export failed");
      }
      target = std::move(replacement);
      targetFd = std::move(replacementFd);
      targetExternalOwner = false;
    }

    void recordBarrier(VkCommandBuffer command, bool release) {
      VkCommandBufferBeginInfo begin { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
      begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      checkResult(vkd->vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
      std::array<VkImageMemoryBarrier, 3> barriers { };
      uint32_t count = 0;
      const auto addImage = [&](const Rc<DxvkImage>& target, bool external) {
        if (target == nullptr) {
          return;
        }
        auto& barrier = barriers[count++];
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = release ? VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT : 0;
        barrier.dstAccessMask = release ? 0 : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.oldLayout = external || release ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = release ? device->queues().graphics.queueFamily
          : external ? VK_QUEUE_FAMILY_EXTERNAL : VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = release ? VK_QUEUE_FAMILY_EXTERNAL
          : external ? device->queues().graphics.queueFamily : VK_QUEUE_FAMILY_IGNORED;
        barrier.image = target->handle();
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
      };
      addImage(image, externalOwner);
      addImage(depth, depthExternalOwner);
      addImage(overlay, overlayExternalOwner);
      vkd->vkCmdPipelineBarrier(command,
        release ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        release ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 0, nullptr, 0, nullptr, count, barriers.data());
      checkResult(vkd->vkEndCommandBuffer(command), "vkEndCommandBuffer");
    }

    void submit(VkCommandBuffer command, bool release) {
      const auto waitSemaphore = clientDone->handle();
      const auto signalSemaphore = rendererDone->handle();
      const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      const uint64_t signalValue = rendererValue + 1;
      VkTimelineSemaphoreSubmitInfo timeline { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
      timeline.waitSemaphoreValueCount = !release && clientValue ? 1 : 0;
      timeline.pWaitSemaphoreValues = &clientValue;
      timeline.signalSemaphoreValueCount = release ? 1 : 0;
      timeline.pSignalSemaphoreValues = &signalValue;
      VkSubmitInfo submitInfo { VK_STRUCTURE_TYPE_SUBMIT_INFO };
      submitInfo.pNext = &timeline;
      submitInfo.waitSemaphoreCount = timeline.waitSemaphoreValueCount;
      submitInfo.pWaitSemaphores = &waitSemaphore;
      submitInfo.pWaitDstStageMask = &waitStage;
      submitInfo.commandBufferCount = 1;
      submitInfo.pCommandBuffers = &command;
      submitInfo.signalSemaphoreCount = timeline.signalSemaphoreValueCount;
      submitInfo.pSignalSemaphores = &signalSemaphore;
      QueueLock lock(*device);
      checkResult(vkd->vkQueueSubmit(device->queues().graphics.queueHandle, 1, &submitInfo,
        fences[release ? 1 : 0]), "native output queue submission");
      inFlight[release ? 1 : 0] = true;
      if (release) {
        rendererValue = signalValue;
      }
    }
  };

  OffscreenOutput::OffscreenOutput(const Rc<DxvkDevice>& device) {
    if (device == nullptr) {
      throw DxvkError("Native output requires an initialized device");
    }
    m_pImpl = std::make_unique<Impl>(device);
    m_pImpl->initialize();
  }

  OffscreenOutput::~OffscreenOutput() = default;

  const Rc<DxvkImage>& OffscreenOutput::beginFrame(VkExtent3D extent, VkExtent3D depthExtent, bool overlay) {
    auto& state = *m_pImpl;
    if (state.active || state.published) {
      throw DxvkError("Native output frame must be ended and presented before reuse");
    }
    state.validateExtent(extent, false);
    state.validateExtent(depthExtent, true);
    for (size_t index = 0; index < state.fences.size(); index++) {
      if (state.inFlight[index]) {
        checkResult(state.vkd->vkWaitForFences(state.device->handle(), 1, &state.fences[index], VK_TRUE, kWaitTimeout),
          "native output command-buffer reuse wait");
        state.inFlight[index] = false;
      }
    }
    state.ensureImage(state.image, state.imageFd, state.externalOwner, extent, VK_FORMAT_R8G8B8A8_UNORM,
      "native original-renderer shared color");
    state.ensureImage(state.depth, state.depthFd, state.depthExternalOwner, depthExtent, VK_FORMAT_R32_SFLOAT,
      "native original-renderer shared depth");
    state.ensureImage(state.overlay, state.overlayFd, state.overlayExternalOwner,
      overlay ? extent : VkExtent3D { 0, 0, 1 }, VK_FORMAT_R8G8B8A8_UNORM, "native Remix overlay");
    checkResult(state.vkd->vkResetCommandPool(state.device->handle(), state.pool, 0), "vkResetCommandPool");
    checkResult(state.vkd->vkResetFences(state.device->handle(), state.fences.size(), state.fences.data()), "vkResetFences");
    state.recordBarrier(state.commands[0], false);
    state.submit(state.commands[0], false);
    state.active = true;
    return state.image;
  }

  const Rc<DxvkImage>& OffscreenOutput::overlayImage() const {
    return m_pImpl->overlay;
  }

  void OffscreenOutput::endFrame(const Rc<DxvkContext>& context, const Rc<DxvkImage>& depthSource) {
    auto& state = *m_pImpl;
    if (!state.active) {
      throw DxvkError("No native output frame is active");
    }
    if (context == nullptr || context->getDevice().ptr() != state.device.ptr()) {
      throw DxvkError("Native output context belongs to a different device");
    }
    if (state.depth != nullptr) {
      if (depthSource != nullptr && (depthSource->info().format != VK_FORMAT_R32_SFLOAT
          || depthSource->info().extent != state.depth->info().extent)) {
        throw DxvkError("Native depth output requires an R32_SFLOAT source with the requested extent");
      }
      if (depthSource != nullptr) {
        const VkImageSubresourceLayers layers { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        context->copyImage(state.depth, layers, { 0, 0, 0 }, depthSource, layers, { 0, 0, 0 }, state.depth->info().extent);
      } else {
        const VkClearColorValue farDepth { { 1.0f, 0.0f, 0.0f, 0.0f } };
        context->clearColorImage(state.depth, farDepth, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
      }
    } else if (depthSource != nullptr) {
      throw DxvkError("Native depth output extent was not requested at frame begin");
    }
    context->flushCommandList();
    state.recordBarrier(state.commands[1], true);
    state.submit(state.commands[1], true);
    state.externalOwner = true;
    state.depthExternalOwner = state.depth != nullptr;
    state.overlayExternalOwner = state.overlay != nullptr;
    state.active = false;
    state.published = true;
  }

  void OffscreenOutput::present(uint64_t clientDoneValue, bool waitForClient) {
    auto& state = *m_pImpl;
    if (!state.published || clientDoneValue <= state.clientValue) {
      throw DxvkError("Native output requires a new client completion value for the published frame");
    }
    state.clientValue = clientDoneValue;
    if (waitForClient) {
      state.waitClient();
    }
    state.published = false;
  }

  OutputInteropInfo OffscreenOutput::interopInfo() const {
    const auto& state = *m_pImpl;
    if (state.active) {
      throw DxvkError("Native output interop information is unavailable until the frame is ended");
    }
    OutputInteropInfo info;
    info.rendererDoneFd = state.rendererDone->sharedHandle();
    info.clientDoneFd = state.clientDone->sharedHandle();
    info.rendererDoneValue = state.rendererValue;
    if (state.image != nullptr && state.externalOwner) {
      info.imageFd = state.imageFd.get();
      info.allocationSize = state.image->memSize();
      info.extent = state.image->info().extent;
    }
    if (state.depth != nullptr && state.depthExternalOwner) {
      info.depthFd = state.depthFd.get();
      info.depthAllocationSize = state.depth->memSize();
      info.depthExtent = state.depth->info().extent;
    }
    if (state.overlay != nullptr && state.overlayExternalOwner) {
      info.overlayFd = state.overlayFd.get();
      info.overlayAllocationSize = state.overlay->memSize();
      info.overlayExtent = state.overlay->info().extent;
    }
    return info;
  }
}
