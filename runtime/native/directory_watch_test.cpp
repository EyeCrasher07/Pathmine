#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include <unistd.h>

#include "util/util_directory_watch_linux.h"

namespace {
  size_t fdCount() {
    return std::distance(std::filesystem::directory_iterator("/proc/self/fd"),
      std::filesystem::directory_iterator());
  }

  void write(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "updated texture";
    if (!output) {
      throw std::runtime_error("texture fixture write failed");
    }
  }

  void expectPath(dxvk::util::LinuxDirectoryWatch& watch, const std::filesystem::path& path) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
      const auto changes = watch.poll(50);
      if (std::find(changes.paths.begin(), changes.paths.end(), path) != changes.paths.end()) {
        return;
      }
    }
    throw std::runtime_error("missing native file event: " + path.string());
  }
}

int main() {
  const auto root = std::filesystem::temp_directory_path()
    / ("remix-watch-" + std::to_string(::getpid()));
  try {
    std::filesystem::create_directories(root / "существующий");
    const auto initial = root / "существующий" / "вода.dds";
    write(initial);
    const auto baseline = fdCount();
    {
      dxvk::util::LinuxDirectoryWatch watch(root);
      write(initial);
      expectPath(watch, initial);

      const auto added = root / "новый";
      std::filesystem::create_directory(added);
      expectPath(watch, added);
      const auto texture = added / "normal.dds";
      write(texture);
      expectPath(watch, texture);

      const auto staging = added / "temp.dds";
      write(staging);
      std::filesystem::rename(staging, texture);
      expectPath(watch, texture);

      const auto renamed = root / "renamed";
      std::filesystem::rename(added, renamed);
      expectPath(watch, renamed);
      write(renamed / "normal.dds");
      expectPath(watch, renamed / "normal.dds");

      std::filesystem::remove_all(root);
      std::filesystem::create_directory(root);
      bool rescanned = false;
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
      while (!rescanned && std::chrono::steady_clock::now() < deadline) {
        rescanned = watch.poll(50).rescan;
      }
      if (!rescanned) {
        throw std::runtime_error("replacement of the watched root did not rearm its watches");
      }
      write(root / "recovered.dds");
      expectPath(watch, root / "recovered.dds");
    }
    for (uint32_t i = 0; i < 32; ++i) {
      dxvk::util::LinuxDirectoryWatch watch(root);
    }
    if (fdCount() != baseline) {
      throw std::runtime_error("native directory watch lifetime leaked FDs");
    }
    std::filesystem::remove_all(root);
    std::cout << "PASS inotify texture updates, UTF-8 names, new/renamed subtrees, replaced root and FD cleanup\n";
    return 0;
  } catch (const std::exception& error) {
    std::filesystem::remove_all(root);
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
