/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <nvapi.h>
#else
#include <nvml.h>
#endif

#include "rtx_gpu_profile.h"
#include "rtx_dlss_types.h"

#include "../dxvk_adapter.h"
#include "../../util/log/log.h"
#include "../../util/util_string.h"
#include "../../util/util_library.h"

namespace dxvk {

  namespace {

    constexpr uint32_t kNvidiaVendorId = 0x10DE;
    constexpr DLSSProfile kRtxSparkDlssMode = DLSSProfile::MaxPerf;

    GpuHardwareInfo s_hardwareInfo;
    std::optional<uint32_t> s_nativeNvidiaArch;

    // Written once during device initialization and read every frame by DLSS.
    std::atomic<bool> s_isRtxSpark { false };

    uint64_t toMiB(uint64_t bytes) {
      return bytes / (1024 * 1024);
    }

    std::string toHex(uint32_t value) {
      return str::format("0x", std::hex, value);
    }

    std::string describeCoreCount(const GpuHardwareInfo& info) {
      if (info.nvapiCoreCount) {
        return std::to_string(*info.nvapiCoreCount);
      }
      return "unknown";
    }

    std::string describeArchitecture(const GpuHardwareInfo& info) {
      if (info.nvapiArchitecture) {
        return toHex(*info.nvapiArchitecture);
      }
      return "unknown";
    }

#ifdef _WIN32
    static_assert(kNvidiaArchTuring == NV_GPU_ARCHITECTURE_TU100);
    static_assert(kNvidiaArchAmpere == NV_GPU_ARCHITECTURE_GA100);
    static_assert(kNvidiaArchAda == NV_GPU_ARCHITECTURE_AD100);
    static_assert(kNvidiaArchBlackwell == NV_GPU_ARCHITECTURE_GB200);

