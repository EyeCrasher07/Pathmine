/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#include <numeric>

// NV-DXVK start: Native platform service dependencies
#include <atomic>
#include <cerrno>
#include <cstring>
// NV-DXVK end

#include "util_env.h"
// NV-DXVK start: get environment variable with a fallback
#include "util_string.h"
// NV-DXVK end

// NV-DXVK start: Native platform environment services
#ifdef _WIN32
#include "./com/com_include.h"
#include <shellapi.h>

#include <Windows.h>
#include <stdio.h>
#include <tlhelp32.h>
#include <Psapi.h>
#include <Shlwapi.h>
#else
#include <cstdlib>
#include <fstream>
#include <link.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "util_library.h"

extern char** environ;
#endif
// NV-DXVK end
// NV-DXVK start
#include <set>
#include <filesystem>
// NV-DXVK end
#include "../tracy/TracyC.h"

namespace dxvk::env {
  const char* kRenderingServerExeName = "NvRemixBridge.exe";

  std::string getEnvVar(const char* name) {
    // NV-DXVK start: Native environment variables
#ifdef _WIN32
    std::vector<WCHAR> result;
    result.resize(MAX_PATH + 1);

    DWORD len = ::GetEnvironmentVariableW(str::tows(name).c_str(), result.data(), MAX_PATH);
    result.resize(len);

    return str::fromws(result.data());
#else
    const char* pValue = std::getenv(name);
    return pValue ? pValue : "";
#endif
    // NV-DXVK end
  }

  bool setEnvVar(const char* name, const char* value) {
    // NV-DXVK start: Native environment variables
#ifdef _WIN32
    const auto result = ::SetEnvironmentVariableW(str::tows(name).c_str(), str::tows(value).c_str());

    return result != 0;
#else
    return value ? ::setenv(name, value, 1) == 0 : ::unsetenv(name) == 0;
#endif
    // NV-DXVK end
  }
  
  // NV-DXVK start: Windows bridge process discovery
#ifdef _WIN32
  DWORD getParentPID() {
    const DWORD pid = GetCurrentProcessId();
    HANDLE h = NULL;
    PROCESSENTRY32 pe = { 0 };
    DWORD ppid = 0;
    pe.dwSize = sizeof(PROCESSENTRY32);
    h = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (::Process32First(h, &pe)) {
      do {
        if (pe.th32ProcessID == pid) {
          ppid = pe.th32ParentProcessID;
          break;
        }
      } while (::Process32Next(h, &pe));
    }
    ::CloseHandle(h);
    return (ppid);
  }

  std::string getProcessName(DWORD pid) {
    HANDLE h = ::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (h) {
      std::vector<WCHAR> exePath;
      exePath.resize(MAX_PATH + 1);

      DWORD len = ::GetModuleFileNameExW(h, NULL, exePath.data(), MAX_PATH);
      exePath.resize(len);

      ::CloseHandle(h);

      return str::fromws(exePath.data());
    }

    return "";
  }
#endif
  // NV-DXVK end


  size_t matchFileExtension(const std::string& name, const char* ext) {
    auto pos = name.find_last_of('.');

    if (pos == std::string::npos)
      return pos;

    bool matches = std::accumulate(name.begin() + pos + 1, name.end(), true,
      [&ext] (bool current, char a) {
        if (a >= 'A' && a <= 'Z')
          a += 'a' - 'A';
        return current && *ext && a == *(ext++);
      });

    return matches ? pos : std::string::npos;
  }


  std::string getExeName() {
    std::string fullPath = getExePath();

    // NV-DXVK start: Native path separator
    auto n = fullPath.find_last_of(std::filesystem::path::preferred_separator);
    // NV-DXVK end

    std::string currentProcessName = (n != std::string::npos) ? fullPath.substr(n + 1) : fullPath;

    return currentProcessName;
  }

  std::string getExeNameNoSuffix() {
    std::string exeName = env::getExeName();
    auto extp = exeName.find_last_of('.');

    if (extp != std::string::npos && exeName.substr(extp + 1) == "exe")
      exeName.erase(extp);
    return exeName;
  }

