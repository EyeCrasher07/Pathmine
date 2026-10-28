#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "spirv/spirv.hpp"
#include "dxvk/rtx_render/rtx_nrd_library.h"

namespace {
  void check(bool condition, const char* pMessage) {
    if (!condition) {
      throw std::runtime_error(pMessage);
    }
  }

  void check(nrd::Result result, const char* pMessage) {
    check(result == nrd::Result::SUCCESS, pMessage);
  }

  void checkStorageFormats(const nrd::ComputeShaderDesc& shader) {
    std::vector<uint32_t> words(shader.size / sizeof(uint32_t));
    std::memcpy(words.data(), shader.bytecode, shader.size);
    bool untypedWrite = false;
    bool storageImage = false;
    for (size_t offset = 5; offset < words.size();) {
      const auto count = words[offset] >> 16;
      const auto opcode = words[offset] & 0xffff;
      check(count && count <= words.size() - offset, "Malformed NRD SPIR-V instruction");
      if (opcode == spv::OpCapability && count == 2
          && words[offset + 1] == spv::CapabilityStorageImageWriteWithoutFormat) {
        untypedWrite = true;
      }
      if (opcode == spv::OpTypeImage && count >= 9 && words[offset + 7] == 2) {
        check(words[offset + 8] == spv::ImageFormatUnknown,
          "NRD storage images require the native Vulkan storage-format patch");
        storageImage = true;
      }
      offset += count;
    }
    check(untypedWrite && storageImage, "NRD pipeline cannot write its runtime-selected pool formats");
  }
}

int main() {
  nrd::Instance* pInstance = nullptr;
  std::unique_ptr<dxvk::util::SharedLibrary> library;
  try {
    std::string error;
    library = nrd::initialize(error);
    if (!library) {
      throw std::runtime_error(error);
    }
    const auto& version = nrd::dispatch.GetLibraryDesc();
    check(version.versionMajor == 4 && version.versionMinor == 13, "Wrong NRD library version");
    check(version.normalEncoding == nrd::NormalEncoding::R10_G10_B10_A2_UNORM, "Normal encoding differs from Remix");
    check(version.roughnessEncoding == nrd::RoughnessEncoding::LINEAR, "Roughness encoding differs from Remix");
    check(version.supportedDenoisers && version.supportedDenoisersNum, "NRD library exposes no denoisers");
    std::vector<nrd::DenoiserDesc> denoisers;
    std::vector<nrd::Identifier> identifiers;
    for (uint32_t index = 0; index < version.supportedDenoisersNum; index++) {
      identifiers.push_back(index + 1);
      denoisers.push_back({ identifiers.back(), version.supportedDenoisers[index] });
    }
    nrd::InstanceCreationDesc create{};
    create.denoisers = denoisers.data();
    create.denoisersNum = denoisers.size();
    check(nrd::dispatch.CreateInstance(create, pInstance), "Create original NRD instance failed");
    const auto& instance = nrd::dispatch.GetInstanceDesc(*pInstance);
    check(instance.pipelinesNum > 0, "NRD instance has no shader pipelines");
    uint64_t shaderBytes = 0;
    uint32_t genericClears = 0;
    for (uint32_t i = 0; i < instance.pipelinesNum; ++i) {
      const auto& shader = instance.pipelines[i].computeShaderSPIRV;
      check(shader.bytecode && shader.size >= 20 && shader.size % 4 == 0, "NRD SPIR-V shader is absent");
      uint32_t magic = 0;
      std::memcpy(&magic, shader.bytecode, sizeof(magic));
      check(magic == 0x07230203, "NRD shader is not SPIR-V");
      checkStorageFormats(shader);
      if (std::strstr(instance.pipelines[i].shaderFileName, "Clear_Float")
          || std::strstr(instance.pipelines[i].shaderFileName, "Clear_Uint")) {
        ++genericClears;
      }
      shaderBytes += shader.size;
    }
    check(genericClears == 2, "NRD float/uint clear pipelines are missing");
    nrd::CommonSettings common{};
    const float projection[16] = {1,0,0,0, 0,1,0,0, 0,0,1.001f,1, 0,0,-.1001f,0};
    const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    std::copy_n(projection,16,common.viewToClipMatrix);
    std::copy_n(projection,16,common.viewToClipMatrixPrev);
    std::copy_n(identity,16,common.worldToViewMatrix);
    std::copy_n(identity,16,common.worldToViewMatrixPrev);
    for (auto* size : {common.resourceSize, common.resourceSizePrev, common.rectSize, common.rectSizePrev}) {
      size[0] = 64;
      size[1] = 48;
    }
    uint32_t dispatchesTotal = 0;
    for (uint32_t frame = 0; frame < 4; ++frame) {
      common.frameIndex = frame;
      check(nrd::dispatch.SetCommonSettings(*pInstance, common), "Set NRD frame settings failed");
      const nrd::DispatchDesc* pDispatches = nullptr;
      uint32_t dispatchesNum = 0;
      check(nrd::dispatch.GetComputeDispatches(*pInstance, identifiers.data(), identifiers.size(), pDispatches, dispatchesNum), "Resolve NRD dispatches failed");
      check(pDispatches && dispatchesNum > 0, "NRD produced no dispatches");
      for (uint32_t i = 0; i < dispatchesNum; ++i) {
        check(pDispatches[i].pipelineIndex < instance.pipelinesNum, "NRD dispatch references an invalid pipeline");
      }
      dispatchesTotal += dispatchesNum;
    }
    const auto pipelinesNum = instance.pipelinesNum;
    nrd::dispatch.DestroyInstance(*pInstance);
    pInstance = nullptr;
    std::cout << "PASS original NRD " << unsigned(version.versionMajor) << '.' << unsigned(version.versionMinor)
      << ", " << denoisers.size() << " denoisers, " << pipelinesNum << " pipelines, " << shaderBytes
      << " SPIR-V bytes, " << dispatchesTotal << " frame dispatches, " << genericClears
      << " format-independent Vulkan clears\n";
    return 0;
  } catch (const std::exception& error) {
    if (pInstance) {
      nrd::dispatch.DestroyInstance(*pInstance);
    }
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
