option(REMIX_BUILD_RUNTIME_COMPONENTS "Compile the original renderer components being ported to Linux" OFF)
if(NOT REMIX_BUILD_RUNTIME_COMPONENTS)
  return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "The original runtime uses MSVC aggregate extensions. Use clang++ for these components.")
endif()

find_package(pxr CONFIG REQUIRED)
string(JSON nvmlRevision GET "${nativeDependencyLock}" sources nvmlHeaders revision)
string(JSON nvmlHash GET "${nativeDependencyLock}" sources nvmlHeaders archiveSha256)
FetchContent_Declare(nvml_headers
  URL "https://codeload.github.com/NVIDIA/go-nvml/tar.gz/${nvmlRevision}"
  URL_HASH "SHA256=${nvmlHash}"
  SOURCE_SUBDIR native-headers-only)
FetchContent_MakeAvailable(nvml_headers)
include(glsl_shaders.cmake)
if(NOT EXISTS "${REMIX_NRD_SOURCE_DIR}/Include/NRD.h")
  message(FATAL_ERROR "The runtime components require NRD 4.13 headers")
endif()

add_library(remix-runtime-components OBJECT
  ../src/dxvk/dxvk_adapter.cpp
  ../src/dxvk/dxvk_barrier.cpp
  ../src/dxvk/dxvk_buffer.cpp
  ../src/dxvk/dxvk_cmdlist.cpp
  ../src/dxvk/dxvk_compute.cpp
  ../src/dxvk/dxvk_context.cpp
  ../src/dxvk/dxvk_cs.cpp
  ../src/dxvk/dxvk_data.cpp
  ../src/dxvk/dxvk_descriptor.cpp
  ../src/dxvk/dxvk_device.cpp
  ../src/dxvk/dxvk_device_filter.cpp
  ../src/dxvk/dxvk_framebuffer.cpp
  ../src/dxvk/dxvk_gpu_event.cpp
  ../src/dxvk/dxvk_gpu_query.cpp
  ../src/dxvk/dxvk_graphics.cpp
  ../src/dxvk/dxvk_image.cpp
  ../src/dxvk/dxvk_instance.cpp
  ../src/dxvk/dxvk_lifetime.cpp
  ../src/dxvk/dxvk_memory.cpp
  ../src/dxvk/dxvk_memory_tracker.cpp
  ../src/dxvk/dxvk_meta_blit.cpp
  ../src/dxvk/dxvk_meta_clear.cpp
  ../src/dxvk/dxvk_meta_copy.cpp
  ../src/dxvk/dxvk_meta_mipgen.cpp
  ../src/dxvk/dxvk_meta_pack.cpp
  ../src/dxvk/dxvk_meta_resolve.cpp
  ../src/dxvk/dxvk_options.cpp
  ../src/dxvk/dxvk_pipecache.cpp
  ../src/dxvk/dxvk_pipelayout.cpp
  ../src/dxvk/dxvk_pipemanager.cpp
  ../src/dxvk/dxvk_queue.cpp
  ../src/dxvk/dxvk_raytracing.cpp
  ../src/dxvk/dxvk_renderpass.cpp
  ../src/dxvk/dxvk_resource.cpp
  ../src/dxvk/dxvk_sampler.cpp
  ../src/dxvk/dxvk_scoped_annotation.cpp
  ../src/dxvk/dxvk_shader.cpp
  ../src/dxvk/dxvk_shader_key.cpp
  ../src/dxvk/dxvk_signal.cpp
  ../src/dxvk/dxvk_spec_const.cpp
  ../src/dxvk/dxvk_stats.cpp
  ../src/dxvk/dxvk_staging.cpp
  ../src/dxvk/dxvk_state_cache.cpp
  ../src/dxvk/dxvk_unbound.cpp
  ../src/dxvk/dxvk_util.cpp
  ../src/dxvk/rtx_render/rtx_semaphore_device.cpp)
set_target_properties(remix-runtime-components PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(remix-runtime-settings INTERFACE)
target_compile_features(remix-runtime-settings INTERFACE cxx_std_17)
target_compile_options(remix-runtime-settings INTERFACE -fms-extensions -msse3)
target_compile_definitions(remix-runtime-settings INTERFACE VK_ENABLE_BETA_EXTENSIONS REMIX_SENTRY_ENABLED=0)
target_include_directories(remix-runtime-settings INTERFACE
  ../include
  ../src/dxvk
  ../src/dxvk/rtx_render
  ../src/dxvk/shaders
  ../src/tracy
  ../submodules/nrc/Include
  ../submodules/dlss/include
  ../submodules/nvapi
  ../submodules
  ../submodules/rtxdi/rtxdi-sdk/include
  ../submodules/rtxdi/rtxdi-sdk/include/volumetrics
  ../submodules/rtxcr/shaders/include
  ../external/ngx_sdk_dlfg/include
  ../external/ngx_sdk_dlnr/include
  ../public/include
  "${legacyShaderOutput}"
  "${CMAKE_CURRENT_BINARY_DIR}"
  "${nvml_headers_SOURCE_DIR}/pkg/nvml"
  "${REMIX_NRD_SOURCE_DIR}/Include")
target_link_libraries(remix-runtime-settings INTERFACE remix-core-native usd_m)
target_link_libraries(remix-runtime-components PUBLIC remix-runtime-settings)
add_dependencies(remix-runtime-components remix-glsl-shaders)

set(VCS_TAG "remix-main+bbd74a1.native-linux-dirty")
configure_file(../version.h.in "${CMAKE_CURRENT_BINARY_DIR}/version.h" @ONLY)

option(REMIX_BUILD_RTX_COMPONENTS "Compile the original RTX render passes and scene system" OFF)
if(REMIX_BUILD_RTX_COMPONENTS)
  if(NOT TARGET remix-rtx-shaders OR NOT TARGET remix-nrd-loader)
    message(FATAL_ERROR "RTX components require REMIX_BUILD_SHADERS and the matching native NRD library")
  endif()
  file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../src/dxvk/meson.build" rtxSourceEntries
    REGEX "^  'rtx_render/.*\\.cpp',")
  set(rtxSources)
  foreach(entry IN LISTS rtxSourceEntries)
    string(REGEX REPLACE "^  '([^']+)'.*$" "\\1" source "${entry}")
    if(source MATCHES "rtx_render/rtx_(semaphore(_device)?|nrd_library)\\.cpp$")
      continue()
    endif()
    if(source MATCHES "rtx_render/rtx_(overlay_window|remix_api)\\.cpp$")
      # The Win32 overlay and D3D9 API host are platform entry points.
      # The native API host is integrated separately from these renderer objects.
      continue()
    endif()
    list(APPEND rtxSources "${CMAKE_CURRENT_LIST_DIR}/../src/dxvk/${source}")
  endforeach()
  add_library(remix-rtx-components OBJECT ${rtxSources})
  set_target_properties(remix-rtx-components PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_link_libraries(remix-rtx-components PUBLIC remix-runtime-settings remix-nrd-loader)
  target_precompile_headers(remix-rtx-components PRIVATE ../src/dxvk/pch/dxvk.pch.h)
  add_dependencies(remix-rtx-components remix-validate-rtx-shaders)
endif()

include(runtime_support.cmake)
