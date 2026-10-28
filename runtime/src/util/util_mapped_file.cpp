// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <limits>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "util_mapped_file.h"

namespace dxvk::util {
  MappedFile::MappedFile(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      throw std::system_error(::GetLastError(), std::system_category(), "Open mapped file");
    }
    LARGE_INTEGER size = {};
    if (!::GetFileSizeEx(file, &size)) {
      const auto error = ::GetLastError();
      ::CloseHandle(file);
      throw std::system_error(error, std::system_category(), "Get mapped file size");
    }
    if (size.QuadPart <= 0 || static_cast<uint64_t>(size.QuadPart) > std::numeric_limits<size_t>::max()) {
      ::CloseHandle(file);
      throw std::system_error(std::make_error_code(std::errc::invalid_argument), "Invalid mapped file size");
    }
    const auto mapping = ::CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping) {
      const auto error = ::GetLastError();
      ::CloseHandle(file);
      throw std::system_error(error, std::system_category(), "Create file mapping");
    }
    auto* pData = ::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!pData) {
      const auto error = ::GetLastError();
      ::CloseHandle(mapping);
      ::CloseHandle(file);
      throw std::system_error(error, std::system_category(), "Map file contents");
    }
    m_file = file;
    m_mapping = mapping;
    m_size = static_cast<size_t>(size.QuadPart);
#else
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
      throw std::system_error(errno, std::generic_category(), "Open mapped file");
    }
    struct stat status = {};
    if (::fstat(fd, &status) != 0) {
      const auto error = errno;
      ::close(fd);
      throw std::system_error(error, std::generic_category(), "Get mapped file size");
    }
    if (!S_ISREG(status.st_mode) || status.st_size <= 0
        || static_cast<uint64_t>(status.st_size) > std::numeric_limits<size_t>::max()) {
      ::close(fd);
      throw std::system_error(std::make_error_code(std::errc::invalid_argument), "Invalid mapped file size");
    }
    auto* pData = ::mmap(nullptr, status.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (pData == MAP_FAILED) {
      const auto error = errno;
      ::close(fd);
      throw std::system_error(error, std::generic_category(), "Map file contents");
    }
    m_fd = fd;
    m_size = static_cast<size_t>(status.st_size);
#endif
    m_data = pData;
  }

  MappedFile::~MappedFile() {
#ifdef _WIN32
    if (m_data) {
      ::UnmapViewOfFile(m_data);
    }
    if (m_mapping) {
      ::CloseHandle(m_mapping);
    }
    if (m_file) {
      ::CloseHandle(m_file);
    }
#else
    if (m_size) {
      ::munmap(m_data, m_size);
    }
    if (m_fd >= 0) {
      ::close(m_fd);
    }
#endif
  }
}
