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

#include "rtx_device_features.h"
#include "../dxvk_adapter.h"

namespace dxvk {

  DxvkDeviceFeatures getRtxDeviceFeatures(const Rc<DxvkAdapter>& adapter) {
    DxvkDeviceFeatures supported = adapter->features();
    DxvkDeviceFeatures enabled = {};

    // Geometry shaders are used for some meta ops
    enabled.core.features.geometryShader = VK_TRUE;
    enabled.core.features.robustBufferAccess = VK_TRUE;
    enabled.extRobustness2.robustBufferAccess2 = supported.extRobustness2.robustBufferAccess2;

    enabled.extMemoryPriority.memoryPriority = supported.extMemoryPriority.memoryPriority;

    enabled.extShaderDemoteToHelperInvocation.shaderDemoteToHelperInvocation = supported.extShaderDemoteToHelperInvocation.shaderDemoteToHelperInvocation;

    enabled.extVertexAttributeDivisor.vertexAttributeInstanceRateDivisor = supported.extVertexAttributeDivisor.vertexAttributeInstanceRateDivisor;
    enabled.extVertexAttributeDivisor.vertexAttributeInstanceRateZeroDivisor = supported.extVertexAttributeDivisor.vertexAttributeInstanceRateZeroDivisor;

    // Null Descriptors
    enabled.extRobustness2.nullDescriptor = supported.extRobustness2.nullDescriptor;

    // ProcessVertices
    enabled.core.features.vertexPipelineStoresAndAtomics = supported.core.features.vertexPipelineStoresAndAtomics;

    // DXVK Meta
    enabled.core.features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
    // NV-DXVK start
    enabled.core.features.shaderStorageImageReadWithoutFormat = VK_TRUE;
    // NV-DXVK end
    enabled.core.features.imageCubeArray = VK_TRUE;

    // SM1 level hardware
    enabled.core.features.depthClamp = VK_TRUE;
    enabled.core.features.depthBiasClamp = VK_TRUE;
    enabled.core.features.fillModeNonSolid = VK_TRUE;
    enabled.core.features.pipelineStatisticsQuery = supported.core.features.pipelineStatisticsQuery;
    enabled.core.features.sampleRateShading = VK_TRUE;
    enabled.core.features.samplerAnisotropy = supported.core.features.samplerAnisotropy;
    enabled.core.features.shaderClipDistance = VK_TRUE;
    enabled.core.features.shaderCullDistance = VK_TRUE;

    // Ensure we support real BC formats and unofficial vendor ones.
    enabled.core.features.textureCompressionBC = VK_TRUE;

    enabled.extDepthClipEnable.depthClipEnable = supported.extDepthClipEnable.depthClipEnable;
    enabled.vulkan12Features.hostQueryReset = supported.vulkan12Features.hostQueryReset;

    // SM2 level hardware
    enabled.core.features.occlusionQueryPrecise = VK_TRUE;

    // SM3 level hardware
    enabled.core.features.multiViewport = VK_TRUE;
    enabled.core.features.independentBlend = VK_TRUE;

    // D3D10 level hardware supports this in D3D9 native.
    enabled.core.features.fullDrawIndexUint32 = VK_TRUE;

    // Enable depth bounds test if we support it.
    enabled.core.features.depthBounds = supported.core.features.depthBounds;

    if (supported.extCustomBorderColor.customBorderColorWithoutFormat) {
      enabled.extCustomBorderColor.customBorderColors             = VK_TRUE;
      enabled.extCustomBorderColor.customBorderColorWithoutFormat = VK_TRUE;
    }

    // Transform feedback is required
    enabled.extTransformFeedback.transformFeedback = VK_TRUE;

    // VK_EXT_shader_viewport_index_layer
    enabled.vulkan12Features.shaderOutputViewportIndex = VK_TRUE;
    enabled.vulkan12Features.shaderOutputLayer = VK_TRUE;

    // VK_KHR_draw_indirect_count
    enabled.vulkan12Features.drawIndirectCount = VK_TRUE;

    // VK_KHR_sampler_mirror_clamp_to_edge
    enabled.vulkan12Features.samplerMirrorClampToEdge = VK_TRUE;

    return enabled;
  }

}
