// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <algorithm>
#include <array>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#include "runtime_api.h"
#include "gl_output_reader.h"

namespace {
  namespace api = dxvk::native::api016;
  using dxvk::native::test::require;

  void success(api::remixapi_ErrorCode code) {
    require(code == api::REMIXAPI_ERROR_CODE_SUCCESS, "native C API operation failed");
  }

  struct Session {
    void* library = nullptr;
    api::PFN_remixapi_InitializeLibrary initialize = nullptr;
    dxvk::native::BeginFrameFunction begin = nullptr;
    dxvk::native::OverlayInteropFunction overlayInterop = nullptr;
    dxvk::native::OverlayInputFunction input = nullptr;
    bool overlayEnabled = false;
    api::remixapi_Interface table { };
    uint64_t clientValue = 0;
    bool waitForClient = true;

    explicit Session(const char* pPath) {
      library = dlopen(pPath, RTLD_NOW | RTLD_LOCAL);
      if (!library) {
        throw std::runtime_error(dlerror());
      }
      initialize = reinterpret_cast<api::PFN_remixapi_InitializeLibrary>(dlsym(library, "remixapi_InitializeLibrary"));
      begin = reinterpret_cast<dxvk::native::BeginFrameFunction>(dlsym(library, dxvk::native::kBeginFrameSymbol));
      overlayInterop = reinterpret_cast<dxvk::native::OverlayInteropFunction>(dlsym(library, "remixapi_GetOverlayInteropInfo"));
      input = reinterpret_cast<dxvk::native::OverlayInputFunction>(dlsym(library, "remixapi_InjectOverlayInput"));
      require(initialize && begin && overlayInterop && input, "production runtime lacks its C entry points");
    }

    ~Session() {
      if (table.Shutdown) {
        table.Shutdown();
      }
      if (library) {
        dlclose(library);
      }
    }

    void init() {
      api::remixapi_InitializeLibraryInfo info { };
      info.sType = api::REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO;
      info.version = api::kVersion;
      success(initialize(&info, &table));
    }

    dxvk::native::OutputInteropInfo interopInfo() const {
      api::remixapi_dxvk_InteropInfo info { };
      success(table.dxvk_GetInteropInfo(&info));
      const auto fd = [](uint64_t handle) { return handle ? int(handle) : -1; };
      dxvk::native::OutputInteropInfo result;
      result.rendererDoneFd = fd(info.dxvkDoneSemaphore_Handle);
      result.clientDoneFd = fd(info.clientDoneSemaphore_Handle);
      result.rendererDoneValue = info.dxvkDoneSemaphore_ValueToWait;
      result.imageFd = fd(info.imageMemory_Handle);
      result.depthFd = fd(info.depthImageMemory_Handle);
      result.extent = { uint32_t(info.width), uint32_t(info.height), 1 };
      result.depthExtent = { uint32_t(info.depthWidth), uint32_t(info.depthHeight), 1 };
      result.allocationSize = info.allocationSize;
      result.depthAllocationSize = info.depthAllocationSize;
      if (overlayEnabled) {
        api::remixapi_dxvk_InteropInfo overlay { };
        uint32_t visible = 0;
        success(overlayInterop(&overlay, &visible));
        result.overlayFd = fd(overlay.imageMemory_Handle);
        result.overlayAllocationSize = overlay.allocationSize;
        result.overlayExtent = { uint32_t(overlay.width), uint32_t(overlay.height), 1 };
        result.overlayVisible = visible;
      }
      require(info.interopImageFormat == api::REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM
          && info.interopDepthImageFormat == api::REMIXAPI_TEXTURE_FORMAT_R32_SFLOAT,
        "C API advertised incorrect interop image formats");
      return result;
    }

    void present(uint64_t value) {
      auto info = api::remixapi_PresentInfo_init();
      info.skipVulkanSwapchain = 1;
      info.clientDoneSemaphore_ValueToWait = value;
      info.disableCpuWaitForExternalSemaphores = !waitForClient;
      const auto result = table.Present(&info);
      if (result != api::REMIXAPI_ERROR_CODE_SUCCESS) {
        throw dxvk::DxvkError("C API rejected the client completion value");
      }
      clientValue = value;
    }
  };