    // Fills the NVAPI facts of the GPU matching the Vulkan adapter LUID.
    // Logs every step that fails.
    void queryNvapiGpuInfo(const VkPhysicalDeviceIDProperties& deviceId, GpuHardwareInfo& info) {
      if (!deviceId.deviceLUIDValid) {
        Logger::info("[GPU Profile] Vulkan adapter LUID unavailable.");
        return;
      }

      NvAPI_Status status = NvAPI_Initialize();
      if (status != NVAPI_OK) {
        Logger::info(str::format("[GPU Profile] NVAPI initialization failed (", status, ")."));
        return;
      }

      NvPhysicalGpuHandle handles[NVAPI_MAX_PHYSICAL_GPUS] = { };
      NvU32 handleCount = 0;
      status = NvAPI_EnumPhysicalGPUs(handles, &handleCount);
      if (status != NVAPI_OK) {
        Logger::info(str::format("[GPU Profile] NVAPI GPU enumeration failed (", status, ")."));
        return;
      }

      for (NvU32 i = 0; i < handleCount; ++i) {
        NvLogicalGpuHandle logicalGpu = nullptr;
        status = NvAPI_GetLogicalGPUFromPhysicalGPU(handles[i], &logicalGpu);
        if (status != NVAPI_OK) {
          Logger::info(str::format("[GPU Profile] NVAPI logical GPU query failed for GPU ", i, " (", status, ")."));
          continue;
        }
        LUID adapterLuid = { };
        NV_LOGICAL_GPU_DATA logicalGpuData = { };
        logicalGpuData.version = NV_LOGICAL_GPU_DATA_VER;
        logicalGpuData.pOSAdapterId = &adapterLuid;
        status = NvAPI_GPU_GetLogicalGpuInfo(logicalGpu, &logicalGpuData);
        if (status != NVAPI_OK) {
          Logger::info(str::format("[GPU Profile] NVAPI adapter LUID query failed for GPU ", i, " (", status, ")."));
          continue;
        }
        if (std::memcmp(&adapterLuid, deviceId.deviceLUID, sizeof(adapterLuid)) != 0) {
          continue;
        }

        NV_GPU_TYPE gpuType = NV_SYSTEM_TYPE_GPU_UNKNOWN;
        status = NvAPI_GPU_GetGPUType(handles[i], &gpuType);
        if (status != NVAPI_OK) {
          Logger::info(str::format("[GPU Profile] NVAPI GPU type query failed (", status, ")."));
          return;
        }
        info.nvapiIntegrated = gpuType == NV_SYSTEM_TYPE_IGPU;

        NvU32 coreCount = 0;
        status = NvAPI_GPU_GetGpuCoreCount(handles[i], &coreCount);
        if (status == NVAPI_OK) {
          info.nvapiCoreCount = coreCount;
        } else {
          Logger::info(str::format("[GPU Profile] NVAPI core count query failed (", status, ")."));
        }

        NV_GPU_ARCH_INFO archInfo = { };
        archInfo.version = NV_GPU_ARCH_INFO_VER;
        status = NvAPI_GPU_GetArchInfo(handles[i], &archInfo);
        if (status == NVAPI_OK) {
          info.nvapiArchitecture = static_cast<uint32_t>(archInfo.architecture_id);
        } else {
          Logger::info(str::format("[GPU Profile] NVAPI architecture query failed (", status, ")."));
        }

        Logger::info(str::format("[GPU Profile] NVAPI GPU type ", gpuType, ", ", describeCoreCount(info), " cores, architecture ",
                                 describeArchitecture(info), "."));
        return;
      }

      Logger::info("[GPU Profile] No NVAPI GPU matches the Vulkan adapter LUID.");
    }
#else
    std::optional<uint32_t> queryNvmlArch(const VkPhysicalDeviceIDProperties& deviceId) {
      util::SharedLibrary library("libnvidia-ml.so.1");
      if (!library) {
        Logger::info(str::format("[GPU Profile] NVML unavailable: ", library.error()));
        return std::nullopt;
      }
      const auto pInit = reinterpret_cast<decltype(&nvmlInit_v2)>(library.symbol("nvmlInit_v2"));
      const auto pShutdown = reinterpret_cast<decltype(&nvmlShutdown)>(library.symbol("nvmlShutdown"));
      const auto pGetDevice = reinterpret_cast<decltype(&nvmlDeviceGetHandleByUUID)>(library.symbol("nvmlDeviceGetHandleByUUID"));
      const auto pGetArch = reinterpret_cast<decltype(&nvmlDeviceGetArchitecture)>(library.symbol("nvmlDeviceGetArchitecture"));
      if (!pInit || !pShutdown || !pGetDevice || !pGetArch || pInit() != NVML_SUCCESS) {
        Logger::info("[GPU Profile] NVML architecture query unavailable.");
        return std::nullopt;
      }
      struct NvmlSession {
        decltype(pShutdown) pShutdown;
        ~NvmlSession() { pShutdown(); }
      } session { pShutdown };

      std::ostringstream uuid;
      uuid << "GPU-" << std::hex << std::setfill('0');
      for (size_t i = 0; i < VK_UUID_SIZE; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
          uuid << '-';
        }
        uuid << std::setw(2) << static_cast<unsigned int>(deviceId.deviceUUID[i]);
      }
      nvmlDevice_t device { };
      nvmlDeviceArchitecture_t arch;
      if (pGetDevice(uuid.str().c_str(), &device) != NVML_SUCCESS || pGetArch(device, &arch) != NVML_SUCCESS) {
        Logger::info("[GPU Profile] NVML could not identify the Vulkan adapter by UUID.");
        return std::nullopt;
      }
      Logger::info(str::format("[GPU Profile] Native NVML architecture: ", arch, "."));
      switch (arch) {
      case NVML_DEVICE_ARCH_KEPLER: return 0xE0;
      case NVML_DEVICE_ARCH_MAXWELL: return 0x110;
      case NVML_DEVICE_ARCH_PASCAL: return 0x130;
      case NVML_DEVICE_ARCH_VOLTA: return 0x140;
      case NVML_DEVICE_ARCH_TURING: return kNvidiaArchTuring;
      case NVML_DEVICE_ARCH_AMPERE: return kNvidiaArchAmpere;
      case NVML_DEVICE_ARCH_ADA: return kNvidiaArchAda;
      case NVML_DEVICE_ARCH_HOPPER: return 0x180;
      case NVML_DEVICE_ARCH_BLACKWELL: return kNvidiaArchBlackwell;
      default: return std::nullopt;
      }
    }
#endif

  }

  bool isRtxSpark(const GpuHardwareInfo& info) {
    return info.vendorId == kNvidiaVendorId && info.isIntegrated && describeRtxSparkMismatch(info) == nullptr;
  }

  const char* describeRtxSparkMismatch(const GpuHardwareInfo& info) {
    if (!info.nvapiIntegrated) {
      return "NVAPI did not confirm an integrated GPU";
    }
    if (!info.nvapiArchitecture) {
      return "NVAPI architecture unknown";
    }
    // NVAPI architecture IDs are not ordered by generation, so the match is exact.
    if (*info.nvapiArchitecture != kNvidiaArchBlackwell) {
      return "not a Blackwell GPU";
    }
    return nullptr;
  }

  AutoPresetDecision selectAutoPreset(const GpuHardwareInfo& info, std::optional<GraphicsPreset> gpuOverride, uint32_t nvidiaArch) {
    AutoPresetDecision decision;
    const bool hasOverride = gpuOverride.has_value();
    if (hasOverride) {
      decision.preset = *gpuOverride;
    }

    if (isRtxSpark(info)) {
      if (!hasOverride) {
        decision.preset = kRtxSparkGraphicsPreset;
        decision.log.push_back("RTX Spark detected, setting default graphics settings to Medium");
      }
      decision.lowMemoryGpu = info.deviceLocalHeapTotal <= kLowMemoryGpuThreshold;
      if (decision.lowMemoryGpu) {
        decision.log.push_back(str::format("RTX Spark with ", toMiB(info.deviceLocalHeapTotal),
                                           " MiB of device-local memory detected, enabling low memory mode."));
      }
      return decision;
    }

    if (!hasOverride && info.vendorId == kNvidiaVendorId) {
      if (nvidiaArch < kNvidiaArchTuring) {
        decision.log.push_back("NVIDIA architecture without HW RTX support detected, setting default graphics settings to Low, but your experience may not be optimal");
        decision.preset = GraphicsPreset::Low;
      } else if (nvidiaArch < kNvidiaArchAmpere) {
        decision.log.push_back("NVIDIA Turing architecture detected, setting default graphics settings to Low");
        decision.preset = GraphicsPreset::Low;
      } else if (nvidiaArch < kNvidiaArchAda) {
        decision.log.push_back("NVIDIA Ampere architecture detected, setting default graphics settings to Medium");
        decision.preset = GraphicsPreset::Medium;
      } else if (nvidiaArch < kNvidiaArchBlackwell) {
        decision.log.push_back("NVIDIA Ada architecture detected, setting default graphics settings to High");
        decision.preset = GraphicsPreset::High;
      } else {
        decision.log.push_back("NVIDIA Blackwell architecture detected, setting default graphics settings to Ultra");
        decision.preset = GraphicsPreset::Ultra;
      }
    } else if (info.vendorId != kNvidiaVendorId) {
      // Unknown hardware defaults to Low.
      if (!hasOverride) {
        decision.log.push_back("Non-NVIDIA architecture detected, setting default graphics settings to Low");
        decision.preset = GraphicsPreset::Low;
      }
      decision.applyNonNvidiaDefaults = true;
    }

    // GPUs with 8 GB or less lower the quality even further.
    if (info.exactDeviceLocalTypeHeapSize <= kLowMemoryGpuThreshold) {
      if (hasOverride) {
        decision.log.push_back("8GB GPU detected; keeping the matched graphics preset override.");
      } else {
        decision.log.push_back("8GB GPU detected, lowering quality setting.");
        decision.preset = (GraphicsPreset) std::clamp((int) decision.preset + 1, (int) GraphicsPreset::Medium, (int) GraphicsPreset::Low);
      }
      decision.lowMemoryGpu = true;
    }
    return decision;
  }

  std::optional<DLSSProfile> selectAutoDlssMode(std::optional<DLSSProfile> gpuOverride, bool isRtxSpark) {
    if (gpuOverride) {
      return gpuOverride;
    }
    if (isRtxSpark) {
      return kRtxSparkDlssMode;
    }
    return std::nullopt;
  }

  void GpuProfile::detect(const DxvkDeviceInfo& deviceInfo,
                          const VkPhysicalDeviceMemoryProperties& memoryProperties,
                          const DxvkAdapterMemoryInfo& memoryInfo) {
    GpuHardwareInfo info;
    info.vendorId = deviceInfo.core.properties.vendorID;
    info.isIntegrated = deviceInfo.core.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
      if (memoryProperties.memoryTypes[i].propertyFlags == VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
        info.exactDeviceLocalTypeHeapSize = memoryProperties.memoryHeaps[memoryProperties.memoryTypes[i].heapIndex].size;
        break;
      }
    }

    uint64_t deviceLocalBudget = 0;
    for (uint32_t i = 0; i < memoryProperties.memoryHeapCount; i++) {
      if (memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
        info.deviceLocalHeapTotal += memoryProperties.memoryHeaps[i].size;
        deviceLocalBudget += memoryInfo.heaps[i].memoryBudget;
      }
    }

    if (info.vendorId == kNvidiaVendorId && info.isIntegrated) {
      Logger::info(str::format("[GPU Profile] NVIDIA integrated GPU with ", memoryProperties.memoryHeapCount, " memory heap(s), ",
                               toMiB(info.deviceLocalHeapTotal), " MiB device-local, current budget ",
                               toMiB(deviceLocalBudget), " MiB."));
#ifdef _WIN32
      queryNvapiGpuInfo(deviceInfo.coreDeviceId, info);
#endif
    }
