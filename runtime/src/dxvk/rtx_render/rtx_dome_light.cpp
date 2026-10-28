// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <algorithm>

#include "rtx_context.h"
#include "rtx_dome_light.h"
#include "rtx_shader_manager.h"
#include "rtx/pass/image_utils/dome_light_blend.h"

#include <rtx_shaders/dome_light_blend.h>

namespace dxvk {
  static_assert(sizeof(DomeLightBlendArgs) == 96);
  namespace {
    class DomeLightBlendShader : public ManagedShader {
      SHADER_SOURCE(DomeLightBlendShader, VK_SHADER_STAGE_COMPUTE_BIT, dome_light_blend)
      PUSH_CONSTANTS(DomeLightBlendArgs)
      BEGIN_PARAMETER()
        SAMPLER2D(DOME_LIGHT_BLEND_SOURCE)
        TEXTURE2D(DOME_LIGHT_BLEND_DESTINATION)
        RW_TEXTURE2D(DOME_LIGHT_BLEND_OUTPUT)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(DomeLightBlendShader);
  }

  void DomeLightCompositor::resize(const Rc<DxvkContext>& ctx, VkExtent3D extent) {
    if (extent == m_extent) {
      return;
    }
    DxvkImageCreateInfo info { };
    info.type = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    info.extent = extent;
    info.numLayers = info.mipLevels = 1;
    info.sampleCount = VK_SAMPLE_COUNT_1_BIT;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT
      | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.stages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    info.access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.layout = VK_IMAGE_LAYOUT_GENERAL;
    DxvkImageViewCreateInfo view { };
    view.type = VK_IMAGE_VIEW_TYPE_2D;
    view.format = info.format;
    view.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    view.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    view.numLevels = view.numLayers = 1;
    std::array<Rc<DxvkImageView>, 2> views;
    for (auto& target : views) {
      const auto image = ctx->getDevice()->createImage(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        DxvkMemoryStats::Category::RTXMaterialTexture, "Dome light composition");
      ctx->initImage(image, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }, VK_IMAGE_LAYOUT_UNDEFINED);
      target = ctx->getDevice()->createImageView(image, view);
    }
    m_views = std::move(views);
    m_extent = extent;
  }

  DomeLight DomeLightCompositor::compose(const Rc<DxvkContext>& ctx, const std::vector<const DomeLight*>& layers) {
    ScopedCpuProfileZone();
    ScopedGpuProfileZone(ctx, "Dome light composition");
    VkExtent3D extent { 1, 1, 1 };
    for (const auto* pLayer : layers) {
      if (const auto* pView = pLayer->texture.getImageView()) {
        const auto sourceExtent = pView->mipLevelExtent(0);
        extent.width = std::max(extent.width, sourceExtent.width);
        extent.height = std::max(extent.height, sourceExtent.height);
      }
    }
    resize(ctx, extent);
    const auto sampler = ctx->getDevice()->getCommon()->getResources().getSampler(
      VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST,
      VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    uint32_t output = 0;
    for (size_t index = 0; index < layers.size(); ++index) {
      const auto& layer = *layers[index];
      DomeLightBlendArgs args { };
      args.worldToLight = layer.worldToLight;
      args.radianceAndOpacity = vec4(layer.radiance.x, layer.radiance.y, layer.radiance.z, layer.opacity);
      args.extentBlendAndFlags = { extent.width, extent.height, uint32_t(layer.blendType),
        (index ? 1u : 0u) | (layer.texture.getImageView() ? 2u : 0u) | (layer.enableBlending ? 4u : 0u) };
      output = uint32_t(index % 2);
      ctx->setPushConstantBank(DxvkPushConstantBank::RTX);
      ctx->pushConstants(0, sizeof(args), &args);
      ctx->bindResourceView(DOME_LIGHT_BLEND_SOURCE, layer.texture.getImageView(), nullptr);
      ctx->bindResourceSampler(DOME_LIGHT_BLEND_SOURCE, sampler);
      ctx->bindResourceView(DOME_LIGHT_BLEND_DESTINATION, index ? m_views[1 - output] : nullptr, nullptr);
      ctx->bindResourceView(DOME_LIGHT_BLEND_OUTPUT, m_views[output], nullptr);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, DomeLightBlendShader::getShader());
      ctx->dispatch((extent.width + 15) / 16, (extent.height + 15) / 16, 1);
    }
    DomeLight result;
    result.radiance = Vector3(1.f);
    result.worldToLight = Matrix4();
    result.texture = TextureRef(m_views[output]);
    return result;
  }
}