  void handshake(Session& session) {
    api::remixapi_InitializeLibraryInfo info { };
    info.sType = api::REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO;
    info.version = REMIXAPI_VERSION_MAKE(0, 6, 6);
    alignas(8) std::array<uint8_t, 176> oldTable;
    oldTable.fill(0xa5);
    require(session.initialize(&info, reinterpret_cast<api::remixapi_Interface*>(oldTable.data()))
        == api::REMIXAPI_ERROR_CODE_INCOMPATIBLE_VERSION,
      "native API accepted an incompatible interface layout");
    require(std::all_of(oldTable.begin(), oldTable.end(), [](uint8_t value) { return value == 0xa5; }),
      "incompatible-version rejection wrote beyond the caller's old interface");
    require(session.initialize(nullptr, &session.table) == api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS,
      "native API accepted missing initialization info");
    session.init();
    std::array<uintptr_t, 36> slots { };
    std::memcpy(slots.data(), &session.table, sizeof(session.table));
    require(std::all_of(slots.begin(), slots.end(), [](uintptr_t value) { return value != 0; }),
      "production C table is incomplete");
    api::remixapi_dxvk_InteropInfo interop { };
    require(session.table.dxvk_GetInteropInfo(&interop) == api::REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED,
      "interop was advertised before Startup");
    require(session.begin(96, 64) == api::REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED,
      "frame recording succeeded before Startup");
    api::remixapi_ConfigLayerHandle layer = nullptr;
    require(session.table.AcquireConfigLayer("missing.conf", 10000, .5f, &layer)
        == api::REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED,
      "config layer pre-Startup contract changed");
    success(session.table.SetConfigVariable("rtx.fallbackLightMode", "0"));
    success(session.table.SetConfigVariable("rtx.autoExposure.enabled", "False"));
    success(session.table.SetConfigVariable("rtx.graphicsPreset", "4"));
    success(session.table.SetConfigVariable("rtx.upscalerType", "0"));
    api::remixapi_StartupInfo start { };
    start.sType = api::REMIXAPI_STRUCT_TYPE_STARTUP_INFO;
    start.forceNoVkSwapchain = start.interopImage = 1;
    success(session.table.Startup(&start));
    const auto resolved = [&](const char* pKey) {
      auto option = api::remixapi_ConfigVariableResolvedInfo_init();
      option.key = pKey;
      success(session.table.GetConfigVariableResolved(&option));
      std::vector<char> value(option.valueSize);
      option.value = value.data();
      success(session.table.GetConfigVariableResolved(&option));
      return std::string(value.data());
    };
    require(resolved("rtx.pathMaxBounces") == "4" && resolved("rtx.postfx.enable") == "True",
      "pre-Startup graphics preset was applied after device quality initialization");
    require(resolved("rtx.upscalerType") == "0", "Startup ignored the disabled upscaler");
    require(resolved("rtx.spatiallyHashedRadianceCache.downscaleFactor") == "4"
        && resolved("rtx.spatiallyHashedRadianceCache.maxNumFramesToAccumulate") == "100",
      "Custom startup did not preserve the original API 0.16 radiance-cache defaults");
    require(session.table.Startup(&start) == api::REMIXAPI_ERROR_CODE_ALREADY_EXISTS, "duplicate Startup succeeded");
    const auto native = session.interopInfo();
    require(native.rendererDoneFd >= 3 && native.clientDoneFd >= 3 && native.imageFd == -1,
      "Startup did not expose semaphores independently of the first image");
    require(session.table.dxvk_CreateD3D9(0, nullptr) == api::REMIXAPI_ERROR_CODE_GENERAL_FAILURE,
      "native runtime pretended to implement D3D9 COM");
  }

  api::remixapi_Transform transform(float x = 0) {
    api::remixapi_Transform value { };
    value.matrix[0][0] = value.matrix[1][1] = value.matrix[2][2] = 1;
    value.matrix[0][3] = x;
    return value;
  }

  void setupCamera(Session& session, uint32_t width, uint32_t height) {
    api::remixapi_CameraInfoParameterizedEXT parameters { };
    parameters.sType = api::REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT;
    parameters.forward = { 0, 0, 1 };
    parameters.up = { 0, 1, 0 };
    parameters.right = { 1, 0, 0 };
    parameters.fovYInDegrees = 60;
    parameters.aspect = width && height ? float(width) / height : 1.f;
    parameters.nearPlane = .05f;
    parameters.farPlane = 100;
    api::remixapi_CameraInfo info { };
    info.sType = api::REMIXAPI_STRUCT_TYPE_CAMERA_INFO;
    info.pNext = &parameters;
    success(session.table.SetupCamera(&info));
    api::remixapi_CameraMediumInfo medium { };
    medium.sType = api::REMIXAPI_STRUCT_TYPE_CAMERA_MEDIUM_INFO;
    success(session.table.SetCameraMediumMaterial(&medium));
  }

