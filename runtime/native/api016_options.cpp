// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <string>

#include "api016_error.h"
#include "api016_options.h"
#include "dxvk/rtx_render/rtx_option_manager.h"
#include "util/config/config.h"

namespace dxvk::native {
  namespace {
    namespace api = api016;

    [[noreturn]] void fail(api::remixapi_ErrorCode code, const char* pMessage) {
      throw ApiError(code, pMessage);
    }

    void requireInitialized(api::remixapi_ErrorCode code) {
      if (!RtxOptionImpl::isInitialized()) {
        fail(code, "The native runtime has not loaded its configuration");
      }
    }

    void requireResult(DynamicOptionResult result) {
      if (result != DynamicOptionResult::Success) {
        fail(result == DynamicOptionResult::InvalidArgument
          ? api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS : api::REMIXAPI_ERROR_CODE_GENERAL_FAILURE,
          "Unknown or invalid Remix option");
      }
    }

    void validateValue(OptionType type, const std::string& value) {
      GenericValue parsed = createGenericValue(type);
      bool valid = false;
      switch (type) {
      case OptionType::HashSet:
      case OptionType::HashVector: {
        std::vector<std::string> hashes;
        valid = Config::parseOptionValue(value, hashes);
        break;
      }
      case OptionType::VirtualKeys:
        valid = Config::parseOptionValue(value, *parsed.virtualKeys);
        break;
      default:
        valid = parseDynamicValue(type, value, parsed);
        break;
      }
      releaseGenericValue(parsed, type);
      if (!valid) {
        fail(api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, "Invalid Remix option value");
      }
    }

    void setStatic(RtxOptionImpl& option, const std::string& key, const std::string& value) {
      Config config;
      config.setOption(key, value);
      std::lock_guard<std::mutex> lock(RtxOptionImpl::getUpdateMutex());
      option.readOption(config, option.getTargetLayer(RtxOptionLayer::getUserLayer()));
    }
  }

  struct ApiOptions::Impl {
    struct Layer {
      RtxOptionLayer* pLayer;
      uint32_t references;
    };

    mutable std::mutex mutex;
    std::map<uintptr_t, Layer> layers;
    std::map<std::pair<uint32_t, std::string>, uintptr_t> cache;
    std::map<std::string, std::string> startupWrites;
    uintptr_t nextHandle = 1;

    Layer& find(api::remixapi_ConfigLayerHandle handle) {
      const auto found = layers.find(reinterpret_cast<uintptr_t>(handle));
      if (found == layers.end()) {
        fail(api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, "Unknown or released API option layer");
      }
      return found->second;
    }
  };

  ApiOptions::ApiOptions() : m_pImpl(std::make_unique<Impl>()) { }

  ApiOptions::~ApiOptions() {
    for (const auto& entry : m_pImpl->layers) {
      for (uint32_t count = 0; count < entry.second.references; ++count) {
        RtxOptionManager::releaseLayer(entry.second.pLayer);
      }
    }
  }

  void ApiOptions::set(const char* pKey, const char* pValue) {
    if (!pKey || !*pKey || !pValue) {
      fail(api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, "Missing Remix option key or value");
    }
    std::lock_guard<std::mutex> lock(m_pImpl->mutex);
    const std::string key(pKey);
    auto* pOption = RtxOptionImpl::getOptionByFullName(key);
    if (!pOption || pOption->isDynamic()) {
      if (RtxOptionImpl::isInitialized()) {
        requireResult(RtxOptionManager::setDynamicValue(key, pValue, RtxOptionLayer::getUserLayer()));
      } else {
        requireResult(RtxOptionManager::queueDynamicValue(key, pValue));
      }
      return;
    }
    validateValue(pOption->getType(), pValue);
    if (RtxOptionImpl::isInitialized()) {
      setStatic(*pOption, key, pValue);
    } else {
      m_pImpl->startupWrites[key] = pValue;
    }
  }

  void ApiOptions::applyStartupWrites() {
    std::lock_guard<std::mutex> lock(m_pImpl->mutex);
    requireInitialized(api::REMIXAPI_ERROR_CODE_NOT_INITIALIZED);
    for (const auto& entry : m_pImpl->startupWrites) {
      if (auto* pOption = RtxOptionImpl::getOptionByFullName(entry.first)) {
        setStatic(*pOption, entry.first, entry.second);
      }
    }
    m_pImpl->startupWrites.clear();
  }

