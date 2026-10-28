// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <codecvt>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <locale>
#include <memory>
#include <mutex>
#include <thread>

#include "api016_cameras.h"
#include "api016_error.h"
#include "api016_instances.h"
#include "api016_lights.h"
#include "api016_materials.h"
#include "api016_meshes.h"
#include "api016_options.h"
#include "api016_textures.h"
#include "runtime_api.h"
#include "runtime_host.h"
#include "dxvk/rtx_render/rtx_context.h"
#include "dxvk/rtx_render/rtx_option_manager.h"
#include "util/log/log.h"

namespace dxvk::native {
  namespace {
    namespace api = api016;

    enum class Phase { Idle, Recording, Submitted, Skipped, SkippedSubmitted, Failed };

    struct Resources {
      RuntimeHost host;
      ApiTextures textures;
      ApiMaterials materials;
      ApiMeshes meshes;
      ApiInstances instances;
      ApiCameras cameras;
      ApiLights lights;

      explicit Resources(ApiOptions& options)
      : host(std::filesystem::current_path().u8string(), [&options] {
          options.applyStartupWrites();
          RtxOptionManager::applyPendingValues(nullptr, false);
        }), textures(host),
        materials(host, textures), meshes(host, materials), instances(host, meshes),
        cameras(host, materials), lights(host, textures) { }
    };

    struct State {
      ApiOptions options;
      std::unique_ptr<Resources> resources;
      std::thread::id contextThread;
      Phase phase = Phase::Idle;
      uint64_t clientValue = 0;
    };

    std::mutex s_mutex;
    std::unique_ptr<State> s_state;

    [[noreturn]] void invalid(const char* pMessage) {
      throw ApiError(api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, pMessage);
    }

    template<typename T>
    const T& input(const T* pValue) {
      if (!pValue) {
        invalid("Missing API input");
      }
      return *pValue;
    }

    template<typename T>
    T& output(T* pValue) {
      if (!pValue) {
        invalid("Missing API output");
      }
      return *pValue;
    }

    Resources& device(State& state, bool contextThread = true) {
      if (!state.resources) {
        throw ApiError(api::REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED, "Call Startup before using renderer resources");
      }
      if (contextThread && state.contextThread != std::this_thread::get_id()) {
        invalid("Renderer calls must use the Startup thread");
      }
      if (state.phase == Phase::Failed) {
        throw ApiError(api::REMIXAPI_ERROR_CODE_GENERAL_FAILURE, "The native rendering session failed");
      }
      return *state.resources;
    }

    void capture(State& state) {
      if (state.phase != Phase::Recording && state.phase != Phase::Skipped) {
        invalid("Call remixapi_BeginFrame before camera or transient scene capture");
      }
    }

    template<typename Action>
    api::remixapi_ErrorCode invoke(const char* pOperation, Action&& action) noexcept {
      std::lock_guard<std::mutex> lock(s_mutex);
      try {
        if (!s_state) {
          return api::REMIXAPI_ERROR_CODE_NOT_INITIALIZED;
        }
        action(*s_state);
        return api::REMIXAPI_ERROR_CODE_SUCCESS;
      } catch (const ApiError& error) {
        if (error.code() == api::REMIXAPI_ERROR_CODE_GENERAL_FAILURE || std::getenv("RTXREMIX_API_LOG_ERRORS")) {
          Logger::err(str::format("[native-remix] ", pOperation, ": ", error.what()));
        }
        return error.code();
      } catch (const DxvkError& error) {
        if (s_state) {
          s_state->phase = Phase::Failed;
        }
        Logger::err(str::format("[native-remix] ", pOperation, ": ", error.message()));
      } catch (const std::exception& error) {
        Logger::err(str::format("[native-remix] ", pOperation, ": ", error.what()));
      } catch (...) {
        Logger::err(str::format("[native-remix] ", pOperation, ": unknown exception"));
      }
      return api::REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
    }

    std::filesystem::path texturePath(api::remixapi_Path path) {
      if (!path || !*path) {
        invalid("Missing texture file path");
      }
#ifdef _WIN32
      return std::filesystem::path(path);
#else
      std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
      return std::filesystem::u8path(converter.to_bytes(path));
#endif
    }

