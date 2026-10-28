// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#define GLFW_INCLUDE_NONE
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <GLFW/glfw3.h>

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "dxvk/pch/dxvk.pch.h"
#include "dxvk/rtx_render/rtx_device_features.h"
#include "offscreen_output.h"
#include "gl_output_reader.h"
#include "util/util_filesys.h"
#include "util/util_scoped_fd.h"

namespace {
  using namespace dxvk::native::test;
  size_t fdCount() {
    return std::distance(std::filesystem::directory_iterator("/proc/self/fd"),
      std::filesystem::directory_iterator());
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
  dxvk::native::test::Window window;
  if (!glfwInit()) {
    return 77;
  }
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  window.pHandle = glfwCreateWindow(128, 96, "Original Remix native output verification", nullptr, nullptr);
  if (!window.pHandle) {
    return 77;
  }
  glfwMakeContextCurrent(window.pHandle);
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
    const auto context = device->createContext();
    context->beginRecording(device->createCommandList());
    dxvk::native::OffscreenOutput output(device);
    dxvk::native::test::Reader reader(output.interopInfo());
    requireFailure([&] { output.endFrame(context); });
    requireFailure([&] { output.present(1); });
    requireFailure([&] { output.beginFrame({ 0, 23, 1 }); });
    requireFailure([&] { output.beginFrame({ 37, 0, 1 }); });
    requireFailure([&] { output.beginFrame({ 37, 23, 2 }); });
    requireFailure([&] { output.beginFrame({ 1u << 14, 23, 1 }); });
    const std::array<VkExtent3D, 4> extents {{{ 37, 23, 1 }, { 63, 47, 1 }, { 17, 29, 1 }, { 37, 23, 1 }}};
    size_t baselineFdCount = 0;
    for (uint32_t frame = 0; frame < 24; frame++) {
      const auto& image = output.beginFrame(extents[(frame / 6) % extents.size()]);
      requireFailure([&] { output.interopInfo(); });
      requireFailure([&] { output.beginFrame(image->info().extent); });
      VkClearColorValue color { };
      color.float32[0] = float(frame + 1) / 32;
      color.float32[1] = 0.5f;
      color.float32[2] = 1 - color.float32[0];
      color.float32[3] = 1;
      context->clearColorImage(image, color, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
      output.endFrame(context);
      requireFailure([&] { output.endFrame(context); });
      requireFailure([&] { output.beginFrame(image->info().extent); });
      const auto pixels = reader.readAndPresent(output);
      const std::array<int, 4> expected { int(std::lround(color.float32[0] * 255)), 128,
        int(std::lround(color.float32[2] * 255)), 255 };
      for (size_t index = 0; index < pixels.size(); index++) {
        require(std::abs(int(pixels[index]) - expected[index % 4]) <= 1,
          "original-core pixels changed during Vulkan to OpenGL FD exchange");
      }
      glFinish();
      if (frame == 5) {
        baselineFdCount = fdCount();
      }
    }
    require(fdCount() == baselineFdCount, "original-core Vulkan/OpenGL output leaked descriptors across resize");
    std::cout << "PASS 24 original-core Vulkan/OpenGL FD frames, timeline synchronization and resize on "
              << glGetString(GL_RENDERER) << '\n';
    return 0;
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "FAIL " << error.message() << '\n';
    return 1;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