  void ApiOptions::read(api016::remixapi_ConfigVariableResolvedInfo& info) const {
    if (info.sType != api::REMIXAPI_STRUCT_TYPE_CONFIG_VARIABLE_RESOLVED_INFO || !info.key || !*info.key) {
      fail(api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, "Invalid resolved Remix option request");
    }
    requireInitialized(api::REMIXAPI_ERROR_CODE_NOT_INITIALIZED);
    std::string value;
    requireResult(RtxOptionManager::readResolvedValue(info.key, value));
    if (value.size() >= std::numeric_limits<uint32_t>::max()) {
      fail(api::REMIXAPI_ERROR_CODE_GENERAL_FAILURE, "Resolved Remix option exceeds the API capacity");
    }
    const auto capacity = info.valueSize;
    info.valueSize = uint32_t(value.size() + 1);
    if (info.value) {
      if (capacity < info.valueSize) {
        fail(api::REMIXAPI_ERROR_CODE_BUFFER_TOO_SMALL, "Resolved Remix option requires a larger buffer");
      }
      std::memcpy(info.value, value.c_str(), info.valueSize);
    }
  }

  api016::remixapi_ConfigLayerHandle ApiOptions::acquire(const char* pPath, float priority, float threshold) {
    if (!pPath || !*pPath || !std::isfinite(priority) || !std::isfinite(threshold)) {
      fail(api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, "Invalid API option layer request");
    }
    std::lock_guard<std::mutex> lock(m_pImpl->mutex);
    requireInitialized(api::REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED);
    const auto clampedPriority = uint32_t(std::clamp(std::round(priority),
      float(kMinDynamicRtxOptionLayerPriority), float(kMaxDynamicRtxOptionLayerPriority)));
    auto key = std::make_pair(clampedPriority, std::string(pPath));
    const auto cached = m_pImpl->cache.find(key);
    if (cached != m_pImpl->cache.end()) {
      auto& layer = m_pImpl->layers.at(cached->second);
      if (layer.references == std::numeric_limits<uint32_t>::max()) {
        fail(api::REMIXAPI_ERROR_CODE_GENERAL_FAILURE, "API option layer reference count exhausted");
      }
      RtxOptionManager::acquireLayer(pPath, { key.first, key.second }, 0, std::clamp(threshold, 0.f, 1.f));
      ++layer.references;
      return reinterpret_cast<api::remixapi_ConfigLayerHandle>(cached->second);
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(std::filesystem::u8path(pPath), error) || !std::ifstream(pPath).good()) {
      fail(api::REMIXAPI_ERROR_CODE_GENERAL_FAILURE, "API option layer file is missing or unreadable");
    }
    if (m_pImpl->nextHandle == std::numeric_limits<uintptr_t>::max()) {
      fail(api::REMIXAPI_ERROR_CODE_GENERAL_FAILURE, "API option layer handle capacity exhausted");
    }
    const uintptr_t handle = m_pImpl->nextHandle++;
    auto* pLayer = RtxOptionManager::acquireLayer(pPath, { key.first, key.second }, 0, std::clamp(threshold, 0.f, 1.f));
    try {
      m_pImpl->layers.emplace(handle, Impl::Layer { pLayer, 1 });
      m_pImpl->cache.emplace(std::move(key), handle);
    } catch (...) {
      m_pImpl->layers.erase(handle);
      RtxOptionManager::releaseLayer(pLayer);
      throw;
    }
    return reinterpret_cast<api::remixapi_ConfigLayerHandle>(handle);
  }

  void ApiOptions::blend(api016::remixapi_ConfigLayerHandle handle, float strength, float threshold) {
    if (!std::isfinite(strength) || !std::isfinite(threshold)) {
      fail(api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, "Invalid API option layer blending");
    }
    std::lock_guard<std::mutex> lock(m_pImpl->mutex);
    auto& layer = m_pImpl->find(handle);
    std::lock_guard<std::mutex> optionLock(RtxOptionImpl::getUpdateMutex());
    layer.pLayer->requestBlendStrength(std::clamp(strength, 0.f, 1.f));
    layer.pLayer->requestBlendThreshold(std::clamp(threshold, 0.f, 1.f));
  }

  void ApiOptions::release(api016::remixapi_ConfigLayerHandle handle) {
    std::lock_guard<std::mutex> lock(m_pImpl->mutex);
    const uintptr_t id = reinterpret_cast<uintptr_t>(handle);
    auto& layer = m_pImpl->find(handle);
    const auto key = layer.pLayer->getLayerKey();
    const auto cacheKey = std::make_pair(key.priority, std::string(key.name));
    RtxOptionManager::releaseLayer(layer.pLayer);
    if (!--layer.references) {
      m_pImpl->cache.erase(cacheKey);
      m_pImpl->layers.erase(id);
    }
  }
}