    api::remixapi_ErrorCode REMIXAPI_CALL startup(const api::remixapi_StartupInfo* pInfo) {
      return invoke("Startup", [&](State& state) {
        const auto& info = input(pInfo);
        if (info.sType != api::REMIXAPI_STRUCT_TYPE_STARTUP_INFO) {
          invalid("Invalid Startup structure");
        }
        if (state.resources) {
          throw ApiError(api::REMIXAPI_ERROR_CODE_ALREADY_EXISTS, "A native renderer is already started");
        }
        if (!info.forceNoVkSwapchain || !info.interopImage || info.editorModeEnabled
            || info.disableSrgbConversionForOutput) {
          throw ApiError(api::REMIXAPI_ERROR_CODE_GENERAL_FAILURE, "The native runtime requires client OpenGL interop presentation");
        }
        auto resources = std::make_unique<Resources>(state.options);
        state.resources = std::move(resources);
        state.contextThread = std::this_thread::get_id();
      });
    }

    api::remixapi_ErrorCode REMIXAPI_CALL shutdown() {
      return invoke("Shutdown", [](State&) { s_state.reset(); });
    }

    api::remixapi_ErrorCode beginFrame(uint32_t width, uint32_t height) {
      return invoke("BeginFrame", [&](State& state) {
        auto& resources = device(state);
        if (state.phase != Phase::Idle || width >= (1u << 14) || height >= (1u << 14)) {
          invalid("Invalid frame extent or an earlier frame has not been presented");
        }
        state.phase = resources.host.beginFrame({ width, height, 1 }) ? Phase::Recording : Phase::Skipped;
      });
    }

    api::remixapi_ErrorCode REMIXAPI_CALL submit(const api::remixapi_SubmitInfo* pInfo) {
      return invoke("Submit", [&](State& state) {
        auto& resources = device(state);
        capture(state);
        const uint32_t flags = pInfo ? input(pInfo).interopFlags : 0;
        if ((pInfo && pInfo->sType != api::REMIXAPI_STRUCT_TYPE_SUBMIT_INFO) || (flags & ~7u)) {
          invalid("Invalid Submit structure or flags");
        }
        if (flags & api::REMIXAPI_SUBMIT_INTEROP_BIT_DISABLE_QUEUE_SUBMIT) {
          throw ApiError(api::REMIXAPI_ERROR_CODE_GENERAL_FAILURE, "External image publication requires a Vulkan queue submission");
        }
        if (state.phase == Phase::Skipped) {
          state.phase = Phase::SkippedSubmitted;
          return;
        }
        resources.host.submit();
        if (flags & api::REMIXAPI_SUBMIT_INTEROP_BIT_FORCE_WAIT_DEVICE_IDLE) {
          resources.host.device()->waitForIdle();
        }
        state.phase = Phase::Submitted;
      });
    }

    api::remixapi_ErrorCode REMIXAPI_CALL present(const api::remixapi_PresentInfo* pInfo) {
      return invoke("Present", [&](State& state) {
        auto& resources = device(state);
        const auto& info = input(pInfo);
        if (info.sType != api::REMIXAPI_STRUCT_TYPE_PRESENT_INFO || !info.skipVulkanSwapchain
            || !info.clientDoneSemaphore_ValueToWait || info.clientDoneSemaphore_ValueToWait <= state.clientValue
            || (state.phase != Phase::Submitted && state.phase != Phase::SkippedSubmitted)) {
          invalid("Present requires a submitted interop frame and a new client completion value");
        }
        if (state.phase == Phase::Submitted) {
          resources.host.present(info.clientDoneSemaphore_ValueToWait, !info.disableCpuWaitForExternalSemaphores);
        }
        state.clientValue = info.clientDoneSemaphore_ValueToWait;
        state.phase = Phase::Idle;
      });
    }

