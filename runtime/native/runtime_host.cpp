// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include "runtime_host.h"

#include "dxvk/dxvk_instance.h"
#include "dxvk/rtx_render/rtx_context.h"
#include "dxvk/rtx_render/rtx_device_features.h"
#include "dxvk/imgui/dxvk_imgui.h"
#include "util/util_filesys.h"

namespace dxvk::native {
  struct RuntimeHost::Impl {
    enum class Phase { Idle, Recording, Submitted, Failed };
    Rc<DxvkInstance> instance;
    Rc<DxvkDevice> device;
    Rc<RtxContext> context;
    std::unique_ptr<OffscreenOutput> output;
    Rc<DxvkImage> image;
    Rc<DxvkImageView> overlayView;
    bool overlayEnabled = false;
    bool overlayVisible = false;
    Phase phase = Phase::Idle;

    void initialize(const std::string& rootPath, const std::function<void()>& configure) {
      if (g_dxvkDeviceNative) {
        throw DxvkError("A Remix device is already registered in this process");
      }
      util::RtxFileSys::init(rootPath);
      Logger::initRtxLog();
      instance = new DxvkInstance();
      if (configure) {
        configure();
      }
      const auto adapter = instance->enumAdapters(0);
      if (adapter == nullptr || adapter->deviceProperties().deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
        throw DxvkError("The native Remix host requires a ray-tracing GPU");
      }
      device = adapter->createDevice(instance, getRtxDeviceFeatures(adapter));
      g_dxvkDeviceNative = device.ptr();
      context = device->createRtxContext();
      context->beginRecording(device->createCommandList());
      output = std::make_unique<OffscreenOutput>(device);
    }

    ~Impl() {
      if (device != nullptr) {
        if (context != nullptr) {
          context->flushCommandList();
        }
        device->waitForIdle();
        if (context != nullptr) {
          context->getSceneManager().clear(context, false);
        }
        output.reset();
        overlayView = nullptr;
        image = nullptr;
        context = nullptr;
        device->getCommon()->onDestroy();
        if (g_dxvkDeviceNative == device.ptr()) {
          g_dxvkDeviceNative = nullptr;
        }
      }
    }
  };

  RuntimeHost::RuntimeHost(const std::string& rootPath, const std::function<void()>& configure)
  : m_pImpl(std::make_unique<Impl>()) {
    m_pImpl->initialize(rootPath, configure);
  }

  RuntimeHost::~RuntimeHost() = default;

  const Rc<DxvkDevice>& RuntimeHost::device() const {
    return m_pImpl->device;
  }

  const Rc<RtxContext>& RuntimeHost::context() const {
    return m_pImpl->context;
  }

  void RuntimeHost::enableOverlay() {
    m_pImpl->overlayEnabled = true;
  }

  void RuntimeHost::overlayInput(uint32_t type, int32_t code, double x, double y) {
    m_pImpl->device->getCommon()->getImgui().nativeInput(type, code, x, y);
  }

  bool RuntimeHost::beginFrame(VkExtent3D extent) {
    auto& state = *m_pImpl;
    if (state.phase != Impl::Phase::Idle) {
      throw DxvkError("The native host must present its submitted frame before beginning another");
    }
    if (extent.depth != 1) {
      throw DxvkError("The native host requires a two-dimensional output");
    }
    if (!extent.width || !extent.height) {
      return false;
    }
    const auto limit = state.device->properties().core.properties.limits.maxImageDimension2D;
    if (extent.width >= (1u << 14) || extent.height >= (1u << 14)
        || extent.width > limit || extent.height > limit) {
      throw DxvkError("The native host output exceeds the renderer's supported extent");
    }
    try {
      state.context->resetScreenResolution(extent);
      // Shared depth is exported at BeginFrame, before the API submits its camera.
      state.context->prepareScreenResolution();
      const auto depthExtent = state.context->getResourceManager().getDownscaleDimensions();
      state.image = state.output->beginFrame(extent, depthExtent, state.overlayEnabled);
      state.phase = Impl::Phase::Recording;
      return true;
    } catch (...) {
      state.phase = Impl::Phase::Failed;
      throw;
    }
  }

  void RuntimeHost::submit() {
    auto& state = *m_pImpl;
    if (state.phase != Impl::Phase::Recording) {
      throw DxvkError("The native host has no frame to submit");
    }
    try {
      const auto frameId = state.device->getCurrentFrameId();
      const VkClearColorValue black { };
      state.context->clearColorImage(state.image, black, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
      state.context->endFrame(frameId, state.image);
      state.context->onPresent(state.image);
      if (state.overlayEnabled) {
        const auto& image = state.output->overlayImage();
        if (state.overlayView == nullptr || state.overlayView->image() != image) {
          DxvkImageViewCreateInfo view { };
          view.type = VK_IMAGE_VIEW_TYPE_2D;
          view.format = image->info().format;
          view.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
          view.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
          view.numLevels = view.numLayers = 1;
          state.overlayView = state.device->createImageView(image, view);
        }
        state.context->clearColorImage(image, black, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
        DxvkRenderTargets targets { };
        targets.color[0].view = state.overlayView;
        targets.color[0].layout = VK_IMAGE_LAYOUT_GENERAL;
        state.context->bindRenderTargets(targets);
        state.device->getCommon()->getImgui().render(state.context,
          { image->info().extent.width, image->info().extent.height });
        const auto* pDrawData = ImGui::GetDrawData();
        state.overlayVisible = pDrawData && pDrawData->TotalVtxCount;
      }
      state.output->endFrame(state.context,
        state.context->didRaytraceFrame()
          ? state.context->getResourceManager().getRaytracingOutput().m_primaryDepth.image : nullptr);
      state.phase = Impl::Phase::Submitted;
    } catch (...) {
      state.phase = Impl::Phase::Failed;
      throw;
    }
  }

  void RuntimeHost::present(uint64_t clientDoneValue, bool waitForClient) {
    auto& state = *m_pImpl;
    if (state.phase != Impl::Phase::Submitted) {
      throw DxvkError("The native host has no submitted frame to present");
    }
    state.output->present(clientDoneValue, waitForClient);
    state.device->incrementPresentCount();
    state.image = nullptr;
    state.phase = Impl::Phase::Idle;
  }

  OutputInteropInfo RuntimeHost::interopInfo() const {
    if (m_pImpl->phase == Impl::Phase::Failed) {
      throw DxvkError("The native host cannot publish a failed frame");
    }
    auto info = m_pImpl->output->interopInfo();
    info.overlayVisible = m_pImpl->overlayVisible;
    return info;
  }
}
