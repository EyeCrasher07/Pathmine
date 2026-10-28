#include <cmath>
#include <iostream>
#include <stdexcept>

#include "util/util_matrix.h"

namespace dxvk {
  Logger Logger::s_instance("native-math-test.log", LogLevel::None);
}

int main() {
  try {
    const dxvk::Matrix4 rotation{
      dxvk::Vector4(0, 0, -1, 0), dxvk::Vector4(0, 1, 0, 0),
      dxvk::Vector4(1, 0, 0, 0), dxvk::Vector4(0, 0, 0, 1)};
    const dxvk::Matrix4 translation(dxvk::Vector3(10, 20, 30));
    auto scale = dxvk::Matrix4(2);
    scale[3][3] = 1;
    const auto world = translation * rotation * scale;
    const dxvk::Vector4 point(1, 2, 3, 1);
    if (world * point != dxvk::Vector4(16, 24, 28, 1)) {
      throw std::runtime_error("original transform composition changed on native x86_64");
    }
    const auto restored = dxvk::inverse(world) * (world * point);
    for (uint32_t i = 0; i < 4; ++i) {
      if (std::abs(restored[i] - point[i]) > 0.00001f) {
        throw std::runtime_error("original inverse transform round trip failed");
      }
    }
    if (!dxvk::isIdentityExact(dxvk::Matrix4()) || dxvk::isIdentityExact(translation)) {
      throw std::runtime_error("native identity transform detection changed");
    }
    static_assert(sizeof(dxvk::Matrix4) == 64);
    static_assert(sizeof(dxvk::Vector4) == 16);
    static_assert(sizeof(dxvk::Vector3) == 12);
    static_assert(dxvk::CACHE_LINE_SIZE == 64);
    std::cout << "PASS original Remix transform composition, inverse and CPU matrix layouts\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
