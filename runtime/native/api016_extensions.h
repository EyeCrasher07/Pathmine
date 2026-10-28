// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstring>

#include "api016_error.h"

namespace dxvk::native {
  template<typename Visitor>
  void visitApiExtensions(const void* pNext, Visitor&& visitor) {
    struct Header {
      api016::remixapi_StructType sType;
      const void* pNext;
    };
    std::array<const void*, 32> visited { };
    size_t count = 0;
    while (pNext) {
      if (count == visited.size()) {
        throw ApiError(api016::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, "API extension chain is too long");
      }
      for (size_t index = 0; index < count; ++index) {
        if (visited[index] == pNext) {
          throw ApiError(api016::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, "Cyclic API extension chain");
        }
      }
      visited[count++] = pNext;
      Header header;
      std::memcpy(&header, pNext, sizeof(header));
      visitor(header.sType, pNext);
      pNext = header.pNext;
    }
  }
}
