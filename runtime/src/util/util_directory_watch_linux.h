// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <unordered_map>
#include <vector>

namespace dxvk::util {
  struct DirectoryChanges {
    bool overflow = false;
    bool rescan = false;
    std::vector<std::filesystem::path> paths;
  };

  class LinuxDirectoryWatch {
  public:
    explicit LinuxDirectoryWatch(const std::filesystem::path& root);
    ~LinuxDirectoryWatch();

    LinuxDirectoryWatch(const LinuxDirectoryWatch&) = delete;
    LinuxDirectoryWatch& operator=(const LinuxDirectoryWatch&) = delete;

    DirectoryChanges poll(int timeoutMs);

  private:
    int m_fd = -1;
    std::filesystem::path m_root;
    std::unordered_map<int, std::filesystem::path> m_paths;

    void addSubtree(const std::filesystem::path& path);
    void removeSubtree(const std::filesystem::path& path);
  };
}
