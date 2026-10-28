#pragma once

#include <chrono>
#include <cstdint>

// NV-DXVK start: Native calibrated clock
#ifdef __linux__
#include <cerrno>
#include <system_error>
#include <time.h>
#endif
// NV-DXVK end

#if defined(_WIN32) && !defined(__WINE__)
#include <windows.h>
#endif

namespace dxvk {

// NV-DXVK start: Native calibrated clock
#if (defined(_WIN32) && !defined(__WINE__)) || defined(__linux__)
// NV-DXVK end
  struct high_resolution_clock {
    static constexpr bool is_steady = true;

    using rep        = int64_t;
    using period     = std::nano;
    using duration   = std::chrono::nanoseconds;
    using time_point = std::chrono::time_point<high_resolution_clock>;

    static inline time_point now() noexcept {
      // Keep the frequency static, this doesn't change at all.
      static const int64_t freq = getFrequency();
      const int64_t counter     = getCounter();

      const int64_t whole = (counter / freq) * period::den;
      const int64_t part  = (counter % freq) * period::den / freq;

      return time_point(duration(whole + part));
    }

    static inline int64_t getFrequency() {
      // NV-DXVK start: Native nanosecond clock units
#ifdef __linux__
      return 1'000'000'000;
#else
      LARGE_INTEGER freq;
      QueryPerformanceFrequency(&freq);

      return freq.QuadPart;
#endif
      // NV-DXVK end
    }

    static inline int64_t getCounter() {
      // NV-DXVK start: Match Vulkan's CLOCK_MONOTONIC time domain
#ifdef __linux__
      timespec count = {};
      if (::clock_gettime(CLOCK_MONOTONIC, &count) != 0) {
        throw std::system_error(errno, std::generic_category(), "Read monotonic clock");
      }
      return static_cast<int64_t>(count.tv_sec) * 1'000'000'000 + count.tv_nsec;
#else
      LARGE_INTEGER count;
      QueryPerformanceCounter(&count);

      return count.QuadPart;
#endif
      // NV-DXVK end
    }
  };
#else
  using high_resolution_clock = std::chrono::high_resolution_clock;
#endif

}