#ifndef _WIN32
    s_nativeNvidiaArch = info.vendorId == kNvidiaVendorId
      ? queryNvmlArch(deviceInfo.coreDeviceId) : std::nullopt;
#endif

    s_hardwareInfo = info;
    s_isRtxSpark.store(dxvk::isRtxSpark(info), std::memory_order_relaxed);
    if (isRtxSpark()) {
      Logger::info(str::format("[GPU Profile] RTX Spark detected (", deviceInfo.core.properties.deviceName, ", ",
                               describeCoreCount(info), " cores, ", toMiB(info.deviceLocalHeapTotal), " MiB device-local memory)."));
    } else if (info.vendorId == kNvidiaVendorId && info.isIntegrated) {
      Logger::info(str::format("[GPU Profile] NVIDIA integrated GPU is not treated as RTX Spark (", describeRtxSparkMismatch(info),
                               "); using the standard automatic settings."));
    }
  }

  const GpuHardwareInfo& GpuProfile::getHardwareInfo() {
    return s_hardwareInfo;
  }

  std::optional<uint32_t> GpuProfile::getNativeNvidiaArch() {
    return s_nativeNvidiaArch;
  }

  bool GpuProfile::isRtxSpark() {
    return s_isRtxSpark.load(std::memory_order_relaxed);
  }

}
