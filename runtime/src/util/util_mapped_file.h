// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace dxvk::util {
  class MappedFile {
  public:
    explicit MappedFile(const std::filesystem::path& path);
    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    const uint8_t* data() const { return static_cast<const uint8_t*>(m_data); }
    size_t size() const { return m_size; }

  private:
    void* m_data = nullptr;
    size_t m_size = 0;
#ifdef _WIN32
    void* m_file = nullptr;
    void* m_mapping = nullptr;
#else
    int m_fd = -1;
#endif
  };
}
