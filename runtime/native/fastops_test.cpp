// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

#include "util/util_fastops.h"

namespace {
  template<typename T>
  void checkVectors(std::mt19937& random) {
    const T sentinel = std::numeric_limits<T>::max();
    for (uint32_t count = 1; count <= 257; count++) {
      std::vector<T> input(count);
      std::generate(input.begin(), input.end(), [&] { return static_cast<T>(random()); });
      for (bool ignoreSentinel : { false, true }) {
        input[0] = 11;
        for (uint32_t i = 7; i < count; i += 7) {
          input[i] = sentinel;
        }
        uint32_t expectedMin = input[0];
        uint32_t expectedMax = input[0];
        for (auto value : input) {
          if (!ignoreSentinel || value != sentinel) {
            expectedMin = std::min(expectedMin, static_cast<uint32_t>(value));
            expectedMax = std::max(expectedMax, static_cast<uint32_t>(value));
          }
        }
        uint32_t actualMin = 0, actualMax = 0;
        fast::findMinMax(count, input.data(), actualMin, actualMax, ignoreSentinel, sentinel);
        if (actualMin != expectedMin || actualMax != expectedMax) {
          throw std::runtime_error("original SIMD min/max differs from scalar result at count " + std::to_string(count));
        }
        std::vector<T> output(count);
        const T subtract = 3;
        fast::copySubtract(output.data(), input.data(), count, subtract, ignoreSentinel, sentinel);
        for (uint32_t i = 0; i < count; i++) {
          const T expected = input[i] - (ignoreSentinel && input[i] == sentinel ? 0 : subtract);
          if (output[i] != expected) {
            throw std::runtime_error("original SIMD subtraction differs from scalar result");
          }
        }
      }
    }
  }
}

int main() {
  try {
    std::mt19937 random(0x2060);
    checkVectors<uint16_t>(random);
    checkVectors<uint32_t>(random);
    for (uint32_t value : { 1u, 0x80000000u, 0x13579BDFu, 0xFFFFFFFFu }) {
      uint32_t ordinal = 0;
      for (uint32_t bit = 0; bit < 32; bit++) {
        if ((value & (1u << bit)) && fast::findNthBit(value, ordinal++) != bit) {
          throw std::runtime_error("original bit selection differs from scalar result");
        }
      }
    }
    std::vector<unsigned char> source(1024 * 1024 + 17), destination(source.size());
    std::generate(source.begin(), source.end(), [&] { return static_cast<unsigned char>(random()); });
    fast::parallel_memcpy(destination.data(), source.data(), source.size());
    if (source != destination) {
      throw std::runtime_error("native parallel copy lost bytes");
    }
    std::cout << "PASS original SIMD vector operations, bit selection and native parallel copy, SIMD level "
              << fast::getSimdSupportLevel() << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
