// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include <stdexcept>

#include "api016.h"

namespace dxvk::native {
  class ApiError : public std::runtime_error {
  public:
    ApiError(api016::remixapi_ErrorCode code, const char* pMessage)
      : std::runtime_error(pMessage), m_code(code) { }

    api016::remixapi_ErrorCode code() const { return m_code; }

  private:
    api016::remixapi_ErrorCode m_code;
  };
}
