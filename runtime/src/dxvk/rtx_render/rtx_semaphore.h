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
#pragma once
#include "../../util/sync/sync_signal.h"
#include "../../vulkan/vulkan_loader.h"

namespace dxvk {
  class DxvkDevice;

  class RtxSemaphore : public sync::Signal {
    Rc<vk::DeviceFn> m_vkd;
    VkSemaphore m_sema = VK_NULL_HANDLE;
    bool m_isTimeline = false;
#ifdef _WIN32
    HANDLE m_handle = INVALID_HANDLE_VALUE;
#else
    int m_handle = -1;
#endif
    
    RtxSemaphore() = default;

    void labelSemaphore(const char* name);
    static RtxSemaphore* create(const Rc<vk::DeviceFn>& vkd, const char* name,
                               uint64_t initialValue, bool timeline, bool shared);

  public:
    static RtxSemaphore* createTimeline(DxvkDevice* device, const char* name, uint64_t initialValue = 0, bool shared = false);
    static RtxSemaphore* createBinary(DxvkDevice* device, const char* name, const bool shared = false);
    static RtxSemaphore* createTimeline(const Rc<vk::DeviceFn>& vkd, const char* name, uint64_t initialValue = 0, bool shared = false);
    static RtxSemaphore* createBinary(const Rc<vk::DeviceFn>& vkd, const char* name, bool shared = false);
    
    ~RtxSemaphore() override;

    uint64_t value() const override;
    void signal(uint64_t value) override;
    void wait(uint64_t value) override;

#ifdef _WIN32
    HANDLE
#else
    int
#endif
    sharedHandle() const {
      return m_handle;
    }

    inline VkSemaphore handle() const {
      return m_sema;
    }
  };

  class RtxFence : public RcObject {
    DxvkDevice* m_device;
    VkFence m_fence = VK_NULL_HANDLE;

  public:
    RtxFence(DxvkDevice* device);
    ~RtxFence();

    inline VkFence handle() const {
      return m_fence;
    }
  };
}
