if(NOT TARGET remix-api016-headers)
  return()
endif()
if(NOT TARGET remix-native-host)
  message(FATAL_ERROR "The Minecraft API adapter requires the original native runtime host")
endif()

add_library(remix-api016-resources STATIC
  api016_cameras.cpp api016_instances.cpp api016_lights.cpp api016_materials.cpp api016_meshes.cpp api016_options.cpp api016_textures.cpp)
set_target_properties(remix-api016-resources PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(remix-api016-resources PUBLIC remix-api016-headers remix-native-host)
add_dependencies(remix-api016-resources remix-validate-rtx-shaders)

add_library(remix-api016-runtime SHARED runtime_api.cpp)
target_compile_definitions(remix-api016-runtime PRIVATE REMIX_NATIVE_API016=1)
target_link_libraries(remix-api016-runtime PRIVATE remix-api016-resources)
set_target_properties(remix-api016-runtime PROPERTIES
  OUTPUT_NAME remix BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN"
  LINK_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/runtime_api.exports")
target_link_options(remix-api016-runtime PRIVATE -Wl,--no-undefined
  -Wl,--defsym=remixapi_InitializeLibrary=remixapi016_InitializeLibraryDeclaration
  "-Wl,--version-script=${CMAKE_CURRENT_LIST_DIR}/runtime_api.exports")
