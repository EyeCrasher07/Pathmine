set(nativeUsdPluginsDir "${CMAKE_CURRENT_BINARY_DIR}/usd/plugins")
file(MAKE_DIRECTORY "${nativeUsdPluginsDir}")
configure_file(../src/usd-plugins/plugInfo.json "${nativeUsdPluginsDir}/plugInfo.json" COPYONLY)

foreach(plugin IN ITEMS RemixCategories RemixParticleSystem)
  if(plugin STREQUAL "RemixCategories")
    set(source remixInstanceCategoryAPI.cpp)
  else()
    set(source particleSystemAPI.cpp)
  endif()
  set(pluginSource "${CMAKE_CURRENT_LIST_DIR}/../src/usd-plugins/${plugin}")
  set(pluginOutput "${nativeUsdPluginsDir}/${plugin}")
  add_library(native-${plugin} MODULE "${pluginSource}/${source}")
  target_compile_features(native-${plugin} PRIVATE cxx_std_17)
  target_link_libraries(native-${plugin} PRIVATE usd_m)
  set_target_properties(native-${plugin} PROPERTIES
    OUTPUT_NAME "${plugin}" LIBRARY_OUTPUT_DIRECTORY "${pluginOutput}"
    INSTALL_RPATH "$ORIGIN/../../..")
  file(MAKE_DIRECTORY "${pluginOutput}/resources")
  file(READ "${pluginSource}/resources/plugInfo.json" pluginInfo)
  string(JSON pluginInfo SET "${pluginInfo}" Plugins 0 LibraryPath
    "\"${CMAKE_SHARED_MODULE_PREFIX}${plugin}${CMAKE_SHARED_MODULE_SUFFIX}\"")
  file(WRITE "${pluginOutput}/resources/plugInfo.json" "${pluginInfo}\n")
  configure_file("${pluginSource}/resources/generatedSchema.usda"
    "${pluginOutput}/generatedSchema.usda" COPYONLY)
endforeach()
