// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "dxvk/pch/dxvk.pch.h"
#include "api016_error.h"
#include "api016_textures.h"
#include "gl_output_reader.h"
#include "original_scene_fixture.h"
#include "runtime_host.h"

namespace {
  namespace api = dxvk::native::api016;
  using dxvk::native::ApiTextures;
  using dxvk::native::RuntimeHost;
  using dxvk::native::test::require;

  template<typename F>
  void invalid(F&& work) {
    try {
      work();
    } catch (const dxvk::native::ApiError& error) {
      require(error.code() == api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, "wrong texture validation error");
      return;
    }
    throw std::runtime_error("invalid API texture input was accepted");
  }

  std::vector<uint8_t> readTexture(RuntimeHost& host, const dxvk::TextureRef& texture,
      uint32_t level, uint32_t bytesPerPixel, uint32_t layer = 0) {
    const auto* pView = texture.getImageView();
    require(pView != nullptr, "an API memory texture has no image view");
    const auto image = pView->image();
    const auto extent = image->mipLevelExtent(level);
    dxvk::DxvkBufferCreateInfo info { };
    info.size = uint64_t(extent.width) * extent.height * bytesPerPixel;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.stages = VK_PIPELINE_STAGE_HOST_BIT;
    info.access = VK_ACCESS_HOST_READ_BIT;
    const auto buffer = host.device()->createBuffer(info,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      dxvk::DxvkMemoryStats::Category::AppBuffer, "API texture readback");
    host.context()->copyImageToBuffer(buffer, 0, 1, 1, image,
      { VK_IMAGE_ASPECT_COLOR_BIT, level, layer, 1 }, { 0, 0, 0 }, extent);
    host.context()->flushCommandList();
    host.device()->waitForIdle();
    std::vector<uint8_t> result(info.size);
    std::memcpy(result.data(), buffer->mapPtr(0), result.size());
    return result;
  }

  api::remixapi_TextureHandle makeTexture(ApiTextures& textures, uint64_t hash,
      api::remixapi_TextureFormat format, api::remixapi_TexturePurpose purpose,
      uint32_t width, uint32_t height, const void* pPixels) {
    api::remixapi_TextureFromMemoryInfo info { };
    info.sType = api::REMIXAPI_STRUCT_TYPE_TEXTURE_FROM_MEMORY_INFO;
    info.hash = hash;
    info.pixelData = static_cast<const uint8_t*>(pPixels);
    info.width = width;
    info.height = height;
    info.format = format;
    info.purpose = purpose;
    return textures.create(info);
  }

  void update(ApiTextures& textures, api::remixapi_TextureHandle handle,
      const api::remixapi_TexturePatch* pPatches, uint32_t count) {
    const api::remixapi_TextureBatchEntry entry { handle, count, pPatches };
    const api::remixapi_TexturePatchInfo info {
      api::REMIXAPI_STRUCT_TYPE_TEXTURE_PATCH_INFO, nullptr, 1, &entry };
    textures.update(info);
  }

