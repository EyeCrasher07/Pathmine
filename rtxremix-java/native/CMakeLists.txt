# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20)
project(remixbridge LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This CMake entry point builds the Linux bridge; Windows uses build_native.bat")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The Remix bridge requires a 64-bit target")
endif()

find_package(JNI REQUIRED COMPONENTS JVM)
find_package(OpenGL REQUIRED COMPONENTS OpenGL GLX EGL)
find_package(Threads REQUIRED)

add_library(remixbridge SHARED remixbridge.cpp)
target_compile_features(remixbridge PRIVATE cxx_std_17)
target_include_directories(remixbridge PRIVATE ${JNI_INCLUDE_DIRS} include)
target_link_libraries(remixbridge PRIVATE OpenGL::OpenGL OpenGL::GLX OpenGL::EGL
        Threads::Threads ${CMAKE_DL_LIBS})
target_compile_options(remixbridge PRIVATE -Wall -Wextra -Wno-unused-parameter)
set_target_properties(remixbridge PROPERTIES
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES
        BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")

option(BUILD_TESTING "Build the JNI binding/handshake tests (requires the facade's JOML jar)" OFF)
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
