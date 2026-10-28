// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include "api016_textures.h"

#include <unordered_map>
#include <unordered_set>

#include "api016_error.h"
#include "runtime_host.h"
#include "dxvk/rtx_render/rtx_asset_data_manager.h"
#include "dxvk/rtx_render/rtx_context.h"
#include "dxvk/rtx_render/rtx_mipmap.h"
#include "dxvk/rtx_render/rtx_shader_manager.h"
#include "dxvk/rtx_render/rtx_texture_manager.h"
#include "rtx/pass/image_utils/api_texture_normal.h"
#include <rtx_shaders/api_texture_normal.h>

namespace dxvk::native {
  namespace {
    struct Format {
      VkFormat vk;
      uint32_t bytes;
      uint32_t channels;
      bool srgb;
    };

    [[noreturn]] void invalid(const char* pMessage) {
      throw ApiError(api016::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, pMessage);
    }

    Format describe(api016::remixapi_TextureFormat format) {
      switch (format) {
      case api016::REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM: return { VK_FORMAT_R8G8B8A8_UNORM, 4, 4, false };
      case api016::REMIXAPI_TEXTURE_FORMAT_RGBA8_SRGB: return { VK_FORMAT_R8G8B8A8_SRGB, 4, 4, true };
      case api016::REMIXAPI_TEXTURE_FORMAT_BGRA8_UNORM: return { VK_FORMAT_B8G8R8A8_UNORM, 4, 4, false };
      case api016::REMIXAPI_TEXTURE_FORMAT_BGRA8_SRGB: return { VK_FORMAT_B8G8R8A8_SRGB, 4, 4, true };
      case api016::REMIXAPI_TEXTURE_FORMAT_R8_UNORM: return { VK_FORMAT_R8_UNORM, 1, 1, false };
      case api016::REMIXAPI_TEXTURE_FORMAT_RG8_UNORM: return { VK_FORMAT_R8G8_UNORM, 2, 2, false };
      case api016::REMIXAPI_TEXTURE_FORMAT_RGBA16_SFLOAT: return { VK_FORMAT_R16G16B16A16_SFLOAT, 8, 4, false };
      case api016::REMIXAPI_TEXTURE_FORMAT_RGBA32_SFLOAT: return { VK_FORMAT_R32G32B32A32_SFLOAT, 16, 4, false };
      case api016::REMIXAPI_TEXTURE_FORMAT_R32_SFLOAT: return { VK_FORMAT_R32_SFLOAT, 4, 1, false };
      default: invalid("Unknown API texture format");
      }
    }

    bool needsNormalConversion(api016::remixapi_TexturePurpose purpose) {
      return purpose == api016::REMIXAPI_TEXTURE_PURPOSE_NORMAL_DX
        || purpose == api016::REMIXAPI_TEXTURE_PURPOSE_NORMAL_OGL;
    }

    uint32_t mipLevels(uint32_t width, uint32_t height) {
      uint32_t result = 1;
      for (uint32_t size = std::max(width, height); size > 1; size >>= 1) {
        ++result;
      }
      return result;
    }

    class ApiNormalShader : public ManagedShader {
      SHADER_SOURCE(ApiNormalShader, VK_SHADER_STAGE_COMPUTE_BIT, api_texture_normal)
      PUSH_CONSTANTS(ApiTextureNormalArgs)
      BEGIN_PARAMETER()
        TEXTURE2D(API_TEXTURE_NORMAL_INPUT)
        RW_TEXTURE2D(API_TEXTURE_NORMAL_OUTPUT)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(ApiNormalShader);
  }

  struct ApiTextures::Impl {
    struct Texture {
      api016::remixapi_TextureFormat format = api016::REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM;
      api016::remixapi_TexturePurpose purpose = api016::REMIXAPI_TEXTURE_PURPOSE_COLOR;
      VkExtent3D extent { 0, 0, 1 };
      Rc<DxvkImage> source;
      Rc<DxvkImageView> sourceView;
      RtxMipmap::Resource output;
      TextureRef reference;
    };

