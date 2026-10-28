/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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
#include <memory>
#include <string>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "rtx_semaphore.h"
#include "../../util/log/log.h"
#include "../../util/util_error.h"

namespace dxvk {
  namespace {
    void checkSemaphoreResult(VkResult result, const char* pOperation) {
      if (result != VK_SUCCESS) {
        throw DxvkError(std::string(pOperation) + " failed with: " + std::to_string(result));
      }
    }
  }

  RtxSemaphore* RtxSemaphore::createTimeline(const Rc<vk::DeviceFn>& vkd, const char* name, uint64_t initialValue, bool shared) {
    return create(vkd, name, initialValue, true, shared);
  }

  RtxSemaphore* RtxSemaphore::createBinary(const Rc<vk::DeviceFn>& vkd, const char* name, bool shared) {
    return create(vkd, name, 0, false, shared);
  }

  RtxSemaphore* RtxSemaphore::create(const Rc<vk::DeviceFn>& vkd, const char* name,
                                     uint64_t initialValue, bool timeline, bool shared) {
    if (vkd.ptr() == nullptr || !vkd->device()) {
      throw DxvkError("Semaphore creation requires a Vulkan device");
    }
    auto semaphore = std::unique_ptr<RtxSemaphore>(new RtxSemaphore());
    semaphore->m_vkd = vkd;
    semaphore->m_isTimeline = timeline;

    VkExportSemaphoreCreateInfo exportInfo = {};
    exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
#ifdef _WIN32
    exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
    exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif

    VkSemaphoreTypeCreateInfo typeInfo = {};
    typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    typeInfo.pNext = shared ? &exportInfo : nullptr;
    typeInfo.semaphoreType = timeline ? VK_SEMAPHORE_TYPE_TIMELINE : VK_SEMAPHORE_TYPE_BINARY;
    typeInfo.initialValue = initialValue;

    VkSemaphoreCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    createInfo.pNext = &typeInfo;
    checkSemaphoreResult(vkd->vkCreateSemaphore(vkd->device(), &createInfo, nullptr, &semaphore->m_sema),
                         "Semaphore creation");

    if (shared) {
#ifdef _WIN32
      if (!vkd->vkGetSemaphoreWin32HandleKHR) {
        throw DxvkError("Vulkan external Win32 semaphores are unavailable");
      }
      VkSemaphoreGetWin32HandleInfoKHR handleInfo = {};
      handleInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
      handleInfo.semaphore = semaphore->m_sema;
      handleInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
      checkSemaphoreResult(vkd->vkGetSemaphoreWin32HandleKHR(vkd->device(), &handleInfo, &semaphore->m_handle),
                           "Semaphore handle export");
#else
      if (!vkd->vkGetSemaphoreFdKHR) {
        throw DxvkError("Vulkan external FD semaphores are unavailable");
      }
      VkSemaphoreGetFdInfoKHR handleInfo = {};
      handleInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
      handleInfo.semaphore = semaphore->m_sema;
      handleInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
      checkSemaphoreResult(vkd->vkGetSemaphoreFdKHR(vkd->device(), &handleInfo, &semaphore->m_handle),
                           "Semaphore FD export");
#endif
    }

    semaphore->labelSemaphore(name);
    return semaphore.release();
  }

  void RtxSemaphore::labelSemaphore(const char* name) {
    if (name && m_vkd->vkSetDebugUtilsObjectNameEXT) {
      VkDebugUtilsObjectNameInfoEXT nameInfo = {};
      nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
      nameInfo.objectType = VK_OBJECT_TYPE_SEMAPHORE;
      nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_sema);
      nameInfo.pObjectName = name;
      m_vkd->vkSetDebugUtilsObjectNameEXT(m_vkd->device(), &nameInfo);
    }
  }

  RtxSemaphore::~RtxSemaphore() {
    if (m_sema) {
      m_vkd->vkDestroySemaphore(m_vkd->device(), m_sema, nullptr);
    }
#ifdef _WIN32
    if (m_handle != INVALID_HANDLE_VALUE) {
      ::CloseHandle(m_handle);
    }
#else
    if (m_handle >= 0) {
      ::close(m_handle);
    }
#endif
  }

  uint64_t RtxSemaphore::value() const {
    if (!m_isTimeline) {
      throw DxvkError("A binary semaphore has no timeline value");
    }
    uint64_t value = 0;
    checkSemaphoreResult(m_vkd->vkGetSemaphoreCounterValue(m_vkd->device(), m_sema, &value),
                         "Semaphore counter query");
    return value;
  }

  void RtxSemaphore::signal(uint64_t value) {
    if (!m_isTimeline) {
      throw DxvkError("A binary semaphore cannot be signaled from the CPU");
    }
    VkSemaphoreSignalInfo signalInfo = {};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
    signalInfo.semaphore = m_sema;
    signalInfo.value = value;
    checkSemaphoreResult(m_vkd->vkSignalSemaphore(m_vkd->device(), &signalInfo), "Semaphore signal");
  }

  void RtxSemaphore::wait(uint64_t value) {
    if (!m_isTimeline) {
      throw DxvkError("A binary semaphore cannot be waited on from the CPU");
    }
    VkSemaphoreWaitInfo waitInfo = {};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &m_sema;
    waitInfo.pValues = &value;

    for (;;) {
      const auto result = m_vkd->vkWaitSemaphores(m_vkd->device(), &waitInfo, 1'000'000'000ull);
      if (result == VK_SUCCESS) {
        return;
      }
      if (result == VK_TIMEOUT) {
        Logger::warn("Timeline semaphore wait timeout!");
      } else {
        checkSemaphoreResult(result, "Timeline semaphore wait");
      }
    }
  }
}