  void testFormats(RuntimeHost& host, ApiTextures& textures) {
    for (uint32_t value = 0; value <= 8; value++) {
      const auto format = static_cast<api::remixapi_TextureFormat>(value);
      std::vector<uint8_t> texel;
      if (value <= 3) {
        texel = { 37, 93, 171, 255 };
      } else if (value == 4) {
        texel = { 93 };
      } else if (value == 5) {
        texel = { 93, 171 };
      } else if (value == 6) {
        const std::array<uint16_t, 4> halves { 0x3400, 0x3800, 0x3a00, 0x3c00 };
        texel.resize(sizeof(halves));
        std::memcpy(texel.data(), halves.data(), texel.size());
      } else {
        const std::array<float, 4> floats { 0.25f, 0.5f, 0.75f, 1.0f };
        texel.resize(value == 7 ? sizeof(floats) : sizeof(float));
        std::memcpy(texel.data(), floats.data(), texel.size());
      }
      std::vector<uint8_t> pixels;
      for (uint32_t pixel = 0; pixel < 16; pixel++) {
        pixels.insert(pixels.end(), texel.begin(), texel.end());
      }
      const auto handle = makeTexture(textures, 100 + value, format, api::REMIXAPI_TEXTURE_PURPOSE_COLOR,
        4, 4, pixels.data());
      const auto reference = textures.resolve(handle);
      require(reinterpret_cast<uintptr_t>(handle) == 100 + value && reference.getImageHash() == 100 + value,
        "API texture handle or image asset hash differs from its supplied hash");
      std::fill(pixels.begin(), pixels.end(), 0);
      const auto base = readTexture(host, reference, 0, texel.size());
      for (size_t index = 0; index < base.size(); index++) {
        require(base[index] == texel[index % texel.size()], "API upload changed its source bytes or borrowed caller memory");
      }
      const auto mip = readTexture(host, reference, 2, texel.size());
      require(mip == texel, "original mip generation changed a constant texture");
      textures.destroy(handle);
      invalid([&] { textures.resolve(handle); });
      require(readTexture(host, reference, 0, texel.size()) == base,
        "destroying a handle invalidated an existing material texture reference");
    }
    const std::array<uint8_t, 16> checker {
      0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255 };
    const auto srgb = makeTexture(textures, 200, api::REMIXAPI_TEXTURE_FORMAT_RGBA8_SRGB,
      api::REMIXAPI_TEXTURE_PURPOSE_COLOR, 2, 2, checker.data());
    const auto linear = makeTexture(textures, 201, api::REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM,
      api::REMIXAPI_TEXTURE_PURPOSE_COLOR, 2, 2, checker.data());
    const auto srgbMip = readTexture(host, textures.resolve(srgb), 1, 4);
    const auto linearMip = readTexture(host, textures.resolve(linear), 1, 4);
    require(srgbMip[0] >= 187 && srgbMip[0] <= 189 && linearMip[0] >= 127 && linearMip[0] <= 128,
      "API sRGB mips were averaged in gamma space");
    textures.destroy(srgb);
    textures.destroy(linear);
  }

  void testPatches(RuntimeHost& host, ApiTextures& textures) {
    std::array<uint8_t, 64> initial { };
    const auto handle = makeTexture(textures, 300, api::REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM,
      api::REMIXAPI_TEXTURE_PURPOSE_DATA, 4, 4, initial.data());
    std::array<uint8_t, 24> data;
    data.fill(0xff);
    std::fill_n(data.data(), 8, 64);
    std::fill_n(data.data() + 12, 8, 128);
    const api::remixapi_TexturePatch patch {
      1, 1, 2, 2, data.data(), 12, api::REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM };
    update(textures, handle, &patch, 1);
    data.fill(0);
    const auto reference = textures.resolve(handle);
    const auto bytes = readTexture(host, reference, 0, 4);
    for (uint32_t y = 0; y < 4; y++) {
      for (uint32_t x = 0; x < 4; x++) {
        const uint8_t expected = x >= 1 && x < 3 && y >= 1 && y < 3 ? (y == 1 ? 64 : 128) : 0;
        for (uint32_t channel = 0; channel < 4; channel++) {
          require(bytes[(y * 4 + x) * 4 + channel] == expected, "API patch offset or padded row pitch is wrong");
        }
      }
    }
    const auto mip = readTexture(host, reference, 2, 4);
    require(mip[0] >= 23 && mip[0] <= 25, "API patch did not refresh lower mip levels");
    std::array<api::remixapi_TexturePatch, 2> invalidBatch { patch, patch };
    invalidBatch[1].dstX = 4;
    invalid([&] { update(textures, handle, invalidBatch.data(), invalidBatch.size()); });
    require(readTexture(host, reference, 0, 4) == bytes, "invalid API batch partially modified a texture");
    auto wrongPitch = patch;
    wrongPitch.rowPitch = 1;
    invalid([&] { update(textures, handle, &wrongPitch, 1); });
    textures.destroy(handle);
    invalid([&] { update(textures, handle, &patch, 1); });
    invalid([&] { textures.destroy(handle); });
  }