    api::remixapi_ErrorCode REMIXAPI_CALL interop(api::remixapi_dxvk_InteropInfo* pInfo) {
      return invoke("GetInteropInfo", [&](State& state) {
        auto& resources = device(state);
        auto& info = output(pInfo);
        if (state.phase == Phase::Recording || state.phase == Phase::Skipped) {
          invalid("Interop images are published by Submit");
        }
        auto native = resources.host.interopInfo();
        if (state.phase == Phase::Skipped || state.phase == Phase::SkippedSubmitted) {
          native.imageFd = native.depthFd = -1;
          native.extent = native.depthExtent = { 0, 0, 1 };
          native.allocationSize = native.depthAllocationSize = 0;
        }
        const auto handle = [](int fd) { return fd >= 0 ? uint64_t(fd) : uint64_t(0); };
        info = { };
        info.dxvkDoneSemaphore_Handle = handle(native.rendererDoneFd);
        info.clientDoneSemaphore_Handle = handle(native.clientDoneFd);
        info.dxvkDoneSemaphore_ValueToWait = native.rendererDoneValue;
        info.imageMemory_Handle = handle(native.imageFd);
        info.allocationSize = native.allocationSize;
        info.width = int32_t(native.extent.width);
        info.height = int32_t(native.extent.height);
        info.interopImageFormat = api::REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM;
        info.depthImageMemory_Handle = handle(native.depthFd);
        info.depthAllocationSize = native.depthAllocationSize;
        info.depthWidth = int32_t(native.depthExtent.width);
        info.depthHeight = int32_t(native.depthExtent.height);
        info.interopDepthImageFormat = api::REMIXAPI_TEXTURE_FORMAT_R32_SFLOAT;
      });
    }

