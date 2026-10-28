option(REMIX_BUILD_RUNTIME_LIBRARY "Link the original runtime core with its native dependencies" OFF)
if(NOT REMIX_BUILD_RUNTIME_LIBRARY)
  return()
endif()
if(NOT TARGET remix-rtx-components)
  message(FATAL_ERROR "The runtime library requires the original RTX components")
endif()

find_program(REMIX_XXD xxd REQUIRED)
find_path(REMIX_GLFW_INCLUDE_DIR GLFW/glfw3.h REQUIRED)
string(JSON dlssNativeVersion GET "${nativeDependencyLock}" sources dlss nativeFeatureVersion)
foreach(feature IN ITEMS dlss dlssd)
  set(name "libnvidia-ngx-${feature}.so.${dlssNativeVersion}")
  configure_file("${CMAKE_CURRENT_LIST_DIR}/../submodules/dlss/lib/Linux_x86_64/rel/${name}"
    "${CMAKE_CURRENT_BINARY_DIR}/${name}" COPYONLY)
endforeach()
configure_file("${CMAKE_CURRENT_LIST_DIR}/../submodules/dlss/LICENSE.txt"
  "${CMAKE_CURRENT_BINARY_DIR}/LICENSE-NVIDIA-DLSS.txt" COPYONLY)
set(embeddedOutput "${CMAKE_CURRENT_BINARY_DIR}/embedded")
set(embeddedHeaders)
foreach(asset IN ITEMS
    fonts/NVIDIASans/NVIDIASansMd.ttf
    fonts/NVIDIASans/NVIDIASansBd.ttf
    fonts/RobotoMono/RobotoMonoRg.ttf
    omni_core_materials/Base/AperturePBR_Opacity.mdl
    omni_core_materials/Base/AperturePBR_Translucent.mdl
    omni_core_materials/Base/AperturePBR_Model.mdl
    omni_core_materials/Base/AperturePBR_Normal.mdl
    omni_core_materials/Base/AperturePBR_SpriteSheet.mdl)
  set(input "${CMAKE_CURRENT_LIST_DIR}/../external/${asset}")
  get_filename_component(name "${asset}" NAME)
  get_filename_component(stem "${asset}" NAME_WE)
  set(output "${embeddedOutput}/${name}.h")
  add_custom_command(OUTPUT "${output}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${embeddedOutput}"
    COMMAND "${REMIX_XXD}" -i -n "___${stem}" "${input}" "${output}"
    DEPENDS "${input}" VERBATIM)
  list(APPEND embeddedHeaders "${output}")
endforeach()
add_custom_target(remix-native-assets DEPENDS ${embeddedHeaders})

add_library(remix-runtime-support STATIC
  ../src/util/util_fastops.cpp
  ../src/util/util_flush.cpp
  ../src/util/util_global_time.cpp
  ../src/util/util_local_data.cpp
  ../src/util/util_matrix.cpp
  ../src/util/util_sentry.cpp
  ../src/util/log/log_debug.cpp
  ../src/util/log/metrics.cpp
  ../src/util/sha1/sha1.c
  ../src/util/sha1/sha1_util.cpp
  ../src/util/sync/sync_recursive.cpp
  ../src/util/xxHash/xxhash.c
  ../src/spirv/spirv_code_buffer.cpp
  ../src/spirv/spirv_compression.cpp
  ../src/spirv/spirv_module.cpp
  ../src/lssusd/game_exporter.cpp
  ../src/lssusd/usd_mesh_importer.cpp
  ../src/lssusd/hd/usd_mesh_util.cpp
  ../src/dxvk/imgui/dxvk_imgui.cpp
  ../src/dxvk/imgui/dxvk_imgui_about.cpp
  ../src/dxvk/imgui/dxvk_imgui_capture.cpp
  ../src/dxvk/imgui/dxvk_imgui_extensions.cpp
  ../src/dxvk/imgui/dxvk_imgui_first_use_guide.cpp
  ../src/dxvk/imgui/dxvk_imgui_splash.cpp
  ../src/dxvk/imgui/imgui.cpp
  ../src/dxvk/imgui/imgui_demo.cpp
  ../src/dxvk/imgui/imgui_draw.cpp
  ../src/dxvk/imgui/imgui_tables.cpp
  ../src/dxvk/imgui/imgui_widgets.cpp
  ../src/dxvk/imgui/implot.cpp
  ../src/dxvk/imgui/implot_demo.cpp
  ../src/dxvk/imgui/implot_items.cpp
  ../src/dxvk/imgui/rtx_user_menu.cpp)
set_target_properties(remix-runtime-support PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(remix-runtime-support PUBLIC remix-runtime-settings)
target_include_directories(remix-runtime-support PRIVATE ../src/lssusd "${embeddedOutput}" "${REMIX_GLFW_INCLUDE_DIR}")
target_compile_options(remix-runtime-support PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fms-extensions>")
add_dependencies(remix-runtime-support remix-native-assets remix-validate-rtx-shaders)

add_library(remix-original-runtime SHARED
  overlay_input.cpp
  runtime_log.cpp
  $<TARGET_OBJECTS:remix-runtime-components>
  $<TARGET_OBJECTS:remix-rtx-components>)
set_target_properties(remix-original-runtime PROPERTIES
  OUTPUT_NAME remix-core INSTALL_RPATH "$ORIGIN")
target_include_directories(remix-original-runtime PRIVATE "${REMIX_GLFW_INCLUDE_DIR}")
target_compile_options(remix-original-runtime PRIVATE -fms-extensions)
target_link_libraries(remix-original-runtime PRIVATE remix-runtime-support remix-runtime-settings remix-nrd-loader
  "${CMAKE_CURRENT_LIST_DIR}/../submodules/dlss/lib/Linux_x86_64/libnvsdk_ngx.a")
target_link_options(remix-original-runtime PRIVATE -Wl,--no-undefined)

include(usd_plugins.cmake)

add_library(remix-native-output STATIC offscreen_output.cpp)
set_target_properties(remix-native-output PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(remix-native-output PUBLIC remix-original-runtime remix-runtime-settings)

add_library(remix-native-host STATIC runtime_host.cpp)
set_target_properties(remix-native-host PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(remix-native-host PUBLIC remix-native-output)

include(minecraft_api.cmake)
include(runtime_install.cmake)
