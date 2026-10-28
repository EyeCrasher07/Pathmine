// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>

#include "api016_error.h"
#include "api016_options.h"
#include "dxvk/rtx_render/rtx_option_manager.h"
#include "runtime_host.h"
#include "util/util_error.h"

namespace {
  namespace api = dxvk::native::api016;
  using dxvk::native::ApiOptions;

  void require(bool condition, const char* pMessage) {
    if (!condition) {
      throw std::runtime_error(pMessage);
    }
  }

  template<typename F>
  void expectError(api::remixapi_ErrorCode code, F&& action) {
    try {
      action();
    } catch (const dxvk::native::ApiError& error) {
      require(error.code() == code, "API option operation returned the wrong error");
      return;
    }
    throw std::runtime_error("invalid API option operation succeeded");
  }

  std::string read(const ApiOptions& options, const char* pKey) {
    auto info = api::remixapi_ConfigVariableResolvedInfo_init();
    info.key = pKey;
    options.read(info);
    std::string result(info.valueSize, '\0');
    info.value = result.data();
    options.read(info);
    require(!result.empty() && result.back() == '\0', "API option read omitted its NUL terminator");
    result.pop_back();
    return result;
  }

  void writeFile(const std::filesystem::path& path, const std::string& value) {
    std::ofstream file(path);
    file << value;
    require(bool(file), "cannot write native option-layer fixture");
  }

  void closeMenu() {
    dxvk::Config value;
    value.setOption("rtx.showUI", 0);
    std::lock_guard<std::mutex> lock(dxvk::RtxOptionImpl::getUpdateMutex());
    dxvk::RtxOptionImpl::getOptionByFullName("rtx.showUI")->readOption(value, dxvk::RtxOptionLayer::getDerivedLayer());
  }
}

