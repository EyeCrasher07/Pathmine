#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <unistd.h>

#include "vulkan/vulkan_loader.h"

namespace {
  void check(VkResult result, const char* pOperation) {
    if (result != VK_SUCCESS) {
      throw std::runtime_error(std::string(pOperation) + ": " + std::to_string(result));
    }
  }

  template<typename T>
  T info(VkStructureType type) {
    T result{};
    result.sType = type;
    return result;
  }

  size_t fdCount() {
    return static_cast<size_t>(std::distance(std::filesystem::directory_iterator("/proc/self/fd"),
      std::filesystem::directory_iterator()));
  }
}

int main() {
  try {
    dxvk::Rc<dxvk::vk::LibraryFn> library = new dxvk::vk::LibraryFn();
    auto app = info<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
    app.pApplicationName = "Original Remix native platform verification";
    app.apiVersion = VK_API_VERSION_1_2;
    auto createInstance = info<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
    createInstance.pApplicationInfo = &app;
    VkInstance instance{};
    check(library->vkCreateInstance(&createInstance, nullptr, &instance), "create instance using original loader");
    dxvk::Rc<dxvk::vk::InstanceFn> vki = new dxvk::vk::InstanceFn(true, instance);
    uint32_t count = 0;
    check(vki->vkEnumeratePhysicalDevices(instance, &count, nullptr), "count physical devices");
    std::vector<VkPhysicalDevice> physicalDevices(count);
    check(vki->vkEnumeratePhysicalDevices(instance, &count, physicalDevices.data()), "enumerate physical devices");
    for (const auto physical : physicalDevices) {
      auto properties = info<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
      vki->vkGetPhysicalDeviceProperties2(physical, &properties);
      if (properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
        continue;
      }
      std::cout << "Hardware: " << properties.properties.deviceName << '\n';
      uint32_t familyCount = 0;
      vki->vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
      std::vector<VkQueueFamilyProperties> families(familyCount);
      vki->vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data());
      uint32_t queueFamily = 0;
      while (queueFamily < familyCount && !(families[queueFamily].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
        ++queueFamily;
      }
      if (queueFamily == familyCount) {
        continue;
      }
      const float priority = 1.0f;
      auto queueInfo = info<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
      queueInfo.queueFamilyIndex = queueFamily;
      queueInfo.queueCount = 1;
      queueInfo.pQueuePriorities = &priority;
      const char* extensions[] = {VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME};
      auto timeline = info<VkPhysicalDeviceTimelineSemaphoreFeatures>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES);
      timeline.timelineSemaphore = true;
      auto deviceInfo = info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
      deviceInfo.pNext = &timeline;
      deviceInfo.queueCreateInfoCount = 1;
      deviceInfo.pQueueCreateInfos = &queueInfo;
      deviceInfo.enabledExtensionCount = 1;
      deviceInfo.ppEnabledExtensionNames = extensions;
      VkDevice device{};
      check(vki->vkCreateDevice(physical, &deviceInfo, nullptr, &device), "create native Vulkan device");
      dxvk::Rc<dxvk::vk::DeviceFn> vkd = new dxvk::vk::DeviceFn(true, instance, device);
      if (!vkd->vkGetSemaphoreFdKHR || !vkd->vkImportSemaphoreFdKHR) {
        throw std::runtime_error("Native external semaphore functions were not loaded");
      }
      VkQueue queue{};
      vkd->vkGetDeviceQueue(device, queueFamily, 0, &queue);
      const auto baseline = fdCount();
      for (uint32_t iteration = 0; iteration < 32; ++iteration) {
        auto type = info<VkSemaphoreTypeCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
        type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        auto exportInfo = info<VkExportSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO);
        exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
        type.pNext = &exportInfo;
        auto semaphoreInfo = info<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        semaphoreInfo.pNext = &type;
        std::array<VkSemaphore, 2> semaphores{};
        check(vkd->vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphores[0]), "create exportable semaphore");
        type.pNext = nullptr;
        check(vkd->vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphores[1]), "create imported semaphore");
        auto fdInfo = info<VkSemaphoreGetFdInfoKHR>(VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR);
        fdInfo.semaphore = semaphores[0];
        fdInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
        int fd = -1;
        check(vkd->vkGetSemaphoreFdKHR(device, &fdInfo, &fd), "export owned Linux FD");
        auto importInfo = info<VkImportSemaphoreFdInfoKHR>(VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR);
        importInfo.semaphore = semaphores[1];
        importInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
        importInfo.fd = fd;
        const auto imported = vkd->vkImportSemaphoreFdKHR(device, &importInfo);
        if (imported != VK_SUCCESS) {
          close(fd);
          check(imported, "import Linux FD");
        }
        const uint64_t value = 17 + iteration;
        auto values = info<VkTimelineSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO);
        values.signalSemaphoreValueCount = 1;
        values.pSignalSemaphoreValues = &value;
        auto submit = info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
        submit.pNext = &values;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &semaphores[0];
        check(vkd->vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "signal on actual GPU queue");
        auto wait = info<VkSemaphoreWaitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
        wait.semaphoreCount = 1;
        wait.pSemaphores = &semaphores[1];
        wait.pValues = &value;
        check(vkd->vkWaitSemaphores(device, &wait, 5'000'000'000ull), "wait through imported Linux FD");
        uint64_t observed = 0;
        check(vkd->vkGetSemaphoreCounterValue(device, semaphores[1], &observed), "read timeline value");
        if (observed != value) {
          throw std::runtime_error("Imported timeline value differs from GPU signal");
        }
        check(vkd->vkQueueWaitIdle(queue), "finish queue before semaphore teardown");
        for (const auto semaphore : semaphores) {
          vkd->vkDestroySemaphore(device, semaphore, nullptr);
        }
      }
      if (fdCount() != baseline) {
        throw std::runtime_error("Native semaphore round trips leaked file descriptors");
      }
      std::cout << "PASS original Remix Vulkan loader, 32 hardware FD round trips, no FD leak\n";
      return 0;
    }
    return 77;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
