// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "api016_cameras.h"
#include "api016_materials.h"
#include "api016_textures.h"
#include "dxvk/pch/dxvk.pch.h"
#include "runtime_host.h"

namespace {
  namespace api = dxvk::native::api016;

  void require(bool condition, const char* pMessage) {
    if (!condition) {
      throw std::runtime_error(pMessage);
    }
  }

  void verifyFirstFrame(bool requestDlss) {
    dxvk::native::RuntimeHost host(std::filesystem::current_path().u8string());
    auto& ngx = host.device()->getCommon()->metaNGXContext();
    const auto requestedUpscaler = requestDlss ? dxvk::UpscalerType::DLSS : dxvk::UpscalerType::None;
    require(dxvk::RtxOptions::upscalerType() == requestedUpscaler,
      "device creation replaced the requested upscaler before its first frame");
    const VkExtent3D extent { 320, 240, 1 };
    require(host.beginFrame(extent), "first native frame was skipped");
    const auto renderExtent = host.context()->getResourceManager().getDownscaleDimensions();
    if (requestDlss) {
      require(ngx.isInitializationComplete(), "shared depth was allocated before NGX capabilities completed");
    }
    if (requestDlss && ngx.supportsDLSS()) {
      uint32_t inputWidth = 0;
      uint32_t inputHeight = 0;
      host.device()->getCommon()->metaDLSS().getInputSize(inputWidth, inputHeight);
      require(renderExtent.width == inputWidth && renderExtent.height == inputHeight,
        "first shared depth does not use the selected DLSS render size");
      require(renderExtent.width < extent.width && renderExtent.height < extent.height,
        "supported DLSS silently used the full-resolution fallback");
      require(dxvk::RtxOptions::upscalerType() == requestedUpscaler,
        "supported DLSS was disabled during initialization");
    } else {
      require(renderExtent == extent, "non-DLSS first frame has an unexpected render size");
    }

    dxvk::native::ApiTextures textures(host);
    dxvk::native::ApiMaterials materials(host, textures);
    dxvk::native::ApiCameras cameras(host, materials);
    api::remixapi_CameraInfoParameterizedEXT params { };
    params.sType = api::REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT;
    params.forward = { 0, 0, 1 };
    params.up = { 0, 1, 0 };
    params.right = { 1, 0, 0 };
    params.fovYInDegrees = 60;
    params.aspect = float(extent.width) / extent.height;
    params.nearPlane = .05f;
    params.farPlane = 1000;
    api::remixapi_CameraInfo info { };
    info.sType = api::REMIXAPI_STRUCT_TYPE_CAMERA_INFO;
    info.type = api::REMIXAPI_CAMERA_TYPE_WORLD;
    info.pNext = &params;
    cameras.setup(info);
    const auto camera = host.context()->getSceneManager().getCamera().getShaderConstants();
    require(camera.resolution.x == renderExtent.width && camera.resolution.y == renderExtent.height,
      "first camera and shared depth use different render resolutions");
    host.submit();
    const auto output = host.interopInfo();
    require(output.extent == extent && output.depthExtent == renderExtent && output.depthFd >= 0,
      "published first-frame color or depth extent differs from camera setup");
    std::cout << "First frame: " << extent.width << 'x' << extent.height
      << ", depth/camera: " << renderExtent.width << 'x' << renderExtent.height
      << ", NGX complete: " << ngx.isInitializationComplete()
      << ", DLSS supported: " << ngx.supportsDLSS() << '\n';
  }
}

int main(int argc, char** argv) {
  try {
    require(argc == 2, "expected --shutdown, --none or --dlss");
    const std::string mode(argv[1]);
    if (mode == "--shutdown") {
      for (uint32_t cycle = 0; cycle < 2; ++cycle) {
        {
          dxvk::native::RuntimeHost host(std::filesystem::current_path().u8string());
          std::cout << "Immediate shutdown " << cycle << ", NGX complete: "
            << host.device()->getCommon()->metaNGXContext().isInitializationComplete() << '\n';
        }
        require(dxvk::g_dxvkDeviceNative == nullptr, "device registration survived startup shutdown");
      }
    } else {
      require(mode == "--none" || mode == "--dlss", "unknown startup test mode");
      verifyFirstFrame(mode == "--dlss");
      require(dxvk::g_dxvkDeviceNative == nullptr, "device registration survived first-frame shutdown");
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "NGX startup verification failed: " << error.what() << '\n';
    return 1;
  }
}