  size_t color(const std::vector<uint8_t>& pixels, uint32_t channel) {
    size_t count = 0;
    for (size_t index = 0; index < pixels.size(); index += 4) {
      count += pixels[index + channel] > 90 && pixels[index + channel] > 2 * unsigned(pixels[index + (channel + 1) % 3])
        && pixels[index + channel] > 2 * unsigned(pixels[index + (channel + 2) % 3]);
    }
    return count;
  }

  void fileTexture(Session& session) {
    const auto path = std::filesystem::u8path(u8"native-апи-файл.dds");
    std::array<uint32_t, 32> header { };
    header[0] = 0x20534444; header[1] = 124; header[2] = 0x100f;
    header[3] = header[4] = 2; header[5] = 8; header[7] = 1;
    header[19] = header[22] = 32; header[20] = 0x41;
    header[23] = 0xff; header[24] = 0xff00; header[25] = 0xff0000; header[26] = 0xff000000; header[27] = 0x1000;
    const std::array<uint32_t, 4> data { 0xff00ff00, 0xff00ff00, 0xff00ff00, 0xff00ff00 };
    {
      std::ofstream file(path, std::ios::binary);
      file.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
      file.write(reinterpret_cast<const char*>(data.data()), sizeof(data));
    }
    api::remixapi_TextureHandle handle = nullptr;
    success(session.table.CreateTextureFromFile(L"native-апи-файл.dds", &handle));
    require(handle, "wide file path did not produce a texture");
    success(session.table.DestroyTexture(handle));
    std::filesystem::remove(path);
  }

  void configuration(Session& session) {
    const auto path = std::filesystem::current_path() / "native-api.conf";
    { std::ofstream file(path); file << "rtx.sceneScale = 1.25\n"; }
    api::remixapi_ConfigLayerHandle layer = nullptr;
    success(session.table.AcquireConfigLayer(path.u8string().c_str(), 10000, .5f, &layer));
    success(session.table.SetConfigLayerBlending(layer, 1, .5f));
    success(session.table.ReleaseConfigLayer(layer));
    std::filesystem::remove(path);
    auto info = api::remixapi_ConfigVariableResolvedInfo_init();
    info.key = "rtx.autoExposure.enabled";
    success(session.table.GetConfigVariableResolved(&info));
    require(info.valueSize > 0, "resolved option sizing did not cross the C boundary");
    char tooSmall = 'x';
    info.value = &tooSmall;
    info.valueSize = 1;
    require(session.table.GetConfigVariableResolved(&info) == api::REMIXAPI_ERROR_CODE_BUFFER_TOO_SMALL && tooSmall == 'x',
      "resolved option overflow wrote to the caller's buffer");
    std::vector<char> value(info.valueSize);
    info.value = value.data();
    success(session.table.GetConfigVariableResolved(&info));
    require(!value.empty() && value.back() == 0, "resolved option omitted its terminator");
    api::remixapi_ErrorCode threadResult = api::REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
    std::thread worker([&] { threadResult = session.table.SetConfigVariable("rtx.autoExposure.enabled", "False"); });
    worker.join();
    success(threadResult);
  }

