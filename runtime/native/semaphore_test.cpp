#include <array>
#include <cerrno>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "dxvk/rtx_render/rtx_semaphore.h"
#include "util/log/log.h"
#include "util/util_error.h"

namespace dxvk {
  Logger Logger::s_instance("native-semaphore-test.log", LogLevel::None);
}

namespace {
  void check(VkResult result, const char* pOperation) {
    if (result != VK_SUCCESS) {
      throw std::runtime_error(std::string(pOperation) + ": " + std::to_string(result));
    }
  }

  void require(bool condition, const char* pMessage) {
    if (!condition) {
      throw std::runtime_error(pMessage);
    }
  }

  size_t fdCount() {
    return std::distance(std::filesystem::directory_iterator("/proc/self/fd"),
      std::filesystem::directory_iterator());
  }

  void exercise(const dxvk::Rc<dxvk::vk::DeviceFn>& vkd, VkQueue queue, bool timeline) {
    const auto baseline = fdCount();
    for (uint32_t frame = 0; frame < 32; ++frame) {
      int borrowedFd = -1;
      {
        dxvk::Rc<dxvk::RtxSemaphore> exported = timeline
          ? dxvk::RtxSemaphore::createTimeline(vkd, "Native Remix exported", 2, true)
          : dxvk::RtxSemaphore::createBinary(vkd, "Native Remix exported", true);
        dxvk::Rc<dxvk::RtxSemaphore> imported = timeline
          ? dxvk::RtxSemaphore::createTimeline(vkd, "Native Remix imported")
          : dxvk::RtxSemaphore::createBinary(vkd, "Native Remix imported");
        borrowedFd = exported->sharedHandle();
        require(borrowedFd >= 0, "original semaphore did not export a Linux FD");
        require((::fcntl(borrowedFd, F_GETFD) & FD_CLOEXEC) != 0, "exported FD is not close-on-exec");
        VkImportSemaphoreFdInfoKHR importInfo = {};
        importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR;
        importInfo.semaphore = imported->handle();
        importInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
        importInfo.fd = ::fcntl(borrowedFd, F_DUPFD_CLOEXEC, 0);
        require(importInfo.fd >= 0, "owned FD duplication failed");
        const auto result = vkd->vkImportSemaphoreFdKHR(vkd->device(), &importInfo);
        if (result != VK_SUCCESS) {
          ::close(importInfo.fd);
          check(result, "import original semaphore");
        }

        const uint64_t signalValue = 3 + frame;
        VkTimelineSemaphoreSubmitInfo values = {};
        values.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        values.signalSemaphoreValueCount = 1;
        values.pSignalSemaphoreValues = &signalValue;
        const auto exportedHandle = exported->handle();
        VkSubmitInfo submit = {};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.pNext = timeline ? &values : nullptr;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &exportedHandle;
        check(vkd->vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "signal original semaphore on GPU");
        if (timeline) {
          imported->wait(signalValue);
          require(imported->value() == signalValue, "GPU timeline signal was not shared");
          imported->signal(signalValue + 1);
          exported->wait(signalValue + 1);
          require(exported->value() == signalValue + 1, "CPU timeline signal was not shared");
        } else {
          const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
          const auto importedHandle = imported->handle();
          VkSubmitInfo wait = {};
          wait.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
          wait.waitSemaphoreCount = 1;
          wait.pWaitSemaphores = &importedHandle;
          wait.pWaitDstStageMask = &stage;
          check(vkd->vkQueueSubmit(queue, 1, &wait, VK_NULL_HANDLE), "wait through original binary semaphore FD");
        }
        check(vkd->vkQueueWaitIdle(queue), "finish semaphore GPU work");
        require(::fcntl(borrowedFd, F_GETFD) >= 0, "Vulkan import consumed the engine's borrowed FD");
      }
      require(::fcntl(borrowedFd, F_GETFD) == -1 && errno == EBADF, "semaphore destruction did not close its owned FD");
    }
    require(fdCount() == baseline, "original semaphore lifetime leaked Linux FDs");
  }
}

int main() {
  try {
    dxvk::Rc<dxvk::vk::LibraryFn> library = new dxvk::vk::LibraryFn();
    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo instanceInfo = {};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    check(library->vkCreateInstance(&instanceInfo, nullptr, &instance), "create native instance");
    dxvk::Rc<dxvk::vk::InstanceFn> vki = new dxvk::vk::InstanceFn(true, instance);
    uint32_t count = 0;
    check(vki->vkEnumeratePhysicalDevices(instance, &count, nullptr), "count GPUs");
    std::vector<VkPhysicalDevice> devices(count);
    check(vki->vkEnumeratePhysicalDevices(instance, &count, devices.data()), "enumerate GPUs");
    for (const auto physical : devices) {
      VkPhysicalDeviceProperties properties = {};
      vki->vkGetPhysicalDeviceProperties(physical, &properties);
      if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
        continue;
      }
      uint32_t familyCount = 0;
      vki->vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
      std::vector<VkQueueFamilyProperties> families(familyCount);
      vki->vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data());
      uint32_t family = 0;
      while (family < familyCount && !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
        ++family;
      }
      if (family == familyCount) {
        continue;
      }
      const float priority = 1;
      VkDeviceQueueCreateInfo queueInfo = {};
      queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      queueInfo.queueFamilyIndex = family;
      queueInfo.queueCount = 1;
      queueInfo.pQueuePriorities = &priority;
      VkPhysicalDeviceTimelineSemaphoreFeatures timeline = {};
      timeline.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
      timeline.timelineSemaphore = true;
      const char* extensions[] = {VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME};
      VkDeviceCreateInfo deviceInfo = {};
      deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
      deviceInfo.pNext = &timeline;
      deviceInfo.queueCreateInfoCount = 1;
      deviceInfo.pQueueCreateInfos = &queueInfo;
      deviceInfo.enabledExtensionCount = 1;
      deviceInfo.ppEnabledExtensionNames = extensions;
      VkDevice device = VK_NULL_HANDLE;
      check(vki->vkCreateDevice(physical, &deviceInfo, nullptr, &device), "create native device");
      dxvk::Rc<dxvk::vk::DeviceFn> vkd = new dxvk::vk::DeviceFn(true, instance, device);
      VkQueue queue = VK_NULL_HANDLE;
      vkd->vkGetDeviceQueue(device, family, 0, &queue);
      exercise(vkd, queue, true);
      exercise(vkd, queue, false);
      const auto baseline = fdCount();
      const auto exportFunction = vkd->vkGetSemaphoreFdKHR;
      vkd->vkGetSemaphoreFdKHR = nullptr;
      bool rejected = false;
      try {
        dxvk::Rc<dxvk::RtxSemaphore> semaphore = dxvk::RtxSemaphore::createBinary(vkd, "Unavailable export", true);
      } catch (const dxvk::DxvkError&) {
        rejected = true;
      }
      vkd->vkGetSemaphoreFdKHR = exportFunction;
      require(rejected && fdCount() == baseline, "unavailable export was not rejected cleanly");
      std::cout << "PASS original RtxSemaphore on " << properties.deviceName
                << ": 32 timeline and 32 binary GPU FD round trips, no FD leak\n";
      return 0;
    }
    return 77;
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "FAIL " << error.message() << '\n';
    return 1;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