int main() {
  using dxvk::RtxOptionManager;
  using dxvk::RtxOptionImpl;
  try {
    ApiOptions options;
    expectError(api::REMIXAPI_ERROR_CODE_NOT_INITIALIZED, [&] { read(options, "rtx.sceneScale"); });
    expectError(api::REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED,
      [&] { options.acquire("missing.conf", 3000, .5f); });
    require(RtxOptionManager::registerDynamicNamespace({ "rtx.nativeAdapter.vector.", dxvk::OptionType::Vector3,
      "0.1, 0.2, 0.3", 0, "Native API typed layer verification" }), "cannot register vector test namespace");
    require(RtxOptionManager::registerDynamicNamespace({ "rtx.nativeAdapter.text.", dxvk::OptionType::String,
      "default", 0, "Native API string verification" }), "cannot register string test namespace");
    require(RtxOptionManager::registerDynamicNamespace({ "rtx.nativeAdapter.menu.", dxvk::OptionType::Int,
      "0", dxvk::RtxOptionFlags::NoSave, "Native API unsaved option verification" }),
      "cannot register unsaved test namespace");
    options.set("rtx.sceneScale", "2");
    options.set("rtx.nativeAdapter.menu.open", "1");
    dxvk::native::RuntimeHost host(std::filesystem::current_path().u8string());
    const auto apply = [&] { RtxOptionManager::applyPendingValues(host.device().ptr(), false); };
    options.applyStartupWrites();
    apply();
    require(read(options, "rtx.sceneScale") == "2", "pre-startup API write was not applied after configuration initialization");
    require(read(options, "rtx.nativeAdapter.menu.open") == "1", "pre-startup dynamic API write was lost");
    auto* pMenu = RtxOptionImpl::getOptionByFullName("rtx.nativeAdapter.menu.open");
    require(pMenu->hasValueInLayer(dxvk::RtxOptionLayer::getDerivedLayer())
        && !pMenu->hasValueInLayer(dxvk::RtxOptionLayer::getUserLayer()),
      "pre-startup unsaved dynamic option was routed into the saved user layer");
    require(read(options, "rtx.nativeAdapter.vector.unset") == "0.1, 0.2, 0.3",
      "reading an untouched dynamic key did not return the namespace default");
    expectError(api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS,
      [&] { read(options, "rtx.nativeAdapter.vector.bad..key"); });
    expectError(api::REMIXAPI_ERROR_CODE_GENERAL_FAILURE, [&] { read(options, "rtx.not_a_real_option"); });
    expectError(api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, [&] { options.set("rtx.sceneScale", "NaN"); });
    expectError(api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, [&] { options.set("rtx.showUI", "2oops"); });
    expectError(api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, [&] { options.set("rtx.nativeAdapter.vector.bad", "1,2"); });
    expectError(api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, [&] { options.set(nullptr, "1"); });
    expectError(api::REMIXAPI_ERROR_CODE_GENERAL_FAILURE, [&] { options.set("rtx.not_a_real_option", "1"); });

    options.set("rtx.nativeAdapter.text.greeting", u8"Привет, RTX");
    apply();
    require(read(options, "rtx.nativeAdapter.text.greeting") == u8"Привет, RTX",
      "resolved API strings lost their UTF-8 bytes");
    auto info = api::remixapi_ConfigVariableResolvedInfo_init();
    info.key = "rtx.nativeAdapter.text.greeting";
    std::array<char, 8> shortBuffer;
    shortBuffer.fill('x');
    info.value = shortBuffer.data();
    info.valueSize = shortBuffer.size();
    expectError(api::REMIXAPI_ERROR_CODE_BUFFER_TOO_SMALL, [&] { options.read(info); });
    require(info.valueSize == std::strlen(u8"Привет, RTX") + 1
        && std::all_of(shortBuffer.begin(), shortBuffer.end(), [](char c) { return c == 'x'; }),
      "short API option buffer was partially overwritten or reported the wrong byte capacity");

    const float base = std::stof(read(options, "rtx.vertexColorStrength"));
    const bool initialCulling = read(options, "rtx.enableCulling") == "True";
    const std::string culling = initialCulling ? "False" : "True";
    const auto lowPath = std::filesystem::absolute(u8"профиль-low.conf");
    const auto highPath = std::filesystem::absolute("profile-high.conf");
    const auto emptyPath = std::filesystem::absolute("profile-empty.conf");
    writeFile(lowPath, "rtx.vertexColorStrength = 0.2\nrtx.enableCulling = " + culling
      + "\nrtx.nativeAdapter.vector.sky = 0.8, 0.6, 0.4\n");
    writeFile(highPath, "rtx.vertexColorStrength = 0.9\n");
    writeFile(emptyPath, "# no options\n");
    const auto low = options.acquire(lowPath.c_str(), 3000, .5f);
    const auto duplicate = options.acquire(lowPath.c_str(), 3000, 0);
    require(duplicate == low, "cached option-layer acquire did not return the same handle");
    apply();
    require(std::abs(std::stof(read(options, "rtx.vertexColorStrength")) - base) < 1e-5,
      "new API option layer did not start at zero strength");
    writeFile(lowPath, "rtx.vertexColorStrength = 0.99\n");
    const auto cached = options.acquire(lowPath.c_str(), 3000, .5f);
    require(cached == low, "editing a cached option layer changed its handle");
    options.blend(low, .2f, .9f);
    options.blend(low, .5f, .7f);
    apply();
    const float lowValue = (base + .2f) * .5f;
    require(std::abs(std::stof(read(options, "rtx.vertexColorStrength")) - lowValue) < 1e-5,
      "API option layer re-read a cached file or failed to merge its strongest blend request");
    require((read(options, "rtx.enableCulling") == "True") == initialCulling,
      "non-float API option crossed its threshold too early");
    require(read(options, "rtx.nativeAdapter.vector.sky") == "0.45, 0.4, 0.35",
      "dynamic vector option did not blend through the original option resolver");
    options.blend(low, .5f, .4f);
    apply();
    require(read(options, "rtx.enableCulling") == culling, "boolean API option did not switch at its blend threshold");
    const auto high = options.acquire(highPath.c_str(), 4000, .5f);
    options.blend(high, .5f, .5f);
    apply();
    require(std::abs(std::stof(read(options, "rtx.vertexColorStrength")) - (lowValue + .9f) * .5f) < 1e-5,
      "API option layers were resolved in the wrong priority order");
    options.release(high);
    apply();
    require(std::abs(std::stof(read(options, "rtx.vertexColorStrength")) - lowValue) < 1e-5,
      "released high-priority option layer still influenced the renderer");
    options.release(low);
    options.release(duplicate);
    apply();
    require(std::abs(std::stof(read(options, "rtx.vertexColorStrength")) - lowValue) < 1e-5,
      "option layer was removed before its last acquired reference was released");
    options.release(cached);
    apply();
    require(std::abs(std::stof(read(options, "rtx.vertexColorStrength")) - base) < 1e-5,
      "last option-layer release did not restore the underlying value");
    expectError(api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, [&] { options.release(low); });
    const auto fresh = options.acquire(lowPath.c_str(), 3000, .5f);
    require(fresh != low, "released option-layer handle was reused");
    options.blend(fresh, 2, -1);
    apply();
    require(std::abs(std::stof(read(options, "rtx.vertexColorStrength")) - .99f) < 1e-5,
      "fresh API option layer did not reload the edited file or clamp blend inputs");
    options.release(fresh);
    const auto empty = options.acquire(emptyPath.c_str(), -100, 2);
    options.blend(empty, 1, .5f);
    apply();
    options.release(empty);
    expectError(api::REMIXAPI_ERROR_CODE_GENERAL_FAILURE, [&] { options.acquire("missing.conf", 3000, .5f); });
    expectError(api::REMIXAPI_ERROR_CODE_GENERAL_FAILURE, [&] { options.acquire(".", 3000, .5f); });
    expectError(api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS,
      [&] { options.acquire("missing.conf", std::numeric_limits<float>::infinity(), .5f); });

    options.set("rtx.showUI", "2");
    apply();
    require(read(options, "rtx.showUI") == "2", "API menu open request did not resolve");
    auto* pShow = RtxOptionImpl::getOptionByFullName("rtx.showUI");
    require(!pShow->hasValueInLayer(dxvk::RtxOptionLayer::getUserLayer()),
      "unsaved API menu control was written above the runtime's own menu controls");
    closeMenu();
    apply();
    require(read(options, "rtx.showUI") == "0", "API menu state masked the runtime's close action");

    std::atomic<bool> stop { false };
    std::atomic<bool> badRead { false };
    std::thread reader([&] {
      try {
        while (!stop.load()) {
          const auto value = read(options, "rtx.sceneScale");
          if (value != "2" && value != "3") {
            badRead = true;
          }
        }
      } catch (...) {
        badRead = true;
      }
    });
    for (uint32_t frame = 0; frame < 64; ++frame) {
      options.set("rtx.sceneScale", frame & 1 ? "2" : "3");
      apply();
    }
    stop = true;
    reader.join();
    require(!badRead, "concurrent API option read returned a torn resolved value");
    std::filesystem::remove(lowPath);
    std::filesystem::remove(highPath);
    std::filesystem::remove(emptyPath);
    std::cout << "PASS original API options, pending writes, UTF-8 buffers, typed blending, layer lifetime and menu controls\n";
    return 0;
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "FAIL " << error.message() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
  }
  return 1;
}
