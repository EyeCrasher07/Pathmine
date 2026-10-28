// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include <utility>

#include <fcntl.h>
#include <unistd.h>

namespace dxvk::util {
  class ScopedFd {
  public:
    explicit ScopedFd(int fd = -1) : m_fd(fd) { }
    ~ScopedFd() { reset(); }

    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;
    ScopedFd(ScopedFd&& other) noexcept : m_fd(other.release()) { }
    ScopedFd& operator=(ScopedFd&& other) noexcept {
      if (this != &other) {
        reset(other.release());
      }
      return *this;
    }

    int get() const { return m_fd; }
    int release() { return std::exchange(m_fd, -1); }
    void reset(int fd = -1) {
      if (m_fd != fd) {
        if (m_fd >= 0) {
          ::close(m_fd);
        }
        m_fd = fd;
      }
    }

    static ScopedFd duplicate(int borrowedFd) {
      return ScopedFd(::fcntl(borrowedFd, F_DUPFD_CLOEXEC, 3));
    }

  private:
    int m_fd;
  };
}
