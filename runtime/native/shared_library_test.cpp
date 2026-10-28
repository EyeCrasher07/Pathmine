#include <cmath>
#include <iostream>
#include <stdexcept>

#include "util/util_library.h"

int main() {
  try {
    using dxvk::util::SharedLibrary;
    SharedLibrary missing("/nonexistent/remix-library.so");
    if (missing || missing.error().empty() || missing.symbol("anything")) {
      throw std::runtime_error("Missing library did not report a failure");
    }
    SharedLibrary math("libm.so.6");
    if (!math) {
      throw std::runtime_error(math.error());
    }
    auto cosine = reinterpret_cast<double (*)(double)>(math.symbol("cos"));
    if (!cosine || std::abs(cosine(0.0) - 1.0) > 1e-12) {
      throw std::runtime_error("Native library symbol did not execute");
    }
    if (math.symbol("remix_deliberately_missing_symbol")) {
      throw std::runtime_error("Missing export resolved to a pointer");
    }
    const auto path = SharedLibrary::moduleDirectory(reinterpret_cast<const void*>(cosine));
    if (path.empty() || !std::filesystem::is_directory(path)) {
      throw std::runtime_error("Cannot locate a loaded native module");
    }
    std::cout << "PASS native library loading, exports, missing dependencies and module directory\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