  void scene(Session& session, dxvk::native::test::Reader& reader) {
    const std::array<uint8_t, 4> red { 255, 0, 0, 255 };
    const std::array<uint8_t, 4> green { 0, 255, 0, 255 };
    api::remixapi_TextureFromMemoryInfo texture { };
    texture.sType = api::REMIXAPI_STRUCT_TYPE_TEXTURE_FROM_MEMORY_INFO;
    texture.hash = 0x811;
    texture.width = texture.height = 1;
    texture.format = api::REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM;
    texture.pixelData = red.data();
    require(session.table.CreateTextureFromMemory(&texture, nullptr) == api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS,
      "texture creation accepted a null output");
    texture.pixelData = green.data();
    api::remixapi_TextureHandle image = nullptr;
    success(session.table.CreateTextureFromMemory(&texture, &image));
    api::remixapi_MaterialInfoOpaqueEXT opaque { };
    opaque.sType = api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT;
    opaque.albedoConstant = { 1, 1, 1 };
    opaque.opacityConstant = 1;
    opaque.roughnessConstant = .6f;
    opaque.alphaTestType = 7;
    api::remixapi_MaterialInfo material { };
    material.sType = api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
    material.pNext = &opaque;
    material.albedoTexture = material.emissiveTexture = image;
    material.emissiveColorConstant = { 1, 1, 1 };
    material.emissiveIntensity = 2;
    material.spriteSheetRow = material.spriteSheetCol = 1;
    material.wrapModeU = material.wrapModeV = 1;
    api::remixapi_MaterialHandle mat = nullptr;
    success(session.table.CreateMaterial(&material, &mat));
    std::array<api::remixapi_HardcodedVertex, 3> vertices { };
    const float positions[3][3] = { { -1.3f, -1, 3 }, { 1.3f, -1, 3 }, { 0, 1, 3 } };
    for (uint32_t index = 0; index < 3; ++index) {
      std::copy_n(positions[index], 3, vertices[index].position);
      vertices[index].normal[2] = -1;
      vertices[index].color = 0xffffffff;
    }
    api::remixapi_MeshInfoSurfaceTriangles surface { };
    surface.vertices_values = vertices.data();
    surface.vertices_count = vertices.size();
    surface.material = mat;
    api::remixapi_MeshInfo geometry { };
    geometry.sType = api::REMIXAPI_STRUCT_TYPE_MESH_INFO;
    geometry.surfaces_values = &surface;
    geometry.surfaces_count = 1;
    api::remixapi_MeshHandle mesh = nullptr;
    success(session.table.CreateMesh(&geometry, &mesh));
    api::remixapi_InstanceInfo instance { };
    instance.sType = api::REMIXAPI_STRUCT_TYPE_INSTANCE_INFO;
    instance.mesh = mesh;
    instance.transform = transform();
    instance.doubleSided = 1;
    api::remixapi_SceneInstanceHandle retained = nullptr;
    success(session.table.SceneInstanceAdd(&instance, &retained));
    api::remixapi_LightInfoSphereEXT sphere { };
    sphere.sType = api::REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
    sphere.position = { 0, 0, 1 };
    sphere.radius = .1f;
    sphere.volumetricRadianceScale = 1;
    api::remixapi_LightInfo light { };
    light.sType = api::REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
    light.pNext = &sphere;
    light.hash = 0x812;
    light.radiance = { 0, 1, 0 };
    api::remixapi_LightHandle lamp = nullptr;
    success(session.table.CreateLight(&light, &lamp));
    std::vector<uint8_t> pixels;
    uint32_t frame = 0;
    auto step = [&](uint32_t width = 96, uint32_t height = 64, bool transient = false) {
      success(session.begin(width, height));
      require(session.begin(width, height) == api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS,
        "duplicate BeginFrame was accepted");
      api::remixapi_dxvk_InteropInfo unpublished { };
      require(session.table.dxvk_GetInteropInfo(&unpublished) == api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS,
        "recording frame was advertised before Submit");
      setupCamera(session, width, height);
      if (lamp) {
        success(session.table.DrawLightInstance(lamp));
      }
      if (transient) {
        instance.categoryFlags = api::REMIXAPI_INSTANCE_CATEGORY_BIT_WORLD_UI;
        success(session.table.DrawInstance(&instance));
        instance.categoryFlags = 0;
      }
      auto submit = api::remixapi_SubmitInfo_init();
      submit.interopFlags = frame == 1 ? api::REMIXAPI_SUBMIT_INTEROP_BIT_FORCE_WAIT_DEVICE_IDLE
        : (frame & 1 ? api::REMIXAPI_SUBMIT_INTEROP_BIT_DISABLE_CPU_WAIT_FOR_VK_SUBMIT : 0);
      success(session.table.Submit(&submit));
      session.waitForClient = !(frame++ & 1);
      pixels = reader.readAndPresent(session);
    };
    for (uint32_t i = 0; i < 8; ++i) { step(); }
    require(color(pixels, 1) > 200, "C API renderer lost the green texture or mutated state after an invalid output argument");
    step(128, 96, true);
    require(pixels.size() == 128 * 96 * 4, "C API resize did not update shared images");
    step(0, 0);
    require(pixels.empty(), "minimized C API frame published an image");
    step();
    const std::array<uint8_t, 4> blue { 0, 0, 255, 255 };
    const api::remixapi_TexturePatch patch { 0, 0, 1, 1, blue.data(), 4, api::REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM };
    const api::remixapi_TextureBatchEntry entry { image, 1, &patch };
    const api::remixapi_TexturePatchInfo update { api::REMIXAPI_STRUCT_TYPE_TEXTURE_PATCH_INFO, nullptr, 1, &entry };
    success(session.table.UpdateTextureFromMemory(&update));
    success(session.table.DestroyTexture(image));
    for (uint32_t i = 0; i < 8; ++i) { step(); }
    require(color(pixels, 2) > 200, "C API texture patch or retained image ownership failed");
    material.albedoTexture = material.emissiveTexture = nullptr;
    material.emissiveColorConstant = { 1, 0, 0 };
    success(session.table.UpdateMaterial(mat, &material));
    instance.transform = transform(.2f);
    success(session.table.SceneInstanceUpdate(retained, &instance));
    success(session.table.DestroyLight(lamp));
    lamp = nullptr;
    for (uint32_t i = 0; i < 8; ++i) { step(); }
    require(color(pixels, 0) > 200, "C API material/instance update failed");
    success(session.table.DestroyMesh(mesh));
    success(session.table.SceneInstanceRemove(retained));
    success(session.table.DestroyMaterial(mat));
    step();
    require(color(pixels, 0) == 0 && color(pixels, 1) == 0 && color(pixels, 2) == 0,
      "C API resource removal left ghost geometry");
    std::cout << "C ABI frames=" << frame << ", last client value=" << session.clientValue << '\n';
  }

