#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>

#include "util/log/log.h"
#include "util/util_env.h"
#include "util/util_filesys.h"
#include "util/util_library.h"
#include "util/util_string.h"
#include "util/util_time.h"
#include "util/config/config.h"

namespace dxvk {
  Logger Logger::s_instance("native-platform-test.log", LogLevel::None);
}

namespace {
  void require(bool condition, const char* pMessage) {
    if (!condition) {
      throw std::runtime_error(pMessage);
    }
  }
}

int main() {
  const auto temporary = std::filesystem::temp_directory_path()
    / ("remix-platform-" + std::to_string(::getpid()));
  try {
    require(dxvk::env::setEnvVar("REMIX_NATIVE_TEST_VALUE", "Привет, RTX"), "setenv failed");
    require(dxvk::env::getEnvVar("REMIX_NATIVE_TEST_VALUE") == "Привет, RTX", "environment lost UTF-8");
    require(dxvk::env::setEnvVar("REMIX_NATIVE_TEST_VALUE", nullptr), "unsetenv failed");
    require(dxvk::env::getEnvVar("REMIX_NATIVE_TEST_VALUE").empty(), "environment value was not removed");
    require(dxvk::env::getEnvVar("REMIX_NATIVE_TEST_VALUE", true), "missing boolean did not keep its fallback");
    dxvk::env::setEnvVar("REMIX_NATIVE_TEST_VALUE", "false");
    require(!dxvk::env::getEnvVar("REMIX_NATIVE_TEST_VALUE", true), "boolean parsing changed");
    dxvk::env::setEnvVar("REMIX_NATIVE_TEST_VALUE", nullptr);
    const std::string utf8 = "Текстуры / 日本語 / RTX 🧱";
    require(dxvk::str::fromws(dxvk::str::tows(utf8.c_str()).c_str()) == utf8, "wide string conversion lost Unicode");
    require(dxvk::str::sanitizeUtf8(utf8) == utf8, "valid UTF-8 was changed");
    timespec before = {}, after = {};
    require(::clock_gettime(CLOCK_MONOTONIC, &before) == 0, "clock query failed");
    const auto ticks = dxvk::high_resolution_clock::getCounter();
    const auto now = dxvk::high_resolution_clock::now().time_since_epoch().count();
    require(::clock_gettime(CLOCK_MONOTONIC, &after) == 0, "clock query failed");
    const auto lower = before.tv_sec * 1'000'000'000ll + before.tv_nsec;
    const auto upper = after.tv_sec * 1'000'000'000ll + after.tv_nsec;
    require(dxvk::high_resolution_clock::getFrequency() == 1'000'000'000
            && lower <= ticks && ticks <= now && now <= upper,
            "runtime clock does not match Vulkan's monotonic time domain");

    const auto executable = std::filesystem::canonical("/proc/self/exe");
    require(dxvk::env::getExePath() == executable, "executable path is incorrect");
    require(dxvk::env::getExeName() == executable.filename(), "executable basename is incorrect");
    require(dxvk::env::getDllDirectory() == executable.parent_path(), "module directory is incorrect");
    require(!dxvk::env::isRemixBridgeActive(), "native process was mistaken for a Windows bridge");
    dxvk::util::SharedLibrary mathLibrary("libm.so.6");
    require(bool(mathLibrary), "system ELF library could not be opened");
    require(std::filesystem::exists(dxvk::env::getModulePath("libm.so.6") + "/libm.so.6"),
            "loaded module path is incorrect");
    require(dxvk::env::getModulePath("not-a-remix-module.so").empty(), "missing module resolved to another library");
    uint64_t available = 0;
    require(dxvk::env::getAvailableSystemPhysicalMemory(available) && available > 0,
            "available physical memory could not be read");

    dxvk::env::setThreadName("remix-native-rt");
    char threadName[16] = {};
    require(::pthread_getname_np(::pthread_self(), threadName, sizeof(threadName)) == 0,
            "thread name query failed");
    require(std::string(threadName) == "remix-native-rt", "native thread name was not applied");

    require(dxvk::env::createDirectory(temporary.string()), "temporary directory creation failed");
    const auto unicodeDirectory = temporary / "Текстуры";
    require(dxvk::util::createDirectories(unicodeDirectory), "UTF-8 directory creation failed");
    const auto outputPath = unicodeDirectory / "материал.txt";
    auto output = dxvk::util::createDirectoriesAndOpenFile(outputPath);
    require(output.has_value(), "UTF-8 output path could not be opened");
    *output << "material data";
    output.reset();
    require(std::filesystem::file_size(outputPath) == 13, "output file contents differ");
    dxvk::Config config;
    config.setOption("rtx.test", std::string("Привет"));
    config.setOption("rtx.enabled", true);
    const auto configPath = unicodeDirectory / "настройки.conf";
    dxvk::Config::serializeCustomConfig(config, configPath.string());
    const auto reloaded = dxvk::Config::getOptionLayerConfig(configPath.string());
    require(reloaded.getOption<std::string>("rtx.test") == "Привет"
            && reloaded.getOption<bool>("rtx.enabled"), "configuration round trip lost values");

    const auto logDirectory = temporary / "logs";
    require(dxvk::env::setEnvVar("DXVK_LOG_PATH", logDirectory.c_str()), "log path override failed");
    require(dxvk::env::setEnvVar("DEFAULT_MODS_DIR", (temporary / "mods").c_str()), "mod path override failed");
    require(dxvk::env::setEnvVar("DXVK_CAPTURE_PATH", (temporary / "captures").c_str()), "capture path override failed");
    dxvk::util::RtxFileSys::init(temporary.string());
    dxvk::Logger::initRtxLog();
    dxvk::Logger::info("Native Remix logging: Текстуры");
    std::ifstream log(logDirectory / "remix-dxvk.log");
    const std::string contents(std::istreambuf_iterator<char>(log), {});
    require(contents.find("Native Remix logging: Текстуры") != std::string::npos, "native log output is missing");

    const auto child = ::fork();
    require(child >= 0, "fork failed");
    if (child == 0) {
      dxvk::env::killProcess(37);
    }
    int status = 0;
    require(::waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 37,
            "native process exit code was lost");
    std::filesystem::remove_all(temporary);
    std::cout << "PASS original Remix platform services, UTF-8 paths and logs, real memory and process metadata\n";
    return 0;
  } catch (const std::exception& error) {
    std::filesystem::remove_all(temporary);
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