    api::remixapi_ErrorCode REMIXAPI_CALL createMaterial(const api::remixapi_MaterialInfo* info, api::remixapi_MaterialHandle* handle) {
      return invoke("CreateMaterial", [&](State& state) {
        auto& resources = device(state);
        auto& result = output(handle);
        result = resources.materials.create(input(info));
      });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL updateMaterial(api::remixapi_MaterialHandle handle, const api::remixapi_MaterialInfo* info) {
      return invoke("UpdateMaterial", [&](State& state) { device(state).materials.update(handle, input(info)); });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL destroyMaterial(api::remixapi_MaterialHandle handle) {
      return invoke("DestroyMaterial", [&](State& state) { device(state).materials.destroy(handle); });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL createMesh(const api::remixapi_MeshInfo* info, api::remixapi_MeshHandle* handle) {
      return invoke("CreateMesh", [&](State& state) {
        auto& resources = device(state);
        auto& result = output(handle);
        result = resources.meshes.create(input(info));
      });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL destroyMesh(api::remixapi_MeshHandle handle) {
      return invoke("DestroyMesh", [&](State& state) { device(state).meshes.destroy(handle); });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL createTexture(const api::remixapi_TextureFromMemoryInfo* info, api::remixapi_TextureHandle* handle) {
      return invoke("CreateTextureFromMemory", [&](State& state) {
        auto& resources = device(state);
        auto& result = output(handle);
        result = resources.textures.create(input(info));
      });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL createFileTexture(api::remixapi_Path path, api::remixapi_TextureHandle* handle) {
      return invoke("CreateTextureFromFile", [&](State& state) {
        auto& resources = device(state);
        auto& result = output(handle);
        result = resources.textures.createFile(texturePath(path));
      });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL updateTexture(const api::remixapi_TexturePatchInfo* info) {
      return invoke("UpdateTextureFromMemory", [&](State& state) { device(state).textures.update(input(info)); });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL destroyTexture(api::remixapi_TextureHandle handle) {
      return invoke("DestroyTexture", [&](State& state) { device(state).textures.destroy(handle); });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL createLight(const api::remixapi_LightInfo* info, api::remixapi_LightHandle* handle) {
      return invoke("CreateLight", [&](State& state) {
        auto& resources = device(state);
        auto& result = output(handle);
        result = resources.lights.create(input(info));
      });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL destroyLight(api::remixapi_LightHandle handle) {
      return invoke("DestroyLight", [&](State& state) { device(state).lights.destroy(handle); });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL drawLight(api::remixapi_LightHandle handle) {
      return invoke("DrawLightInstance", [&](State& state) {
        auto& resources = device(state);
        capture(state);
        if (state.phase != Phase::Skipped) {
          resources.lights.draw(handle);
        }
      });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL camera(const api::remixapi_CameraInfo* info) {
      return invoke("SetupCamera", [&](State& state) {
        auto& resources = device(state);
        capture(state);
        if (state.phase != Phase::Skipped) {
          resources.cameras.setup(input(info));
        }
      });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL medium(const api::remixapi_CameraMediumInfo* info) {
      return invoke("SetCameraMediumMaterial", [&](State& state) { device(state).cameras.setMedium(input(info)); });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL draw(const api::remixapi_InstanceInfo* info) {
      return invoke("DrawInstance", [&](State& state) {
        auto& resources = device(state);
        capture(state);
        if (state.phase != Phase::Skipped) {
          resources.instances.draw(input(info));
        }
      });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL add(const api::remixapi_InstanceInfo* info, api::remixapi_SceneInstanceHandle* handle) {
      return invoke("SceneInstanceAdd", [&](State& state) {
        auto& resources = device(state);
        auto& result = output(handle);
        result = resources.instances.add(input(info));
      });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL update(api::remixapi_SceneInstanceHandle handle, const api::remixapi_InstanceInfo* info) {
      return invoke("SceneInstanceUpdate", [&](State& state) { device(state).instances.update(handle, input(info)); });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL remove(api::remixapi_SceneInstanceHandle handle) {
      return invoke("SceneInstanceRemove", [&](State& state) { device(state).instances.remove(handle); });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL setOption(const char* key, const char* value) {
      return invoke("SetConfigVariable", [&](State& state) { state.options.set(key, value); });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL readOption(api::remixapi_ConfigVariableResolvedInfo* info) {
      return invoke("GetConfigVariableResolved", [&](State& state) { state.options.read(output(info)); });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL acquire(const char* path, float priority, float threshold, api::remixapi_ConfigLayerHandle* handle) {
      return invoke("AcquireConfigLayer", [&](State& state) {
        device(state, false);
        auto& result = output(handle);
        result = state.options.acquire(path, priority, threshold);
      });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL blend(api::remixapi_ConfigLayerHandle handle, float strength, float threshold) {
      return invoke("SetConfigLayerBlending", [&](State& state) {
        device(state, false);
        state.options.blend(handle, strength, threshold);
      });
    }
    api::remixapi_ErrorCode REMIXAPI_CALL release(api::remixapi_ConfigLayerHandle handle) {
      return invoke("ReleaseConfigLayer", [&](State& state) {
        device(state, false);
        state.options.release(handle);
      });
    }

    api::remixapi_ErrorCode unsupported() {
      return api::REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
    }
    api::remixapi_ErrorCode REMIXAPI_CALL d3d(api::remixapi_Bool, api::IDirect3D9Ex**) { return unsupported(); }
    api::remixapi_ErrorCode REMIXAPI_CALL registerD3d(api::IDirect3DDevice9Ex*) { return unsupported(); }
    api::remixapi_ErrorCode REMIXAPI_CALL swapchain(uint64_t*, uint64_t*, uint64_t*) { return unsupported(); }
    api::remixapi_ErrorCode REMIXAPI_CALL vkImage(api::IDirect3DSurface9*, uint64_t*) { return unsupported(); }
    api::remixapi_ErrorCode REMIXAPI_CALL copyOutput(api::IDirect3DSurface9*, api::remixapi_dxvk_CopyRenderingOutputType) { return unsupported(); }
    api::remixapi_ErrorCode REMIXAPI_CALL defaultOutput(api::remixapi_dxvk_CopyRenderingOutputType, const api::remixapi_Float4D*) { return unsupported(); }
    api::remixapi_ErrorCode REMIXAPI_CALL pick(const api::remixapi_Rect2D*, api::PFN_remixapi_pick_RequestObjectPickingUserCallback, void*) { return unsupported(); }
    api::remixapi_ErrorCode REMIXAPI_CALL highlight(const uint32_t*, uint32_t, uint8_t, uint8_t, uint8_t) { return unsupported(); }

    api::remixapi_Interface table() {
      api::remixapi_Interface result { };
      result.Shutdown = shutdown;
      result.CreateMaterial = createMaterial;
      result.DestroyMaterial = destroyMaterial;
      result.CreateMesh = createMesh;
      result.DestroyMesh = destroyMesh;
      result.SetupCamera = camera;
      result.DrawInstance = draw;
      result.CreateLight = createLight;
      result.DestroyLight = destroyLight;
      result.DrawLightInstance = drawLight;
      result.SetConfigVariable = setOption;
      result.dxvk_CreateD3D9 = d3d;
      result.dxvk_RegisterD3D9Device = registerD3d;
      result.dxvk_GetExternalSwapchain = swapchain;
      result.dxvk_GetVkImage = vkImage;
      result.dxvk_CopyRenderingOutput = copyOutput;
      result.dxvk_SetDefaultOutput = defaultOutput;
      result.pick_RequestObjectPicking = pick;
      result.pick_HighlightObjects = highlight;
      result.Startup = startup;
      result.Present = present;
      result.CreateTextureFromMemory = createTexture;
      result.CreateTextureFromFile = createFileTexture;
      result.DestroyTexture = destroyTexture;
      result.SetCameraMediumMaterial = medium;
      result.UpdateTextureFromMemory = updateTexture;
      result.dxvk_GetInteropInfo = interop;
      result.Submit = submit;
      result.AcquireConfigLayer = acquire;
      result.SetConfigLayerBlending = blend;
      result.ReleaseConfigLayer = release;
      result.SceneInstanceAdd = add;
      result.SceneInstanceUpdate = update;
      result.SceneInstanceRemove = remove;
      result.UpdateMaterial = updateMaterial;
      result.GetConfigVariableResolved = readOption;
      return result;
    }
  }

  namespace api016 {
    // The ELF export aliases this name to the published initializer symbol.
    extern "C" REMIXAPI remixapi_ErrorCode REMIXAPI_CALL remixapi016_InitializeLibraryDeclaration(
        const remixapi_InitializeLibraryInfo* info, remixapi_Interface* result) {
      if (!info || !result || info->sType != REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO) {
        return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
      }
      if (info->version != kVersion) {
        return REMIXAPI_ERROR_CODE_INCOMPATIBLE_VERSION;
      }
      std::lock_guard<std::mutex> lock(s_mutex);
      try {
        if (!s_state) {
          s_state = std::make_unique<State>();
        }
        *result = table();
        return REMIXAPI_ERROR_CODE_SUCCESS;
      } catch (...) {
        return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
      }
    }

    extern "C" REMIXAPI remixapi_ErrorCode REMIXAPI_CALL remixapi_BeginFrame(uint32_t width, uint32_t height) {
      return beginFrame(width, height);
    }

    extern "C" REMIXAPI remixapi_ErrorCode REMIXAPI_CALL remixapi_GetOverlayInteropInfo(
        remixapi_dxvk_InteropInfo* pInfo, uint32_t* pVisible) {
      return invoke("GetOverlayInteropInfo", [&](State& state) {
        auto& host = device(state).host;
        auto& info = output(pInfo);
        auto& visible = output(pVisible);
        if (state.phase == Phase::Recording || state.phase == Phase::Skipped) {
          invalid("Overlay images are published by Submit");
        }
        host.enableOverlay();
        const auto native = host.interopInfo();
        info = { };
        visible = 0;
        if (state.phase != Phase::SkippedSubmitted && native.overlayFd >= 0) {
          info.imageMemory_Handle = uint64_t(native.overlayFd);
          info.allocationSize = native.overlayAllocationSize;
          info.width = int32_t(native.overlayExtent.width);
          info.height = int32_t(native.overlayExtent.height);
          info.interopImageFormat = REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM;
          visible = native.overlayVisible;
        }
      });
    }

    extern "C" REMIXAPI remixapi_ErrorCode REMIXAPI_CALL remixapi_InjectOverlayInput(
        uint32_t type, int32_t code, double x, double y) {
      return invoke("InjectOverlayInput", [&](State& state) {
        if (type > 5 || !std::isfinite(x) || !std::isfinite(y)
            || (type == 4 && (code < 0 || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)))) {
          invalid("Invalid native overlay input");
        }
        device(state).host.overlayInput(type, code, x, y);
      });
    }
  }
}
