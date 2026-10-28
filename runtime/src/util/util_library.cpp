// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "util_library.h"

namespace dxvk::util {

  SharedLibrary::SharedLibrary(const std::filesystem::path& path) {
#ifdef _WIN32
    m_module = reinterpret_cast<void*>(LoadLibraryW(path.c_str()));
    if (!m_module) {
      m_error = "LoadLibraryW failed: " + std::to_string(GetLastError());
    }
#else
    m_module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!m_module) {
      const char* pError = dlerror();
      m_error = pError ? pError : "dlopen failed";
    }
#endif
  }

  SharedLibrary::~SharedLibrary() {
    if (!m_module) {
      return;
    }
#ifdef _WIN32
    FreeLibrary(reinterpret_cast<HMODULE>(m_module));
#else
    dlclose(m_module);
#endif
  }

  void* SharedLibrary::symbol(const char* pName) const {
    if (!m_module || !pName) {
      return nullptr;
    }
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(m_module), pName));
#else
    return dlsym(m_module, pName);
#endif
  }

  std::filesystem::path SharedLibrary::moduleDirectory(const void* pAddress) {
    if (!pAddress) {
      return {};
    }
#ifdef _WIN32
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(pAddress), &module)) {
      return {};
    }
    std::vector<wchar_t> path(512);
    while (path.size() <= 32768) {
      const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
      if (!length) {
        return {};
      }
      if (length < path.size()) {
        return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
      }
      path.resize(path.size() * 2);
    }
    return {};
#else
    Dl_info info{};
    if (!dladdr(pAddress, &info) || !info.dli_fname) {
      return {};
    }
    std::error_code error;
    const auto path = std::filesystem::absolute(info.dli_fname, error);
    return error ? std::filesystem::path{} : path.parent_path();
#endif
  }
}