    RuntimeHost& host;
    std::unordered_map<uint64_t, Texture> textures;

    explicit Impl(RuntimeHost& value) : host(value) { }

    Texture& find(api016::remixapi_TextureHandle handle) {
      const auto found = textures.find(reinterpret_cast<uintptr_t>(handle));
      if (found == textures.end()) {
        invalid("Unknown or destroyed API texture handle");
      }
      return found->second;
    }

    void makeOutput(Texture& texture, VkFormat format, uint32_t levels, bool storage, uint64_t hash) {
      DxvkImageCreateInfo info { };
      info.type = VK_IMAGE_TYPE_2D;
      info.format = format;
      info.extent = texture.extent;
      info.numLayers = 1;
      info.mipLevels = levels;
      info.sampleCount = VK_SAMPLE_COUNT_1_BIT;
      info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
        | VK_IMAGE_USAGE_SAMPLED_BIT | (storage ? VK_IMAGE_USAGE_STORAGE_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
      info.stages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      info.access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
      info.tiling = VK_IMAGE_TILING_OPTIMAL;
      info.layout = VK_IMAGE_LAYOUT_GENERAL;
      const auto image = host.device()->createImage(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        DxvkMemoryStats::Category::RTXMaterialTexture, "API memory texture");
      image->setHash(hash);
      host.context()->initImage(image, { VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1 }, VK_IMAGE_LAYOUT_UNDEFINED);
      DxvkImageViewCreateInfo view { };
      view.type = VK_IMAGE_VIEW_TYPE_2D;
      view.format = format;
      view.usage = VK_IMAGE_USAGE_SAMPLED_BIT | (storage ? VK_IMAGE_USAGE_STORAGE_BIT : 0);
      view.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
      view.numLevels = levels;
      view.numLayers = 1;
      texture.output.image = image;
      texture.output.view = host.device()->createImageView(image, view);
      view.numLevels = 1;
      for (uint32_t level = 0; level < levels; level++) {
        view.minLevel = level;
        texture.output.views.push_back(host.device()->createImageView(image, view));
      }
      texture.reference = TextureRef(texture.output.view);
    }

    void makeSource(Texture& texture, Format format) {
      if (!needsNormalConversion(texture.purpose)) {
        texture.source = texture.output.image;
        texture.sourceView = texture.output.views[0];
        return;
      }
      DxvkImageCreateInfo info { };
      info.type = VK_IMAGE_TYPE_2D;
      info.format = format.vk;
      info.extent = texture.extent;
      info.numLayers = 1;
      info.mipLevels = 1;
      info.sampleCount = VK_SAMPLE_COUNT_1_BIT;
      info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
      info.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
      info.tiling = VK_IMAGE_TILING_OPTIMAL;
      info.layout = VK_IMAGE_LAYOUT_GENERAL;
      texture.source = host.device()->createImage(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        DxvkMemoryStats::Category::RTXMaterialTexture, "API normal source");
      host.context()->initImage(texture.source, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }, VK_IMAGE_LAYOUT_UNDEFINED);
      DxvkImageViewCreateInfo view { };
      view.type = VK_IMAGE_VIEW_TYPE_2D;
      view.format = format.vk;
      view.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
      view.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
      view.numLevels = 1;
      view.numLayers = 1;
      texture.sourceView = host.device()->createImageView(texture.source, view);
    }

