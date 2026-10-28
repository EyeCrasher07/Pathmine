#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <system_error>

#include <unistd.h>

#include "util/util_mapped_file.h"

namespace {
  size_t fdCount() {
    return std::distance(std::filesystem::directory_iterator("/proc/self/fd"),
      std::filesystem::directory_iterator());
  }

  void expectFailure(const std::filesystem::path& path) {
    try {
      dxvk::util::MappedFile mapping(path);
    } catch (const std::system_error&) {
      return;
    }
    throw std::runtime_error("an invalid texture source was mapped successfully");
  }
}

int main() {
  const auto directory = std::filesystem::temp_directory_path()
    / ("remix-mapping-" + std::to_string(::getpid()));
  try {
    std::filesystem::create_directory(directory);
    const auto texture = directory / "текстура.bin";
    std::array<uint8_t, 4096> bytes;
    for (size_t i = 0; i < bytes.size(); ++i) {
      bytes[i] = static_cast<uint8_t>(i % 251);
    }
    {
      std::ofstream output(texture, std::ios::binary);
      output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    const auto baseline = fdCount();
    for (uint32_t iteration = 0; iteration < 64; ++iteration) {
      dxvk::util::MappedFile mapping(texture);
      if (mapping.size() != bytes.size() || std::memcmp(mapping.data(), bytes.data(), bytes.size()) != 0) {
        throw std::runtime_error("mapped texture bytes differ from the original file");
      }
    }
    const auto empty = directory / "empty.dds";
    std::ofstream(empty).close();
    expectFailure(empty);
    expectFailure(directory / "missing.dds");
    if (fdCount() != baseline) {
      throw std::runtime_error("texture file mapping or error handling leaked FDs");
    }
    std::filesystem::remove_all(directory);
    std::cout << "PASS native texture file mapping, UTF-8 path, 64 lifetimes and failed-open cleanup\n";
    return 0;
  } catch (const std::exception& error) {
    std::filesystem::remove_all(directory);
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
