// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <array>
#include <cerrno>
#include <system_error>
#include <unordered_set>

#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "util_directory_watch_linux.h"

namespace dxvk::util {
  namespace {
    constexpr uint32_t kWatchMask = IN_CLOSE_WRITE | IN_ATTRIB | IN_CREATE | IN_DELETE
      | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF;
  }

  LinuxDirectoryWatch::LinuxDirectoryWatch(const std::filesystem::path& root)
    : m_root(root) {
    m_fd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_fd < 0) {
      throw std::system_error(errno, std::generic_category(), "Create directory watch");
    }
    try {
      addSubtree(m_root);
    } catch (...) {
      ::close(m_fd);
      throw;
    }
  }

  LinuxDirectoryWatch::~LinuxDirectoryWatch() {
    if (m_fd >= 0) {
      ::close(m_fd);
    }
  }

  void LinuxDirectoryWatch::addSubtree(const std::filesystem::path& path) {
    auto add = [&](const std::filesystem::path& directory) {
      const int id = ::inotify_add_watch(m_fd, directory.c_str(), kWatchMask);
      if (id < 0) {
        if (errno == ENOENT || errno == EACCES) {
          return;
        }
        throw std::system_error(errno, std::generic_category(), "Watch directory subtree");
      }
      m_paths[id] = directory;
    };
    add(path);
    std::error_code error;
    auto iterator = std::filesystem::recursive_directory_iterator(path,
      std::filesystem::directory_options::skip_permission_denied, error);
    const auto end = std::filesystem::recursive_directory_iterator();
    for (; !error && iterator != end; iterator.increment(error)) {
      if (iterator->is_directory(error)) {
        add(iterator->path());
      }
    }
    if (path == m_root && m_paths.empty()) {
      throw std::system_error(std::make_error_code(std::errc::no_such_file_or_directory), "Watch root directory");
    }
  }

  void LinuxDirectoryWatch::removeSubtree(const std::filesystem::path& path) {
    const auto prefix = path.native() + '/';
    for (auto iterator = m_paths.begin(); iterator != m_paths.end();) {
      const auto& candidate = iterator->second.native();
      if (candidate == path.native() || candidate.compare(0, prefix.size(), prefix) == 0) {
        ::inotify_rm_watch(m_fd, iterator->first);
        iterator = m_paths.erase(iterator);
      } else {
        ++iterator;
      }
    }
  }

  DirectoryChanges LinuxDirectoryWatch::poll(int timeoutMs) {
    DirectoryChanges changes;
    if (m_paths.empty()) {
      std::error_code error;
      if (std::filesystem::is_directory(m_root, error)) {
        addSubtree(m_root);
        changes.rescan = true;
      }
    }
    pollfd descriptor{m_fd, POLLIN, 0};
    const int ready = ::poll(&descriptor, 1, timeoutMs);
    if (ready < 0) {
      if (errno == EINTR) {
        return changes;
      }
      throw std::system_error(errno, std::generic_category(), "Poll directory changes");
    }
    if (!ready) {
      return changes;
    }
    if (descriptor.revents & (POLLERR | POLLNVAL | POLLHUP)) {
      throw std::system_error(std::make_error_code(std::errc::io_error), "Directory watch became unavailable");
    }

    std::unordered_set<std::filesystem::path> seen;
    alignas(inotify_event) std::array<char, 64 * 1024> buffer;
    for (;;) {
      const auto bytes = ::read(m_fd, buffer.data(), buffer.size());
      if (bytes < 0) {
        if (errno == EINTR) {
          continue;
        }
        if (errno == EAGAIN) {
          break;
        }
        throw std::system_error(errno, std::generic_category(), "Read directory changes");
      }
      if (!bytes) {
        break;
      }
      for (size_t offset = 0; offset + sizeof(inotify_event) <= static_cast<size_t>(bytes);) {
        const auto* pEvent = reinterpret_cast<const inotify_event*>(buffer.data() + offset);
        const auto entrySize = sizeof(inotify_event) + pEvent->len;
        if (entrySize > static_cast<size_t>(bytes) - offset) {
          throw std::system_error(std::make_error_code(std::errc::protocol_error), "Truncated directory event");
        }
        offset += entrySize;
        if (pEvent->mask & IN_Q_OVERFLOW) {
          changes.overflow = true;
          addSubtree(m_root);
          continue;
        }
        const auto found = m_paths.find(pEvent->wd);
        if (found == m_paths.end()) {
          continue;
        }
        const auto directory = found->second;
        if (pEvent->mask & IN_IGNORED) {
          m_paths.erase(pEvent->wd);
          continue;
        }
        const auto path = pEvent->len ? directory / pEvent->name : directory;
        if (seen.insert(path).second) {
          changes.paths.push_back(path);
        }
        if ((pEvent->mask & IN_ISDIR) && (pEvent->mask & (IN_CREATE | IN_MOVED_TO))) {
          addSubtree(path);
          changes.rescan = true;
        }
        if ((pEvent->mask & IN_ISDIR) && (pEvent->mask & (IN_DELETE | IN_MOVED_FROM))) {
          removeSubtree(path);
        }
        if ((pEvent->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) && directory == m_root) {
          removeSubtree(m_root);
        }
      }
    }
    return changes;
  }
}