  void overlay(Session& session, dxvk::native::test::Reader& reader) {
    session.overlayEnabled = true;
    session.interopInfo();
    success(session.table.SetConfigVariable("rtx.hideSplashMessage", "True"));
    success(session.table.SetConfigVariable("rtx.showUI", "1"));
    success(session.input(5, 1, 0, 0));
    success(session.input(3, GLFW_KEY_A, 1, GLFW_MOD_CONTROL));
    success(session.input(4, 0x416, 0, 0));
    success(session.input(0, 0, 100, 120));
    success(session.input(1, 0, 1, GLFW_MOD_CONTROL));
    require(session.input(6, 0, 0, 0) == api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS
        && session.input(4, 0xd800, 0, 0) == api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS,
      "native overlay accepted an invalid event or surrogate character");
    const auto step = [&](uint32_t width = 960, uint32_t height = 540) {
      success(session.begin(width, height));
      success(session.table.Submit(nullptr));
      reader.readAndPresent(session);
    };
    for (uint32_t i = 0; i < 4; ++i) { step(); }
    const auto info = session.interopInfo();
    require(info.overlayFd >= 3 && info.overlayVisible && info.overlayExtent.width == 960,
      "native C API did not publish the original Remix menu separately from the scene");
    const auto alphaPixels = [&] {
      size_t count = 0;
      const auto& pixels = reader.overlayPixels();
      for (size_t i = 3; i < pixels.size(); i += 4) count += pixels[i] > 0;
      return count;
    };
    require(alphaPixels() > 1000, "original Remix menu produced an empty GPU overlay");
    success(session.input(5, 0, 0, 0));
    step();
    success(session.table.SetConfigVariable("rtx.showUI", "0"));
    step();
    step();
    require(!session.interopInfo().overlayVisible && alphaPixels() == 0,
      "closing the Remix menu left its previous pixels in the overlay");
    step(0, 0);
    require(reader.overlayPixels().empty(), "minimized native window retained a UI image");
    step(512, 384);
    require(reader.overlayPixels().size() == 512 * 384 * 4,
      "native overlay did not recover with the resized framebuffer");
    std::cout << "PASS original Remix UI GPU overlay, input protocol, close, resize and minimize\n";
  }
}

int main(int argc, char** argv) {
  if (argc != 2) { return 1; }
  dxvk::native::test::Window window;
  if (!glfwInit()) { return 77; }
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
  window.pHandle = glfwCreateWindow(128, 96, "Native runtime C ABI", nullptr, nullptr);
  if (!window.pHandle) { return 77; }
  glfwMakeContextCurrent(window.pHandle);
  try {
    for (uint32_t run = 0; run < 2; ++run) {
      Session session(argv[1]);
      handshake(session);
      fileTexture(session);
      configuration(session);
      {
        dxvk::native::test::Reader reader(session.interopInfo());
        scene(session, reader);
        overlay(session, reader);
      }
      success(session.table.Shutdown());
      require(session.begin(96, 64) == api::REMIXAPI_ERROR_CODE_NOT_INITIALIZED, "Shutdown kept its native API session alive");
    }
    std::cout << "PASS production native C API, 36 slots, GPU scene, GL interop, resize, minimize and restart\n";
    return 0;
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "FAIL " << error.message() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
  }
  return 1;
}