    void convertNormal(Texture& texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
      if (!needsNormalConversion(texture.purpose)) {
        return;
      }
      ApiTextureNormalArgs args { };
      args.offset = { x, y };
      args.extent = { width, height };
      args.flipY = texture.purpose == api016::REMIXAPI_TEXTURE_PURPOSE_NORMAL_DX;
      args.reconstructZ = describe(texture.format).channels == 2;
      const auto& ctx = host.context();
      ctx->setPushConstantBank(DxvkPushConstantBank::RTX);
      ctx->pushConstants(0, sizeof(args), &args);
      ctx->bindResourceView(API_TEXTURE_NORMAL_INPUT, texture.sourceView, nullptr);
      ctx->bindResourceView(API_TEXTURE_NORMAL_OUTPUT, texture.output.views[0], nullptr);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, ApiNormalShader::getShader());
      ctx->dispatch((width + 15) / 16, (height + 15) / 16, 1);
    }

    void generateMips(Texture& texture) {
      if (texture.output.image->info().mipLevels == 1) {
        return;
      }
      if (texture.purpose == api016::REMIXAPI_TEXTURE_PURPOSE_HEIGHTMAP || needsNormalConversion(texture.purpose)) {
        RtxMipmap::updateMipmap(host.context(), texture.output,
          texture.purpose == api016::REMIXAPI_TEXTURE_PURPOSE_HEIGHTMAP ? MipmapMethod::Maximum : MipmapMethod::Simple);
      } else {
        host.context()->generateMipmaps(texture.output.view, VK_FILTER_LINEAR);
      }
    }
  };

  ApiTextures::ApiTextures(RuntimeHost& host) : m_pImpl(std::make_unique<Impl>(host)) { }
  ApiTextures::~ApiTextures() = default;

  api016::remixapi_TextureHandle ApiTextures::create(const api016::remixapi_TextureFromMemoryInfo& info) {
    ScopedCpuProfileZone();
    auto& state = *m_pImpl;
    if (info.sType != api016::REMIXAPI_STRUCT_TYPE_TEXTURE_FROM_MEMORY_INFO
        || !info.hash || !info.pixelData || !info.width || !info.height) {
      invalid("Invalid API memory texture description");
    }
    const auto format = describe(info.format);
    const auto limit = state.host.device()->properties().core.properties.limits.maxImageDimension2D;
    if (info.width > limit || info.height > limit || uint64_t(info.width) * info.height > SIZE_MAX / format.bytes
        || info.purpose > api016::REMIXAPI_TEXTURE_PURPOSE_HEIGHTMAP
        || info.purpose < api016::REMIXAPI_TEXTURE_PURPOSE_COLOR
        || ((needsNormalConversion(info.purpose) || info.purpose == api016::REMIXAPI_TEXTURE_PURPOSE_NORMAL_OTH)
          && format.channels < 2)
        || (info.purpose == api016::REMIXAPI_TEXTURE_PURPOSE_HEIGHTMAP && format.srgb)) {
      invalid("Unsupported API texture extent, format or purpose");
    }
    const auto handle = reinterpret_cast<api016::remixapi_TextureHandle>(info.hash);
    if (state.textures.count(info.hash)) {
      return handle;
    }
    Impl::Texture texture;
    texture.format = info.format;
    texture.purpose = info.purpose;
    texture.extent = { info.width, info.height, 1 };
    const bool normal = needsNormalConversion(info.purpose);
    const bool storage = normal || info.purpose == api016::REMIXAPI_TEXTURE_PURPOSE_HEIGHTMAP;
    state.makeOutput(texture, normal ? VK_FORMAT_R8G8_UNORM : format.vk,
      mipLevels(info.width, info.height), storage, info.hash);
    state.makeSource(texture, format);
    state.host.context()->updateImage(texture.source, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
      { 0, 0, 0 }, texture.extent, info.pixelData,
      VkDeviceSize(info.width) * format.bytes, VkDeviceSize(info.width) * info.height * format.bytes);
    state.convertNormal(texture, 0, 0, info.width, info.height);
    state.generateMips(texture);
    state.textures.emplace(info.hash, std::move(texture));
    return handle;
  }

