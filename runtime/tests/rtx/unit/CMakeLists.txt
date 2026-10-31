# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(remix_buffer_tests LANGUAGES CXX)
  include(CTest)
endif()

foreach(bufferTest retained_buffer_table pending_slot_queues bindless_buffer_mirror)
  add_executable(test_${bufferTest} test_${bufferTest}.cpp)
  target_compile_features(test_${bufferTest} PRIVATE cxx_std_17)
  target_include_directories(test_${bufferTest} PRIVATE ../../../include ../../../src/dxvk)
  target_compile_definitions(test_${bufferTest} PRIVATE XXH_INLINE_ALL)
  set_property(TARGET test_${bufferTest} PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  string(REPLACE "_" "-" testName "${bufferTest}")
  add_test(NAME native-${testName} COMMAND test_${bufferTest})
endforeach()
