set(REMIX_MINECRAFT_API_HEADER "" CACHE FILEPATH "The immutable Minecraft API 0.16 header")
if(NOT REMIX_MINECRAFT_API_HEADER)
  return()
endif()
if(NOT TARGET remix-native-host)
  message(FATAL_ERROR "The Minecraft API adapter requires the original native runtime host")
endif()

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