  api016::remixapi_TextureHandle ApiTextures::createFile(const std::filesystem::path& path) {
    ScopedCpuProfileZone();
    if (path.empty()) {
      invalid("Empty API texture path");
    }
    const auto asset = AssetDataManager::get().findAsset(path.u8string());
    if (asset == nullptr || asset->info().type != AssetType::Image2D || asset->info().numLayers != 1) {
      throw ApiError(api016::REMIXAPI_ERROR_CODE_GENERAL_FAILURE, "API texture file is not a supported 2D image");
    }
    auto& state = *m_pImpl;
    const auto handle = reinterpret_cast<api016::remixapi_TextureHandle>(asset->hash());
    if (!handle) {
      throw ApiError(api016::REMIXAPI_ERROR_CODE_GENERAL_FAILURE, "API texture asset has no valid hash");
    }
    if (!state.textures.count(asset->hash())) {
      Impl::Texture texture;
      texture.extent = asset->info().extent;
      texture.reference = TextureRef(state.host.context()->getCommonObjects()->getTextureManager()
        .preloadTextureAsset(asset, ColorSpace::AUTO, false));
      state.textures.emplace(asset->hash(), std::move(texture));
    }
    return handle;
  }

  void ApiTextures::update(const api016::remixapi_TexturePatchInfo& info) {
    ScopedCpuProfileZone();
    if (info.sType != api016::REMIXAPI_STRUCT_TYPE_TEXTURE_PATCH_INFO || !info.entryCount || !info.entries) {
      invalid("Invalid API texture patch batch");
    }
    auto& state = *m_pImpl;
    // Validate the entire batch before issuing commands that modify texture contents.
    for (uint32_t index = 0; index < info.entryCount; index++) {
      const auto& entry = info.entries[index];
      const auto& texture = state.find(entry.handle);
      if (texture.source == nullptr || !entry.patchCount || !entry.patches) {
        invalid("API texture patches require an updatable memory texture");
      }
      const auto format = describe(texture.format);
      for (uint32_t patchIndex = 0; patchIndex < entry.patchCount; patchIndex++) {
        const auto& patch = entry.patches[patchIndex];
        const uint64_t rowBytes = uint64_t(patch.width) * format.bytes;
        if (!patch.width || !patch.height || !patch.pixelData || patch.format != texture.format
            || patch.dstX >= texture.extent.width || patch.width > texture.extent.width - patch.dstX
            || patch.dstY >= texture.extent.height || patch.height > texture.extent.height - patch.dstY
            || patch.rowPitch < rowBytes
            || uint64_t(patch.rowPitch) * (patch.height - 1) > SIZE_MAX - rowBytes) {
          invalid("Invalid API texture patch bounds, format or row pitch");
        }
      }
    }
    std::unordered_set<Impl::Texture*> changed;
    for (uint32_t index = 0; index < info.entryCount; index++) {
      const auto& entry = info.entries[index];
      auto& texture = state.find(entry.handle);
      for (uint32_t patchIndex = 0; patchIndex < entry.patchCount; patchIndex++) {
        const auto& patch = entry.patches[patchIndex];
        state.host.context()->updateImage(texture.source, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
          { int32_t(patch.dstX), int32_t(patch.dstY), 0 }, { patch.width, patch.height, 1 },
          patch.pixelData, patch.rowPitch, VkDeviceSize(patch.rowPitch) * patch.height);
        state.convertNormal(texture, patch.dstX, patch.dstY, patch.width, patch.height);
      }
      changed.insert(&texture);
    }
    for (auto* pTexture : changed) {
      state.generateMips(*pTexture);
    }
  }

  void ApiTextures::destroy(api016::remixapi_TextureHandle handle) {
    if (!handle || !m_pImpl->textures.erase(reinterpret_cast<uintptr_t>(handle))) {
      invalid("Unknown or destroyed API texture handle");
    }
  }

  TextureRef ApiTextures::resolve(api016::remixapi_TextureHandle handle) const {
    return handle ? m_pImpl->find(handle).reference : TextureRef();
  }
}