  // NV-DXVK start
  bool isRemixBridgeActive() {
#ifdef _WIN32
    enum IsRemixBridgeActiveStatus {
      Unchecked = 0,
      Active,
      Inactive
    };
    
    static std::atomic<IsRemixBridgeActiveStatus> bridgeStatus = IsRemixBridgeActiveStatus::Unchecked;

    // Don't expect this to change in the process life-cycle, cache and read to avoid unnecessary string ops
    if(bridgeStatus == IsRemixBridgeActiveStatus::Unchecked) {
      std::vector<WCHAR> exePath;
      exePath.resize(MAX_PATH + 1);

      DWORD len = ::GetModuleFileNameW(NULL, exePath.data(), MAX_PATH);
      exePath.resize(len);

      std::string fullPath = str::fromws(exePath.data());
      
      if (std::strstr(fullPath.c_str(), kRenderingServerExeName) != nullptr) {
        bridgeStatus = IsRemixBridgeActiveStatus::Active;
      } else {
        bridgeStatus = IsRemixBridgeActiveStatus::Inactive;
      }
    }

    return (bridgeStatus == IsRemixBridgeActiveStatus::Active);
#else
    return false;
#endif
  }
  // NV-DXVK end

  std::string getExeBaseName() {
    auto exeName = getExeName();
    auto extp = matchFileExtension(exeName, "exe");

    if (extp != std::string::npos)
      exeName.erase(extp);

    return exeName;
  }


  std::string getExePath() {
    // NV-DXVK start: Native executable discovery
#ifdef _WIN32
    std::vector<WCHAR> exePath;
    exePath.resize(MAX_PATH + 1);

    DWORD len = ::GetModuleFileNameW(NULL, exePath.data(), MAX_PATH);
    exePath.resize(len);

    std::string fullPath = str::fromws(exePath.data());

    // If this process was launched from the bridge, we should look at our parent process name
    if (isRemixBridgeActive()) {
      fullPath = getProcessName(getParentPID());
    }

    return fullPath;
#else
    std::vector<char> path(256);
    for (;;) {
      const auto length = ::readlink("/proc/self/exe", path.data(), path.size());
      if (length < 0) {
        return "";
      }
      if (static_cast<size_t>(length) < path.size()) {
        return std::string(path.data(), length);
      }
      path.resize(path.size() * 2);
    }
#endif
    // NV-DXVK end
  }
  
  // NV-DXVK start
  std::string dedupeFilename(const std::string& originalFilePath) {
    const auto ogPath = std::filesystem::path(originalFilePath);
    const auto parentDirPath = ogPath.parent_path();
    auto filePathMinusExtension = parentDirPath;
    filePathMinusExtension /= ogPath.stem();
    
    // Enumerate all files that match the input file, even those with "__#" suffix
    std::set<std::filesystem::path> matchingFilePaths;
    for (const auto& dir_entry : std::filesystem::directory_iterator{parentDirPath}) {
      if (dir_entry.path().string().find(filePathMinusExtension.string()) != std::string::npos) {
        matchingFilePaths.insert(dir_entry.path());
      }
    }

    // Process matching file names
    const std::string suffixPrefix("__");
    if (matchingFilePaths.empty()) {
      // Quick path out
      return originalFilePath;
    } else {
      // Figure out what the highest dupe name created is, so we can +1 it
      long highestDupeNum = 0;
      const auto commonDeduped = filePathMinusExtension.string() + suffixPrefix;
      for (const auto& matchingFilePath : matchingFilePaths) {
        const auto dupeNumPos = matchingFilePath.string().find(commonDeduped);
        if (dupeNumPos == 0) {
          const auto potentialSuffix = matchingFilePath.string().substr(commonDeduped.length());
          // stol is convenient and throws if the string is not numerical
          try {
            const auto dupeNumFromName = std::stol(potentialSuffix);
            highestDupeNum = std::max(dupeNumFromName, highestDupeNum);
          } catch (...) {}
        }
      }

      // Form new file name
      const auto dupeNum = highestDupeNum + 1;
      const auto suffixStr = suffixPrefix + std::to_string(dupeNum);
      auto newFileNameStr = filePathMinusExtension.string();
      newFileNameStr.replace(newFileNameStr.length(), suffixStr.length(), suffixStr);
      newFileNameStr += ogPath.extension().string();
      auto newPath = ogPath;
      newPath.replace_filename(newFileNameStr);
      return newPath.string();
    }
  }
  
