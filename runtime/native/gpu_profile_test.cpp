// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <iostream>
#include <stdexcept>
#include <vector>

#include "dxvk/rtx_render/rtx_gpu_profile.h"
#include "util/log/log.h"
#include "vulkan/vulkan_loader.h"

namespace dxvk {
  Logger Logger::s_instance("native-gpu-profile.log", LogLevel::None);
}

namespace {
  void check(VkResult result) {
    if (result != VK_SUCCESS) {
      throw std::runtime_error("Vulkan device query failed: " + std::to_string(result));
    }
  }
}

int main() {
  try {
    dxvk::Logger::initRtxLog();
    dxvk::Rc<dxvk::vk::LibraryFn> library = new dxvk::vk::LibraryFn();
    VkApplicationInfo app { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "Remix native GPU profile verification";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo createInfo { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    createInfo.pApplicationInfo = &app;
    VkInstance instance { };
    check(library->vkCreateInstance(&createInfo, nullptr, &instance));
    dxvk::Rc<dxvk::vk::InstanceFn> vki = new dxvk::vk::InstanceFn(true, instance);
    uint32_t count = 0;
    check(vki->vkEnumeratePhysicalDevices(instance, &count, nullptr));
    std::vector<VkPhysicalDevice> devices(count);
    check(vki->vkEnumeratePhysicalDevices(instance, &count, devices.data()));
    for (const auto device : devices) {
      dxvk::DxvkDeviceInfo info { };
      info.core.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
      info.coreDeviceId.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
      info.core.pNext = &info.coreDeviceId;
      vki->vkGetPhysicalDeviceProperties2(device, &info.core);
      if (info.core.properties.vendorID != 0x10DE) {
        continue;
      }
      VkPhysicalDeviceMemoryProperties memory { };
      vki->vkGetPhysicalDeviceMemoryProperties(device, &memory);
      dxvk::DxvkAdapterMemoryInfo budget { };
      dxvk::GpuProfile::detect(info, memory, budget);
      const auto arch = dxvk::GpuProfile::getNativeNvidiaArch();
      if (!arch) {
        throw std::runtime_error("NVML could not match the real Vulkan adapter UUID");
      }
      if (info.core.properties.deviceID == 0x1F08 && *arch != dxvk::kNvidiaArchTuring) {
        throw std::runtime_error("RTX 2060 was not identified as Turing");
      }
      const auto& hardware = dxvk::GpuProfile::getHardwareInfo();
      const auto preset = dxvk::selectAutoPreset(hardware, std::nullopt, *arch);
      if (info.core.properties.deviceID == 0x1F08
          && (preset.preset != dxvk::GraphicsPreset::Low || !preset.lowMemoryGpu)) {
        throw std::runtime_error("RTX 2060 did not receive the original low-memory quality defaults");
      }
      for (auto& byte : info.coreDeviceId.deviceUUID) {
        byte = 0;
      }
      dxvk::GpuProfile::detect(info, memory, budget);
      if (dxvk::GpuProfile::getNativeNvidiaArch()) {
        throw std::runtime_error("An unmatched UUID selected another NVIDIA device");
      }
      std::cout << "PASS native NVML UUID match, architecture and original quality selection on "
                << info.core.properties.deviceName << '\n';
      return 0;
    }
    return 77;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