  void testLayeredMips(RuntimeHost& host) {
    dxvk::DxvkImageCreateInfo info { };
    info.type = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.extent = { 4, 4, 1 };
    info.numLayers = 2;
    info.mipLevels = 3;
    info.sampleCount = VK_SAMPLE_COUNT_1_BIT;
    info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
      | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.stages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    info.access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.layout = VK_IMAGE_LAYOUT_GENERAL;
    const auto image = host.device()->createImage(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      dxvk::DxvkMemoryStats::Category::RTXMaterialTexture, "layered mip regression fixture");
    host.context()->initImage(image, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 3, 0, 2 }, VK_IMAGE_LAYOUT_UNDEFINED);
    dxvk::DxvkImageViewCreateInfo viewInfo { };
    viewInfo.type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = info.format;
    viewInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.numLevels = 3;
    viewInfo.numLayers = 2;
    const auto view = host.device()->createImageView(image, viewInfo);
    for (uint32_t layer = 0; layer < 2; layer++) {
      std::array<uint8_t, 64> pixels { };
      for (uint32_t pixel = 0; pixel < 16; pixel++) {
        pixels[pixel * 4 + layer] = 255;
        pixels[pixel * 4 + 3] = 255;
      }
      host.context()->updateImage(image, { VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1 },
        { 0, 0, 0 }, info.extent, pixels.data(), 16, 64);
    }
    host.context()->generateMipmaps(view, VK_FILTER_LINEAR);
    for (uint32_t layer = 0; layer < 2; layer++) {
      const auto mip = readTexture(host, dxvk::TextureRef(view), 2, 4, layer);
      require(mip[layer] == 255 && mip[1 - layer] == 0 && mip[2] == 0 && mip[3] == 255,
        "single-layer mip support changed the original layered blit path");
    }
  }

  void testNormalsAndHeight(RuntimeHost& host, ApiTextures& textures) {
    const std::array<uint8_t, 4> flatNormal { 128, 128, 255, 255 };
    for (const auto format : { api::REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM, api::REMIXAPI_TEXTURE_FORMAT_RG8_UNORM }) {
      const auto handle = makeTexture(textures, 430 + format, format,
        api::REMIXAPI_TEXTURE_PURPOSE_NORMAL_DX, 1, 1, flatNormal.data());
      const auto bytes = readTexture(host, textures.resolve(handle), 0, 2);
      require(bytes[0] == 128 && bytes[1] == 127,
        "API byte/RG normal input did not reconstruct the original near-flat material normal");
      textures.destroy(handle);
    }
    const std::array<float, 16> normals {
      .5f, .5f, 1, 1, 1, .5f, .5f, 1, .5f, 1, .5f, 1, .5f, .5f, 1, 1 };
    for (const auto purpose : { api::REMIXAPI_TEXTURE_PURPOSE_NORMAL_DX, api::REMIXAPI_TEXTURE_PURPOSE_NORMAL_OGL }) {
      const auto handle = makeTexture(textures, 400 + purpose, api::REMIXAPI_TEXTURE_FORMAT_RGBA32_SFLOAT,
        purpose, 2, 2, normals.data());
      const auto bytes = readTexture(host, textures.resolve(handle), 0, 2);
      require(bytes[0] == 128 && bytes[1] == 128,
        "neutral API normal is not neutral in original material encoding");
      require(bytes[2] == 255 && bytes[3] == 128, "API normal does not preserve the original sphere encoding");
      const bool directX = purpose == api::REMIXAPI_TEXTURE_PURPOSE_NORMAL_DX;
      require(bytes[4] == 128 && bytes[5] == (directX ? 0 : 255),
        "API DirectX/OpenGL normal handedness is wrong");
      const std::array<float, 4> changed { .5f, .5f, 1, 1 };
      const api::remixapi_TexturePatch patch { 0, 1, 1, 1, reinterpret_cast<const uint8_t*>(changed.data()),
        sizeof(changed), api::REMIXAPI_TEXTURE_FORMAT_RGBA32_SFLOAT };
      update(textures, handle, &patch, 1);
      const auto after = readTexture(host, textures.resolve(handle), 0, 2);
      require(after[4] == 128 && after[5] == 128,
        "API normal patch did not refresh converted material data");
      textures.destroy(handle);
    }
    const std::array<uint8_t, 8> oct { 128, 128, 255, 255, 0, 255, 128, 128 };
    const auto existing = makeTexture(textures, 410, api::REMIXAPI_TEXTURE_FORMAT_RG8_UNORM,
      api::REMIXAPI_TEXTURE_PURPOSE_NORMAL_OTH, 2, 2, oct.data());
    const auto octBytes = readTexture(host, textures.resolve(existing), 0, 2);
    require(std::equal(oct.begin(), oct.end(), octBytes.begin()), "pre-encoded API normals were converted again");
    textures.destroy(existing);
    std::array<uint8_t, 4> heights { 0, 64, 128, 255 };
    const auto height = makeTexture(textures, 420, api::REMIXAPI_TEXTURE_FORMAT_R8_UNORM,
      api::REMIXAPI_TEXTURE_PURPOSE_HEIGHTMAP, 2, 2, heights.data());
    require(readTexture(host, textures.resolve(height), 1, 1)[0] == 255, "API height mip averaged away the maximum");
    heights[3] = 32;
    const api::remixapi_TexturePatch patch { 1, 1, 1, 1, &heights[3], 1, api::REMIXAPI_TEXTURE_FORMAT_R8_UNORM };
    update(textures, height, &patch, 1);
    require(readTexture(host, textures.resolve(height), 1, 1)[0] == 128, "API height patch left stale maximum mips");
    textures.destroy(height);
  }

  void testNormalEncoding(RuntimeHost& host, ApiTextures& textures) {
    const std::array<uint8_t, 24> pixels {
      128, 128, 255, 255, 76, 128, 235, 255, 180, 128, 235, 255,
      255, 128, 128, 255, 128, 255, 128, 255, 128, 128, 0, 255 };
    const std::array<uint8_t, 12> expected { 128, 127, 86, 127, 169, 127, 254, 127, 128, 1, 255, 0 };
    const auto handle = makeTexture(textures, 450, api::REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM,
      api::REMIXAPI_TEXTURE_PURPOSE_NORMAL_DX, 6, 1, pixels.data());
    const auto reference = textures.resolve(handle);
    require(reference.getImageView()->info().format == VK_FORMAT_R8G8_UNORM,
      "API converted normal precision differs from the original RG8 texture contract");
    const auto result = readTexture(host, reference, 0, 2);
    require(std::equal(expected.begin(), expected.end(), result.begin()),
      "API converted normal bytes differ from the original sphere-octahedral encoding");
    textures.destroy(handle);
  }

  void testFile(RuntimeHost& host, ApiTextures& textures, dxvk::native::test::Reader& reader) {
    const auto path = std::filesystem::current_path() / std::filesystem::u8path("native-текстура.dds");
    std::array<uint32_t, 32> header { };
    header[0] = 0x20534444;
    header[1] = 124;
    header[2] = 0x100f;
    header[3] = header[4] = 2;
    header[5] = 8;
    header[7] = 1;
    header[19] = header[22] = 32;
    header[20] = 0x41;
    header[23] = 0xff;
    header[24] = 0xff00;
    header[25] = 0xff0000;
    header[26] = 0xff000000;
    header[27] = 0x1000;
    const std::array<uint32_t, 4> data { 0xff0000ff, 0xff0000ff, 0xff0000ff, 0xff0000ff };
    {
      std::ofstream file(path, std::ios::binary);
      file.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
      file.write(reinterpret_cast<const char*>(data.data()), sizeof(data));
      require(bool(file), "cannot create native DDS texture fixture");
    }
    const auto handle = textures.createFile(path);
    require(textures.createFile(path) == handle, "repeated API file load changed asset identity");
    const auto reference = textures.resolve(handle);
    require(reference.getManagedTexture() != nullptr, "API file texture bypassed the original asset/streaming manager");
    const auto mesh = reinterpret_cast<remixapi_MeshHandle>(uintptr_t(501));
    const auto material = reinterpret_cast<remixapi_MaterialHandle>(uintptr_t(502));
    dxvk::OpaqueMaterialData opaque;
    opaque.setAlbedoOpacityTexture(reference);
    opaque.setEmissiveColorTexture(reference);
    opaque.setAlbedoConstant(dxvk::Vector3(1));
    opaque.setEmissiveColorConstant(dxvk::Vector3(1));
    opaque.setEmissiveIntensity(4);
    opaque.setEnableEmission(true);
    const auto& assets = host.context()->getSceneManager().getAssetReplacer();
    assets->makeMaterialWithTexturePreload(*host.context(), material, dxvk::MaterialData(opaque));
    std::vector<dxvk::RasterGeometry> geometry;
    geometry.push_back(dxvk::native::test::createTriangle(host.device(), material));
    assets->registerExternalMesh(mesh, std::move(geometry));
    std::vector<uint8_t> pixels;
    for (uint32_t frame = 0; frame < 4; frame++) {
      require(host.beginFrame({ 96, 64, 1 }), "API file texture frame was skipped");
      const dxvk::Matrix4 projection(dxvk::Vector4(1.1547005f, 0, 0, 0), dxvk::Vector4(0, 1.7320508f, 0, 0),
        dxvk::Vector4(0, 0, 1.001001f, 1), dxvk::Vector4(0, 0, -.1001001f, 0));
      host.context()->getSceneManager().getCameraManager().processExternalCamera(
        dxvk::CameraType::Main, dxvk::Matrix4(), projection);
      auto state = std::make_unique<dxvk::ExternalDrawState>();
      state->mesh = mesh;
      state->cameraType = dxvk::CameraType::Main;
      state->drawCall.cameraType = dxvk::CameraType::Main;
      state->doubleSided = true;
      host.context()->commitExternalGeometryToRT(std::move(state));
      host.submit();
      pixels = reader.readAndPresent(host);
    }
    size_t redPixels = 0;
    for (size_t offset = 0; offset < pixels.size(); offset += 4) {
      if (pixels[offset] > 180 && pixels[offset] > pixels[offset + 1] + 20 && pixels[offset] > pixels[offset + 2] + 20) {
        ++redPixels;
      }
    }
    require(redPixels > 100, "native API DDS texture was not streamed into the original path-traced material");
    host.context()->getSceneManager().destroyExternalMesh(host.context(), mesh);
    assets->destroyExternalMaterial(material);
    textures.destroy(handle);
    std::filesystem::remove(path);
  }
}

int main() {
  if (!glfwInit()) {
    return 77;
  }
  dxvk::native::test::Window window;
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  window.pHandle = glfwCreateWindow(128, 96, "Native Minecraft texture API verification", nullptr, nullptr);
  if (!window.pHandle) {
    return 77;
  }
  glfwMakeContextCurrent(window.pHandle);
  try {
    RuntimeHost host(std::filesystem::current_path().u8string());
    ApiTextures textures(host);
    dxvk::native::test::Reader reader(host.interopInfo());
    require(!textures.resolve(nullptr).isValid(), "a null texture handle should be an empty material slot");
    testNormalEncoding(host, textures);
    testFormats(host, textures);
    testPatches(host, textures);
    testLayeredMips(host);
    testNormalsAndHeight(host, textures);
    testFile(host, textures, reader);
    std::cout << "PASS native API 0.16 textures: nine formats, sRGB mips, pitched patches, normal conversion, height maxima, DDS streaming and retained references\n";
    return 0;
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "FAIL " << error.message() << '\n';
    return 1;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