  std::string getModulePath(const char* module) {
#ifdef _WIN32
    std::vector<WCHAR> modulePath;
    modulePath.resize(MAX_PATH + 1);

    HMODULE hModule = GetModuleHandle(module);
    GetModuleFileNameW(hModule, modulePath.data(), MAX_PATH);
    PathRemoveFileSpecW(modulePath.data());

    std::string path = str::fromws(modulePath.data());
    return path;
#else
    struct Search {
      const char* pModule;
      std::string directory;
    } search{module, {}};
    ::dl_iterate_phdr([](dl_phdr_info* pInfo, size_t, void* pData) {
      auto& search = *static_cast<Search*>(pData);
      const auto path = std::filesystem::path(pInfo->dlpi_name);
      if (path.filename() == search.pModule || path == search.pModule) {
        search.directory = std::filesystem::absolute(path).parent_path().string();
        return 1;
      }
      return 0;
    }, &search);
    return search.directory;
#endif
  }

  bool getAvailableSystemPhysicalMemory(uint64_t& availableSize) {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    BOOL res = GlobalMemoryStatusEx(&memInfo);
    if (res) {
      availableSize = memInfo.ullAvailPhys;
    }
    return res != 0;
#else
    std::ifstream memoryInfo("/proc/meminfo");
    std::string key;
    uint64_t value = 0;
    std::string unit;
    while (memoryInfo >> key >> value >> unit) {
      if (key == "MemAvailable:" && unit == "kB") {
        availableSize = value * 1024;
        return true;
      }
    }
    return false;
#endif
  }

  std::string getDllDirectory() {
#ifdef _WIN32
    HMODULE hModule;
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR) &getDllDirectory, &hModule);

    wchar_t modulePath[MAX_PATH];
    GetModuleFileNameW(hModule, modulePath, MAX_PATH);
    PathRemoveFileSpecW(modulePath);

    std::filesystem::path path(modulePath);
    return path.string();
#else
    return util::SharedLibrary::moduleDirectory(reinterpret_cast<const void*>(&getDllDirectory)).string();
#endif
  }

  bool isDebuggerPresent() {
#ifdef _WIN32
    return ::IsDebuggerPresent() != 0;
#else
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
      if (line.compare(0, 10, "TracerPid:") == 0) {
        uint64_t tracer = 0;
        std::istringstream(line.substr(10)) >> tracer;
        return tracer != 0;
      }
    }
    return false;
#endif
  }
  // NV-DXVK end

  void setThreadName(const std::string& name) {
#ifdef TRACY_ENABLE
    TracyCSetThreadName(name.c_str());
#endif

    // NV-DXVK start: Native thread names
#ifdef _WIN32
    using SetThreadDescriptionProc = HRESULT (WINAPI *) (HANDLE, PCWSTR);

    static auto proc = reinterpret_cast<SetThreadDescriptionProc>(
      ::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription"));

    if (proc != nullptr) {
      auto wideName = std::vector<WCHAR>(name.length() + 1);
      str::tows(name.c_str(), wideName.data(), wideName.size());
      (*proc)(::GetCurrentThread(), wideName.data());
    }
#else
    ::pthread_setname_np(::pthread_self(), name.substr(0, 15).c_str());
#endif
    // NV-DXVK end
  }

  bool createDirectory(const std::string& path) {
    // NV-DXVK start: Native directory creation
#ifdef _WIN32
    WCHAR widePath[MAX_PATH];
    str::tows(path.c_str(), widePath);
    return !!CreateDirectoryW(widePath, nullptr);
#else
    std::error_code error;
    return std::filesystem::create_directory(path, error);
#endif
    // NV-DXVK end
  }

  // NV-DXVK start: Add exit code parameter so the bridge can distinguish GPU crashes from CPU crashes
  void killProcess(uint32_t exitCode) {
#ifdef _WIN32
    if (!TerminateProcess(GetCurrentProcess(), exitCode)) {
      ExitProcess(exitCode);
    }
#else
    ::_exit(exitCode);
#endif
  }
  // NV-DXVK end

  void openUrlInBrowser(const char* url) {
    // NV-DXVK start: Native desktop URL opener
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#else
    pid_t child = 0;
    char* arguments[] = {const_cast<char*>("xdg-open"), const_cast<char*>(url), nullptr};
    if (::posix_spawnp(&child, arguments[0], nullptr, nullptr, arguments, environ) == 0) {
      std::thread([child] {
        while (::waitpid(child, nullptr, 0) < 0 && errno == EINTR) { }
      }).detach();
    }
#endif
    // NV-DXVK end
  }
}
