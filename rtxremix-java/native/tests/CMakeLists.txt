# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
find_package(Java 21 REQUIRED COMPONENTS Runtime Development)
set(REMIX_JOML_JAR "" CACHE FILEPATH "Existing JOML jar used to compile the Java facade tests")
if(NOT EXISTS "${REMIX_JOML_JAR}")
    message(FATAL_ERROR "Set REMIX_JOML_JAR to the facade's existing JOML jar to run JNI tests")
endif()

foreach(fixture IN ITEMS missing_export incompatible incomplete handshake handshake_no_counter)
    add_library(test_${fixture} SHARED handshake_fixture.cpp)
    target_compile_features(test_${fixture} PRIVATE cxx_std_17)
    target_include_directories(test_${fixture} PRIVATE .. ../include)
    target_compile_definitions(test_${fixture} PRIVATE FIXTURE_${fixture})
    set_target_properties(test_${fixture} PROPERTIES OUTPUT_NAME "test-${fixture}")
endforeach()
target_compile_definitions(test_handshake_no_counter PRIVATE FIXTURE_handshake FIXTURE_no_shader_counter)

file(GLOB wrapper_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/com/nvidia/rtxremix/*.java")
set(test_classes "${CMAKE_CURRENT_BINARY_DIR}/classes")
add_custom_command(OUTPUT "${test_classes}/NativeBridgeTest.class"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${test_classes}"
        COMMAND "${Java_JAVAC_EXECUTABLE}" --release 21 -cp "${REMIX_JOML_JAR}" -d "${test_classes}"
                ${wrapper_sources} "${CMAKE_CURRENT_SOURCE_DIR}/NativeBridgeTest.java"
        DEPENDS ${wrapper_sources} NativeBridgeTest.java
        VERBATIM)
add_custom_target(native_bridge_test_classes ALL
        DEPENDS "${test_classes}/NativeBridgeTest.class")

add_test(NAME native-jni-bridge
        COMMAND "${Java_JAVA_EXECUTABLE}" -Xcheck:jni
                "-Djava.library.path=$<TARGET_FILE_DIR:remixbridge>"
                -cp "${test_classes}:${REMIX_JOML_JAR}" NativeBridgeTest
                "$<TARGET_FILE:remixbridge>" "$<TARGET_FILE:test_missing_export>"
                "$<TARGET_FILE:test_incompatible>" "$<TARGET_FILE:test_incomplete>"
                "$<TARGET_FILE:test_handshake>" "$<TARGET_FILE:test_handshake_no_counter>")
set_tests_properties(native-jni-bridge PROPERTIES TIMEOUT 30)
