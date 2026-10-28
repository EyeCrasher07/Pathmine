include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
file(READ "${CMAKE_CURRENT_LIST_DIR}/dependencies.lock.json" nativeDependencyLock)
string(JSON dxvkNativeRevision GET "${nativeDependencyLock}" sources dxvkNative revision)
string(JSON dxvkNativeHash GET "${nativeDependencyLock}" sources dxvkNative archiveSha256)
string(JSON directxRevision GET "${nativeDependencyLock}" sources directxHeaders revision)
string(JSON directxHash GET "${nativeDependencyLock}" sources directxHeaders archiveSha256)

set(REMIX_DXVK_NATIVE_SOURCE_DIR "" CACHE PATH "DXVK Native source containing its portable Windows type headers")
set(REMIX_DIRECTX_HEADERS_DIR "" CACHE PATH "Portable DirectX headers matching the pinned DXVK Native source")

if(NOT REMIX_DXVK_NATIVE_SOURCE_DIR)
  FetchContent_Declare(dxvk_native_headers
    URL "https://codeload.github.com/doitsujin/dxvk/tar.gz/${dxvkNativeRevision}"
    URL_HASH "SHA256=${dxvkNativeHash}"
    SOURCE_SUBDIR native-headers-only)
  FetchContent_MakeAvailable(dxvk_native_headers)
  set(REMIX_DXVK_NATIVE_SOURCE_DIR "${dxvk_native_headers_SOURCE_DIR}")
endif()

if(NOT REMIX_DIRECTX_HEADERS_DIR)
  FetchContent_Declare(directx_headers
    URL "https://codeload.github.com/Joshua-Ashton/mingw-directx-headers/tar.gz/${directxRevision}"
    URL_HASH "SHA256=${directxHash}"
    SOURCE_SUBDIR native-headers-only)
  FetchContent_MakeAvailable(directx_headers)
  set(REMIX_DIRECTX_HEADERS_DIR "${directx_headers_SOURCE_DIR}")
endif()

if(NOT EXISTS "${REMIX_DXVK_NATIVE_SOURCE_DIR}/include/native/windows/windows.h"
   OR NOT EXISTS "${REMIX_DIRECTX_HEADERS_DIR}/d3d9.h")
  message(FATAL_ERROR "The Linux platform port requires the pinned DXVK Native and DirectX type headers")
endif()

add_library(remix-native-headers INTERFACE)
target_include_directories(remix-native-headers SYSTEM INTERFACE
  "${REMIX_DXVK_NATIVE_SOURCE_DIR}/include/native/windows"
  "${REMIX_DIRECTX_HEADERS_DIR}")

set(REMIX_MINECRAFT_API_HEADER "" CACHE FILEPATH "The immutable Minecraft API 0.16 header")
if(REMIX_MINECRAFT_API_HEADER)
  string(JSON minecraftApiHash GET "${nativeDependencyLock}" sources minecraftApi headerSha256)
  file(SHA256 "${REMIX_MINECRAFT_API_HEADER}" suppliedMinecraftApiHash)
  if(NOT suppliedMinecraftApiHash STREQUAL minecraftApiHash)
    message(FATAL_ERROR "The supplied Minecraft API header does not match the pinned 0.16 contract")
  endif()
  set(minecraftApiInclude "${CMAKE_CURRENT_BINARY_DIR}/api016/include")
  configure_file("${REMIX_MINECRAFT_API_HEADER}"
    "${minecraftApiInclude}/remix_api016/remix_c.h" COPYONLY)
  add_library(remix-api016-headers INTERFACE)
  target_include_directories(remix-api016-headers INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_LIST_DIR}/../public/include" "${minecraftApiInclude}")
  target_compile_features(remix-api016-headers INTERFACE cxx_std_17)
endif()
