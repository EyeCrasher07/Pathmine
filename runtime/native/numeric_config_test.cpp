// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <cerrno>
#include <cmath>
#include <iostream>
#include <locale.h>
#include <stdexcept>
#include <string>
#include <thread>

#include "util/config/config.h"
#include "util/log/log.h"

namespace dxvk {
  Logger Logger::s_instance("native-numeric-config-test.log", LogLevel::None);
}

namespace {
  void require(bool condition, const char* pMessage) {
    if (!condition) {
      throw std::runtime_error(pMessage);
    }
  }

  void checkValues() {
    const auto originalLocale = uselocale(nullptr);
    for (const auto& [text, expected] : {
        std::pair<const char*, float> { "0.01", .01f },
        { "0.21405728", .21405728f }, { "0.75", .75f },
        { "1.4", 1.4f }, { "-2.5", -2.5f },
        { "  +.35f;", .35f }, { "256.f;", 256.f }, { "1e-2", .01f } }) {
      float value = -99.f;
      errno = EDOM;
      require(dxvk::Config::parseOptionValue(text, value), "numeric configuration was rejected");
      require(value == expected, "decimal configuration value changed with the host locale");
      require(errno == EDOM, "numeric parsing changed the caller's errno");
      require(uselocale(nullptr) == originalLocale, "numeric parsing changed the caller's locale");
    }
    for (const char* pText : { "", "text", "1e999", "1e-999" }) {
      float value = 17.f;
      require(!dxvk::Config::parseOptionValue(pText, value), "invalid numeric configuration succeeded");
      require(value == 17.f, "invalid numeric configuration overwrote the fallback");
    }
    dxvk::Vector3 vector;
    require(dxvk::Config::parseOptionValue("0.6, 0.9, 0.89", vector), "vector configuration failed");
    require(vector.x == .6f && vector.y == .9f && vector.z == .89f,
      "vector configuration components changed with the host locale");
    dxvk::Config config;
    config.setOption("sceneScale", std::string("0.01"));
    config.setOption("exposure", .21405728f);
    require(config.getOption<float>("sceneScale", 1.f) == .01f, "scene scale did not retain its fraction");
    require(std::abs(config.getOption<float>("exposure") - .21405728f) < 1e-6f,
      "numeric configuration round trip failed");
  }

  void checkLocale(const char* pName) {
    locale_t locale = newlocale(LC_NUMERIC_MASK, pName, nullptr);
    require(locale != nullptr, "numeric locale is unavailable; generate ru_RU.UTF-8 for this test");
    const auto previous = uselocale(locale);
    try {
      checkValues();
    } catch (...) {
      uselocale(previous);
      freelocale(locale);
      throw;
    }
    uselocale(previous);
    freelocale(locale);
  }
}

int main() {
  try {
    checkLocale("C");
    checkLocale("ru_RU.UTF-8");
    std::exception_ptr workerError;
    std::thread worker([&] {
      try {
        checkLocale("ru_RU.UTF-8");
      } catch (...) {
        workerError = std::current_exception();
      }
    });
    checkLocale("C");
    worker.join();
    if (workerError) {
      std::rethrow_exception(workerError);
    }
    std::cout << "PASS decimal configuration and vectors across C and comma-decimal locales\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
