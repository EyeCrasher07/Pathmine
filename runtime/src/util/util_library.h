// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <string>

namespace dxvk::util {

  /** Owns an OS library reference. Symbols remain valid until its destruction. */
  class SharedLibrary {
  public:
    explicit SharedLibrary(const std::filesystem::path& path);
    ~SharedLibrary();

    SharedLibrary(const SharedLibrary&) = delete;
    SharedLibrary& operator=(const SharedLibrary&) = delete;

    explicit operator bool() const { return m_module != nullptr; }
    const std::string& error() const { return m_error; }
    void* symbol(const char* pName) const;

    static std::filesystem::path moduleDirectory(const void* pAddress);

  private:
    void* m_module = nullptr;
    std::string m_error;
  };
}
