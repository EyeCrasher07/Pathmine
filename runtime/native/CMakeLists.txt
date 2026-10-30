cmake_minimum_required(VERSION 3.20)
project(remix_native_platform LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "This entry point builds the Linux platform port. Windows uses the root Meson build.")
endif()

find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)
include(headers.cmake)

# Build the original runtime sources. This is not a replacement renderer or a
# Remix C API library; the remaining engine/dependency ports are separate work.
add_library(remix-vulkan-native STATIC
  ../src/vulkan/vulkan_loader.cpp
  ../src/vulkan/vulkan_names.cpp
  ../src/util/util_library.cpp)
set_target_properties(remix-vulkan-native PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(remix-vulkan-native PUBLIC cxx_std_17)
target_compile_options(remix-vulkan-native PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(remix-vulkan-native PUBLIC ../include/vulkan/include ../src)
target_link_libraries(remix-vulkan-native PUBLIC Vulkan::Vulkan Threads::Threads ${CMAKE_DL_LIBS})

add_library(remix-util-native STATIC
  ../src/util/util_env.cpp
  ../src/util/util_filesys.cpp
  ../src/util/util_directory_watch_linux.cpp
  ../src/util/util_mapped_file.cpp
  ../src/util/util_string.cpp
  ../src/util/config/config.cpp
  ../src/util/thread.cpp
  ../src/util/log/log.cpp)
set_target_properties(remix-util-native PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(remix-util-native PUBLIC remix-vulkan-native remix-native-headers)
target_include_directories(remix-util-native PRIVATE ../src/util)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  target_compile_options(remix-util-native PUBLIC -msse3)
endif()

add_library(remix-core-native STATIC
  ../src/dxvk/dxvk_extensions.cpp
  ../src/dxvk/dxvk_format.cpp
  ../src/dxvk/rtx_render/rtx_semaphore.cpp)
set_target_properties(remix-core-native PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(remix-core-native PUBLIC remix-util-native)

set(REMIX_NRD_SOURCE_DIR "" CACHE PATH "NRD 4.13 source directory")
set(REMIX_NRD_LIBRARY "" CACHE FILEPATH "The native NRD 4.13 library for integration tests")
if(REMIX_NRD_LIBRARY)
  if(NOT EXISTS "${REMIX_NRD_SOURCE_DIR}/Include/NRD.h" OR NOT EXISTS "${REMIX_NRD_LIBRARY}")
    message(FATAL_ERROR "The native NRD test requires the matching headers and ELF library")
  endif()
  add_library(remix-nrd-loader STATIC ../src/dxvk/rtx_render/rtx_nrd_library.cpp)
  set_target_properties(remix-nrd-loader PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_include_directories(remix-nrd-loader PUBLIC "${REMIX_NRD_SOURCE_DIR}/Include")
  target_link_libraries(remix-nrd-loader PUBLIC remix-vulkan-native)
  configure_file("${REMIX_NRD_LIBRARY}" "${CMAKE_CURRENT_BINARY_DIR}/libNRD.so" COPYONLY)
endif()

option(REMIX_BUILD_SHADERS "Compile the original RTX Remix shader passes using native tools" OFF)
if(REMIX_BUILD_SHADERS)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  find_program(REMIX_SLANGC slangc REQUIRED)
  find_program(REMIX_GLSLANG glslangValidator REQUIRED)
  find_program(REMIX_SPIRV_VAL spirv-val REQUIRED)
  set(REMIX_SLANG_LIBRARY "" CACHE FILEPATH "The shader compiler's native libslang.so")
  if(NOT EXISTS "${REMIX_SLANG_LIBRARY}")
    message(FATAL_ERROR "REMIX_SLANG_LIBRARY must name the native shader compiler library")
  endif()
  if(NOT EXISTS "${REMIX_NRD_SOURCE_DIR}/Shaders/Include/NRD.hlsli")
    message(FATAL_ERROR "REMIX_NRD_SOURCE_DIR must contain the matching NRD shader headers")
  endif()
  set(runtimeRoot "${CMAKE_CURRENT_SOURCE_DIR}/..")
  set(shaderRoot "${runtimeRoot}/src/dxvk/shaders")
  file(GLOB_RECURSE shaderSources CONFIGURE_DEPENDS "${shaderRoot}/*")
  file(GLOB_RECURSE shaderVendorHeaders CONFIGURE_DEPENDS
    "${runtimeRoot}/submodules/rtxdi/rtxdi-sdk/include/*"
    "${runtimeRoot}/submodules/rtxcr/shaders/include/*"
    "${runtimeRoot}/submodules/sharc/include/*"
    "${runtimeRoot}/submodules/nrc/Include/*"
    "${REMIX_NRD_SOURCE_DIR}/Shaders/Include/*")
  set(shaderOutput "${CMAKE_CURRENT_BINARY_DIR}/rtx_shaders")
  set(shaderCommand "${Python3_EXECUTABLE}" "${runtimeRoot}/scripts-common/compile_shaders.py"
      -glslang "${REMIX_GLSLANG}" -slangc "${REMIX_SLANGC}" -slang-library "${REMIX_SLANG_LIBRARY}"
      -spirvval "${REMIX_SPIRV_VAL}" -input "${shaderRoot}/rtx" -output "${shaderOutput}"
      -include "${shaderRoot}"
      -include "${runtimeRoot}/submodules/rtxdi/rtxdi-sdk/include"
      -include "${runtimeRoot}/submodules/rtxdi/rtxdi-sdk/include/volumetrics"
      -include "${runtimeRoot}/submodules/rtxcr/shaders/include"
      -include "${runtimeRoot}/submodules"
      -include "${REMIX_NRD_SOURCE_DIR}/Shaders/Include" -parallel)
  execute_process(COMMAND ${shaderCommand} -list-outputs
    OUTPUT_VARIABLE shaderByproducts OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
  string(REPLACE "\n" ";" shaderByproducts "${shaderByproducts}")
  set(shaderEntrypoints ${shaderSources})
  list(FILTER shaderEntrypoints INCLUDE REGEX "\\.(slang|comp|vert|geom|frag|rgen|rchit|rahit|rmiss|rint)$")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${shaderEntrypoints} "${runtimeRoot}/scripts-common/compile_shaders.py")
  add_custom_command(OUTPUT "${shaderOutput}/native-build.stamp"
    BYPRODUCTS ${shaderByproducts}
    COMMAND ${shaderCommand} -force
    COMMAND "${CMAKE_COMMAND}" -E touch "${shaderOutput}/native-build.stamp"
    DEPENDS ${shaderSources} ${shaderVendorHeaders} "${runtimeRoot}/scripts-common/compile_shaders.py"
      "${runtimeRoot}/scripts-common/depfile.py" "${runtimeRoot}/scripts-common/shader_xxd.py"
      "${REMIX_GLSLANG}" "${REMIX_SLANGC}" "${REMIX_SLANG_LIBRARY}" VERBATIM)
  add_custom_target(remix-rtx-shaders DEPENDS "${shaderOutput}/native-build.stamp")
  add_custom_command(OUTPUT "${shaderOutput}/native-validation.stamp"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/validate_shaders.py"
      --validator "${REMIX_SPIRV_VAL}" --input "${shaderOutput}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${shaderOutput}/native-validation.stamp"
    DEPENDS "${shaderOutput}/native-build.stamp" "${CMAKE_CURRENT_SOURCE_DIR}/validate_shaders.py"
      "${REMIX_SPIRV_VAL}" VERBATIM)
  add_custom_target(remix-validate-rtx-shaders DEPENDS "${shaderOutput}/native-validation.stamp")
endif()

include(runtime_components.cmake)

include(CTest)
if(BUILD_TESTING)
  find_package(Python3 QUIET COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME native-runtime-distribution-checker
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_check_runtime.py")
    add_test(NAME native-shader-dependencies
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_shader_dependencies.py")
  endif()
  if(TARGET remix-api016-headers)
    add_executable(native-api016-header-test api016_header_test.cpp)
    target_link_libraries(native-api016-header-test PRIVATE remix-api016-headers)
    add_test(NAME native-api016-header COMMAND native-api016-header-test)
  endif()
  add_executable(native-api-header-test api_header_test.c)
  target_compile_features(native-api-header-test PRIVATE c_std_11)
  target_include_directories(native-api-header-test PRIVATE ../public/include)
  add_test(NAME native-api-header COMMAND native-api-header-test)
  add_executable(native-vulkan-loader-test vulkan_loader_test.cpp)
  target_link_libraries(native-vulkan-loader-test PRIVATE remix-vulkan-native)
  add_test(NAME native-vulkan-loader COMMAND native-vulkan-loader-test)
  set_tests_properties(native-vulkan-loader PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
  add_executable(native-shared-library-test shared_library_test.cpp)
  target_link_libraries(native-shared-library-test PRIVATE remix-vulkan-native)
  add_test(NAME native-shared-library COMMAND native-shared-library-test)
  add_executable(native-platform-services-test platform_services_test.cpp)
  target_link_libraries(native-platform-services-test PRIVATE remix-util-native)
  add_test(NAME native-platform-services COMMAND native-platform-services-test)
  add_executable(native-numeric-config-test numeric_config_test.cpp)
  target_link_libraries(native-numeric-config-test PRIVATE remix-util-native)
  add_test(NAME native-numeric-config COMMAND native-numeric-config-test)
  add_executable(native-mapped-file-test mapped_file_test.cpp)
  target_link_libraries(native-mapped-file-test PRIVATE remix-util-native)
  add_test(NAME native-mapped-file COMMAND native-mapped-file-test)
  add_executable(native-directory-watch-test directory_watch_test.cpp)
  target_link_libraries(native-directory-watch-test PRIVATE remix-util-native)
  add_test(NAME native-directory-watch COMMAND native-directory-watch-test)
  set_tests_properties(native-directory-watch PROPERTIES TIMEOUT 15)
  add_executable(native-keybind-test keybind_test.cpp)
  target_link_libraries(native-keybind-test PRIVATE remix-native-headers)
  target_include_directories(native-keybind-test PRIVATE ../src)
  target_compile_features(native-keybind-test PRIVATE cxx_std_17)
  add_test(NAME native-keybind COMMAND native-keybind-test)
  add_executable(native-math-test math_test.cpp)
  target_link_libraries(native-math-test PRIVATE remix-util-native)
  add_test(NAME native-math COMMAND native-math-test)
  add_executable(native-semaphore-test semaphore_test.cpp)
  target_link_libraries(native-semaphore-test PRIVATE remix-core-native)
  add_test(NAME native-semaphore COMMAND native-semaphore-test)
  set_tests_properties(native-semaphore PROPERTIES TIMEOUT 45 SKIP_RETURN_CODE 77)
  if(TARGET remix-nrd-loader)
    add_executable(native-nrd-library-test nrd_library_test.cpp)
    target_link_libraries(native-nrd-library-test PRIVATE remix-nrd-loader)
    target_include_directories(native-nrd-library-test PRIVATE ../include)
    add_test(NAME native-nrd-library COMMAND native-nrd-library-test)
  endif()
  if(TARGET usd_m)
    add_executable(native-usd-dependency-test usd_dependency_test.cpp)
    target_compile_features(native-usd-dependency-test PRIVATE cxx_std_17)
    target_link_libraries(native-usd-dependency-test PRIVATE usd_m)
    add_test(NAME native-usd-dependency COMMAND native-usd-dependency-test)
    set_tests_properties(native-usd-dependency PROPERTIES TIMEOUT 30)
  endif()
  if(TARGET remix-rtx-components)
    add_executable(native-gpu-profile-test gpu_profile_test.cpp
      ../src/dxvk/rtx_render/rtx_gpu_profile.cpp)
    target_link_libraries(native-gpu-profile-test PRIVATE remix-runtime-settings)
    add_test(NAME native-gpu-profile COMMAND native-gpu-profile-test)
    set_tests_properties(native-gpu-profile PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
  endif()
  if(TARGET remix-original-runtime)
    add_executable(native-fastops-test fastops_test.cpp)
    target_link_libraries(native-fastops-test PRIVATE remix-runtime-support)
    add_test(NAME native-fastops COMMAND native-fastops-test)
    add_executable(native-original-device-test original_device_test.cpp image_sharing_test.cpp)
    target_link_libraries(native-original-device-test PRIVATE remix-original-runtime remix-runtime-settings)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/smoke")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/smoke/rtx.conf"
      "rtx.initializer.asyncShaderPrewarming = False\nrtx.asyncAssetLoading = False\n")
    add_test(NAME native-original-device COMMAND native-original-device-test)
    set_tests_properties(native-original-device PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
      FAIL_REGULAR_EXPRESSION "common device objects were not disposed of;VUID-;SYNC-HAZARD-;hazard detected;which doesn't match the VkImageView"
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/smoke"
      ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/smoke/config;RTX_ASYNC_SHADER_PREWARMING=0")
    add_executable(native-original-frame-test original_frame_test.cpp)
    target_link_libraries(native-original-frame-test PRIVATE remix-original-runtime remix-runtime-settings)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/frame-smoke")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/frame-smoke/rtx.conf"
      "rtx.initializer.asyncShaderPrewarming = False\nrtx.asyncAssetLoading = False\nrtx.shader.enableAsyncCompilation = False\nrtx.upscalerType = 0\nrtx.enableRayReconstruction = False\n")
    add_test(NAME native-original-frame COMMAND native-original-frame-test)
    set_tests_properties(native-original-frame PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77
      FAIL_REGULAR_EXPRESSION "common device objects were not disposed of;VUID-;SYNC-HAZARD-;hazard detected;which doesn't match the VkImageView"
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/frame-smoke"
      ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/frame-smoke/config;RTX_ASYNC_SHADER_PREWARMING=0;RTX_ENABLE_ASYNC_COMPILATION=0")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/frame-dlss-smoke")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/frame-dlss-smoke/rtx.conf"
      "rtx.initializer.asyncShaderPrewarming = False\nrtx.asyncAssetLoading = False\nrtx.shader.enableAsyncCompilation = False\nrtx.graphicsPreset = 4\nrtx.dlssPreset = 2\nrtx.upscalerType = 1\nrtx.enableRayReconstruction = False\n")
    add_test(NAME native-original-frame-dlss COMMAND native-original-frame-test --dlss)
    set_tests_properties(native-original-frame-dlss PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77
      FAIL_REGULAR_EXPRESSION "common device objects were not disposed of;VUID-;SYNC-HAZARD-;hazard detected;which doesn't match the VkImageView"
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/frame-dlss-smoke"
      ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/frame-dlss-smoke/config;RTX_ASYNC_SHADER_PREWARMING=0;RTX_ENABLE_ASYNC_COMPILATION=0")
    add_executable(native-usd-plugins-test usd_plugins_test.cpp)
    target_compile_features(native-usd-plugins-test PRIVATE cxx_std_17)
    target_link_libraries(native-usd-plugins-test PRIVATE usd_m)
    add_dependencies(native-usd-plugins-test native-RemixCategories native-RemixParticleSystem)
    add_test(NAME native-usd-plugins COMMAND native-usd-plugins-test "${nativeUsdPluginsDir}/")
    set_tests_properties(native-usd-plugins PROPERTIES TIMEOUT 30)
    find_package(glfw3 CONFIG REQUIRED)
    find_package(OpenGL REQUIRED)
    target_link_libraries(native-original-frame-test PRIVATE remix-native-output glfw OpenGL::GL)
    add_executable(native-original-output-test original_output_test.cpp)
    target_link_libraries(native-original-output-test PRIVATE remix-native-output glfw OpenGL::GL)
    add_test(NAME native-original-output COMMAND native-original-output-test)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/output-smoke")
    configure_file("${CMAKE_CURRENT_BINARY_DIR}/smoke/rtx.conf"
      "${CMAKE_CURRENT_BINARY_DIR}/output-smoke/rtx.conf" COPYONLY)
    set_tests_properties(native-original-output PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
      FAIL_REGULAR_EXPRESSION "common device objects were not disposed of;VUID-;SYNC-HAZARD-;hazard detected;which doesn't match the VkImageView"
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/output-smoke"
      ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/output-smoke/config;RTX_ASYNC_SHADER_PREWARMING=0")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/frame-gl-smoke")
    configure_file("${CMAKE_CURRENT_BINARY_DIR}/frame-smoke/rtx.conf"
      "${CMAKE_CURRENT_BINARY_DIR}/frame-gl-smoke/rtx.conf" COPYONLY)
    add_test(NAME native-original-frame-gl COMMAND native-original-frame-test --gl-output)
    set_tests_properties(native-original-frame-gl PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77
      FAIL_REGULAR_EXPRESSION "common device objects were not disposed of;VUID-;SYNC-HAZARD-;hazard detected;which doesn't match the VkImageView"
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/frame-gl-smoke"
      ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/frame-gl-smoke/config;RTX_ASYNC_SHADER_PREWARMING=0;RTX_ENABLE_ASYNC_COMPILATION=0")
    add_executable(native-runtime-host-test runtime_host_test.cpp)
    target_link_libraries(native-runtime-host-test PRIVATE remix-native-host glfw OpenGL::GL)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/host-smoke")
    configure_file("${CMAKE_CURRENT_BINARY_DIR}/frame-smoke/rtx.conf"
      "${CMAKE_CURRENT_BINARY_DIR}/host-smoke/rtx.conf" COPYONLY)
    add_test(NAME native-runtime-host COMMAND native-runtime-host-test)
    set_tests_properties(native-runtime-host PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77
      FAIL_REGULAR_EXPRESSION "common device objects were not disposed of;VUID-;SYNC-HAZARD-;hazard detected;which doesn't match the VkImageView"
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/host-smoke"
      ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/host-smoke/config;RTX_ASYNC_SHADER_PREWARMING=0;RTX_ENABLE_ASYNC_COMPILATION=0")
    if(TARGET remix-api016-resources)
      add_executable(native-ngx-startup-test ngx_startup_test.cpp)
      target_link_libraries(native-ngx-startup-test PRIVATE remix-api016-resources)
      foreach(mode IN ITEMS shutdown none dlss)
        set(startupDirectory "${CMAKE_CURRENT_BINARY_DIR}/ngx-${mode}-smoke")
        file(MAKE_DIRECTORY "${startupDirectory}")
        if(mode STREQUAL "dlss")
          configure_file("${CMAKE_CURRENT_BINARY_DIR}/frame-dlss-smoke/rtx.conf"
            "${startupDirectory}/rtx.conf" COPYONLY)
        else()
          configure_file("${CMAKE_CURRENT_BINARY_DIR}/frame-smoke/rtx.conf"
            "${startupDirectory}/rtx.conf" COPYONLY)
          file(APPEND "${startupDirectory}/rtx.conf"
            "rtx.graphicsPreset = 4\nrtx.dlssPreset = 2\n")
        endif()
        add_test(NAME native-ngx-startup-${mode} COMMAND native-ngx-startup-test --${mode})
        set_tests_properties(native-ngx-startup-${mode} PROPERTIES TIMEOUT 180
          FAIL_REGULAR_EXPRESSION "common device objects were not disposed of;VUID-;SYNC-HAZARD-;hazard detected"
          WORKING_DIRECTORY "${startupDirectory}"
          ENVIRONMENT "XDG_CONFIG_HOME=${startupDirectory}/config;RTX_ASYNC_SHADER_PREWARMING=0;RTX_ENABLE_ASYNC_COMPILATION=0")
      endforeach()
      add_executable(native-runtime-api-test runtime_api_test.cpp)
      target_link_libraries(native-runtime-api-test PRIVATE remix-api016-headers remix-runtime-settings glfw OpenGL::GL ${CMAKE_DL_LIBS})
      add_dependencies(native-runtime-api-test remix-api016-runtime)
      file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/api016-runtime-smoke")
      configure_file("${CMAKE_CURRENT_BINARY_DIR}/frame-smoke/rtx.conf"
        "${CMAKE_CURRENT_BINARY_DIR}/api016-runtime-smoke/rtx.conf" COPYONLY)
      add_test(NAME native-runtime-api COMMAND native-runtime-api-test "$<TARGET_FILE:remix-api016-runtime>")
      set_tests_properties(native-runtime-api PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77
        FAIL_REGULAR_EXPRESSION "common device objects were not disposed of;VUID-;SYNC-HAZARD-;hazard detected;which doesn't match the VkImageView;writes to Layer"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/api016-runtime-smoke"
        ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/api016-runtime-smoke/config;RTX_ASYNC_SHADER_PREWARMING=0;RTX_ENABLE_ASYNC_COMPILATION=0")
      add_executable(native-api016-lights-test api016_lights_test.cpp)
      target_link_libraries(native-api016-lights-test PRIVATE remix-api016-resources glfw OpenGL::GL)
      file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/api016-lights-smoke")
      configure_file("${CMAKE_CURRENT_BINARY_DIR}/frame-smoke/rtx.conf"
        "${CMAKE_CURRENT_BINARY_DIR}/api016-lights-smoke/rtx.conf" COPYONLY)
      add_test(NAME native-api016-lights COMMAND native-api016-lights-test)
      set_tests_properties(native-api016-lights PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77
        FAIL_REGULAR_EXPRESSION "common device objects were not disposed of;VUID-;SYNC-HAZARD-;hazard detected;which doesn't match the VkImageView;writes to Layer"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/api016-lights-smoke"
        ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/api016-lights-smoke/config;RTX_ASYNC_SHADER_PREWARMING=0;RTX_ENABLE_ASYNC_COMPILATION=0")
      add_executable(native-api016-instances-test api016_instances_test.cpp)
      target_link_libraries(native-api016-instances-test PRIVATE remix-api016-resources glfw OpenGL::GL)
      file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/api016-instances-smoke")
      configure_file("${CMAKE_CURRENT_BINARY_DIR}/frame-smoke/rtx.conf"
        "${CMAKE_CURRENT_BINARY_DIR}/api016-instances-smoke/rtx.conf" COPYONLY)
      add_test(NAME native-api016-instances COMMAND native-api016-instances-test)
      set_tests_properties(native-api016-instances PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77
        FAIL_REGULAR_EXPRESSION "common device objects were not disposed of;VUID-;SYNC-HAZARD-;hazard detected;which doesn't match the VkImageView;writes to Layer"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/api016-instances-smoke"
        ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/api016-instances-smoke/config;RTX_ASYNC_SHADER_PREWARMING=0;RTX_ENABLE_ASYNC_COMPILATION=0")
      add_executable(native-api016-meshes-test api016_meshes_test.cpp)
      target_link_libraries(native-api016-meshes-test PRIVATE remix-api016-resources glfw OpenGL::GL)
      file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/api016-meshes-smoke")
      configure_file("${CMAKE_CURRENT_BINARY_DIR}/frame-smoke/rtx.conf"
        "${CMAKE_CURRENT_BINARY_DIR}/api016-meshes-smoke/rtx.conf" COPYONLY)
      add_test(NAME native-api016-meshes COMMAND native-api016-meshes-test)
      set_tests_properties(native-api016-meshes PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77
        FAIL_REGULAR_EXPRESSION "common device objects were not disposed of;VUID-;SYNC-HAZARD-;hazard detected;which doesn't match the VkImageView;writes to Layer"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/api016-meshes-smoke"
        ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/api016-meshes-smoke/config;RTX_ASYNC_SHADER_PREWARMING=0;RTX_ENABLE_ASYNC_COMPILATION=0")
      add_executable(native-api016-options-test api016_options_test.cpp)
      target_link_libraries(native-api016-options-test PRIVATE remix-api016-resources)
      file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/api016-options-smoke")
      configure_file("${CMAKE_CURRENT_BINARY_DIR}/frame-smoke/rtx.conf"
        "${CMAKE_CURRENT_BINARY_DIR}/api016-options-smoke/rtx.conf" COPYONLY)
      add_test(NAME native-api016-options COMMAND native-api016-options-test)
      set_tests_properties(native-api016-options PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77
        FAIL_REGULAR_EXPRESSION "common device objects were not disposed of;VUID-;SYNC-HAZARD-;hazard detected"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/api016-options-smoke"
        ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/api016-options-smoke/config;RTX_ASYNC_SHADER_PREWARMING=0;RTX_ENABLE_ASYNC_COMPILATION=0")
      add_executable(native-api016-cameras-test api016_cameras_test.cpp)
      target_link_libraries(native-api016-cameras-test PRIVATE remix-api016-resources glfw OpenGL::GL)
      file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/api016-cameras-smoke")
      configure_file("${CMAKE_CURRENT_BINARY_DIR}/frame-smoke/rtx.conf"
        "${CMAKE_CURRENT_BINARY_DIR}/api016-cameras-smoke/rtx.conf" COPYONLY)
      file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/api016-cameras-smoke/rtx.conf"
        "rtx.enableNearPlaneOverride = True\n")
      add_test(NAME native-api016-cameras COMMAND native-api016-cameras-test)
      set_tests_properties(native-api016-cameras PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77
        FAIL_REGULAR_EXPRESSION "common device objects were not disposed of;VUID-;SYNC-HAZARD-;hazard detected;which doesn't match the VkImageView;writes to Layer"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/api016-cameras-smoke"
        ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/api016-cameras-smoke/config;RTX_ASYNC_SHADER_PREWARMING=0;RTX_ENABLE_ASYNC_COMPILATION=0")
      add_executable(native-api016-materials-test api016_materials_test.cpp)
      target_link_libraries(native-api016-materials-test PRIVATE remix-api016-resources glfw OpenGL::GL)
      file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/api016-materials-smoke")
      configure_file("${CMAKE_CURRENT_BINARY_DIR}/frame-smoke/rtx.conf"
        "${CMAKE_CURRENT_BINARY_DIR}/api016-materials-smoke/rtx.conf" COPYONLY)
      # Material color checks use fixed exposure and their own emissive sources.
      file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/api016-materials-smoke/rtx.conf"
        "rtx.autoExposure.enabled = False\nrtx.fallbackLightMode = 0\n")
      add_test(NAME native-api016-materials COMMAND native-api016-materials-test)
      set_tests_properties(native-api016-materials PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77
        FAIL_REGULAR_EXPRESSION "common device objects were not disposed of;VUID-;SYNC-HAZARD-;hazard detected;which doesn't match the VkImageView;writes to Layer"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/api016-materials-smoke"
        ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/api016-materials-smoke/config;RTX_ASYNC_SHADER_PREWARMING=0;RTX_ENABLE_ASYNC_COMPILATION=0")
      add_executable(native-api016-textures-test api016_textures_test.cpp)
      target_link_libraries(native-api016-textures-test PRIVATE remix-api016-resources glfw OpenGL::GL)
      file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/api016-textures-smoke")
      configure_file("${CMAKE_CURRENT_BINARY_DIR}/frame-smoke/rtx.conf"
        "${CMAKE_CURRENT_BINARY_DIR}/api016-textures-smoke/rtx.conf" COPYONLY)
      add_test(NAME native-api016-textures COMMAND native-api016-textures-test)
      set_tests_properties(native-api016-textures PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77
        FAIL_REGULAR_EXPRESSION "common device objects were not disposed of;VUID-;SYNC-HAZARD-;hazard detected;which doesn't match the VkImageView;writes to Layer"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/api016-textures-smoke"
        ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/api016-textures-smoke/config;RTX_ASYNC_SHADER_PREWARMING=0;RTX_ENABLE_ASYNC_COMPILATION=0")
    endif()
  endif()
endif()
