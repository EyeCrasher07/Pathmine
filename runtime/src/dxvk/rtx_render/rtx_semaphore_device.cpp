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
#include "rtx_semaphore.h"
#include "../dxvk_device.h"

namespace dxvk {
  RtxSemaphore* RtxSemaphore::createTimeline(DxvkDevice* device, const char* name, uint64_t initialValue, bool shared) {
    return createTimeline(device->vkd(), name, initialValue, shared);
  }

  RtxSemaphore* RtxSemaphore::createBinary(DxvkDevice* device, const char* name, const bool shared) {
    return createBinary(device->vkd(), name, shared);
  }

  RtxFence::RtxFence(DxvkDevice* device)
    : m_device(device) {
    VkFenceCreateInfo info;
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (device->vkd()->vkCreateFence(device->handle(), &info, nullptr, &m_fence) != VK_SUCCESS) {
      throw DxvkError("RtxFence: vkCreateFence failed");
    }
  }

  RtxFence::~RtxFence() {
    if (m_fence) {
      m_device->vkd()->vkDestroyFence(m_device->handle(), m_fence, nullptr);
      m_fence = nullptr;
    }
  }
}
