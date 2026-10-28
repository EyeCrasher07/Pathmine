if(NOT TARGET remix-api016-runtime)
  return()
endif()

install(TARGETS remix-original-runtime remix-api016-runtime
  LIBRARY DESTINATION . COMPONENT NativeRuntime)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libNRD.so"
  "$<TARGET_FILE:usd_m>" DESTINATION . COMPONENT NativeRuntime)
install(FILES "$<TARGET_FILE:TBB::tbb>"
  DESTINATION . RENAME libtbb.so.12 COMPONENT NativeRuntime)
find_program(REMIX_PATCHELF patchelf REQUIRED)
# OpenUSD's original install layout differs from the flat SDK library directory.
install(CODE "execute_process(COMMAND \"${REMIX_PATCHELF}\" --set-rpath \"$ORIGIN\" \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/libusd_ms.so\" COMMAND_ERROR_IS_FATAL ANY)"
  COMPONENT NativeRuntime)
install(DIRECTORY "$<TARGET_FILE_DIR:usd_m>/usd/"
  DESTINATION usd COMPONENT NativeRuntime)

foreach(plugin IN ITEMS RemixCategories RemixParticleSystem)
  install(TARGETS native-${plugin}
    LIBRARY DESTINATION "usd/plugins/${plugin}" COMPONENT NativeRuntime)
  install(FILES "${nativeUsdPluginsDir}/${plugin}/resources/plugInfo.json"
    DESTINATION "usd/plugins/${plugin}/resources" COMPONENT NativeRuntime)
  install(FILES "${nativeUsdPluginsDir}/${plugin}/generatedSchema.usda"
    DESTINATION "usd/plugins/${plugin}" COMPONENT NativeRuntime)
endforeach()
install(FILES "${nativeUsdPluginsDir}/plugInfo.json"
  DESTINATION usd/plugins COMPONENT NativeRuntime)

foreach(feature IN ITEMS dlss dlssd)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libnvidia-ngx-${feature}.so.${dlssNativeVersion}"
    DESTINATION . COMPONENT NativeRuntime)
endforeach()

install(FILES "${CMAKE_CURRENT_LIST_DIR}/../LICENSE"
  "${CMAKE_CURRENT_LIST_DIR}/../LICENSE-MIT"
  "${CMAKE_CURRENT_LIST_DIR}/THIRD_PARTY_NOTICES.md"
  DESTINATION licenses COMPONENT NativeRuntime)
install(FILES "${CMAKE_CURRENT_LIST_DIR}/../submodules/dlss/LICENSE.txt"
  DESTINATION licenses RENAME DLSS-LICENSE.txt COMPONENT NativeRuntime)
install(FILES "${REMIX_NRD_SOURCE_DIR}/LICENSE.txt"
  DESTINATION licenses RENAME NRD-LICENSE.txt COMPONENT NativeRuntime)
set(REMIX_OPENUSD_SOURCE_DIR "" CACHE PATH "Pinned OpenUSD source for distribution notices")
set(REMIX_TBB_SOURCE_DIR "" CACHE PATH "Pinned oneTBB source for distribution notices")
install(FILES "${REMIX_OPENUSD_SOURCE_DIR}/LICENSE.txt"
  DESTINATION licenses RENAME OpenUSD-LICENSE.txt COMPONENT NativeRuntime)
install(FILES "${REMIX_OPENUSD_SOURCE_DIR}/NOTICE.txt"
  DESTINATION licenses RENAME OpenUSD-NOTICE.txt COMPONENT NativeRuntime)
install(FILES "${REMIX_TBB_SOURCE_DIR}/LICENSE.txt"
  DESTINATION licenses RENAME oneTBB-LICENSE.txt COMPONENT NativeRuntime)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
string(JSON distributionGlibc GET "${nativeDependencyLock}" linuxDistribution glibc)
string(JSON distributionGlibcxx GET "${nativeDependencyLock}" linuxDistribution glibcxx)
string(JSON distributionCxxabi GET "${nativeDependencyLock}" linuxDistribution cxxabi)
set(REMIX_DISTRIBUTION_GLIBC "${distributionGlibc}" CACHE STRING "Maximum glibc symbol version in a release")
set(REMIX_DISTRIBUTION_GLIBCXX "${distributionGlibcxx}" CACHE STRING "Maximum libstdc++ symbol version in a release")
set(REMIX_DISTRIBUTION_CXXABI "${distributionCxxabi}" CACHE STRING "Maximum C++ ABI symbol version in a release")
set(nativeDistributionDir "${CMAKE_CURRENT_BINARY_DIR}/remix-sdk")
add_custom_target(remix-package-runtime
  COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_CURRENT_BINARY_DIR}"
    --prefix "${nativeDistributionDir}" --component NativeRuntime --strip
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/check_runtime.py"
    "${nativeDistributionDir}" --max-glibc "${REMIX_DISTRIBUTION_GLIBC}"
    --max-glibcxx "${REMIX_DISTRIBUTION_GLIBCXX}"
    --max-cxxabi "${REMIX_DISTRIBUTION_CXXABI}"
    --ngx-source "${CMAKE_CURRENT_LIST_DIR}/../submodules/dlss/lib/Linux_x86_64/rel"
  DEPENDS remix-original-runtime remix-api016-runtime native-RemixCategories native-RemixParticleSystem
  VERBATIM)
