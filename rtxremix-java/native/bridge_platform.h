/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <gl/GL.h>
#pragma comment(lib, "opengl32.lib")
#else
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glx.h>

// Keep the published header intact. Its optional loader and calling convention
// describe Windows; a native ELF runtime uses the host C ABI instead.
#define REMIX_WINAPI_NO_INCLUDE
#define REMIX_WINAPI_NO_LIBRARY_LOADER
#define REMIX_ALLOW_X86
#define REMIXAPI_CALL
#define REMIXAPI __attribute__((visibility("default")))
#define __stdcall
#endif

#include "remix_c.h"

#ifndef _WIN32
#undef __stdcall
#endif

static_assert(sizeof(void*) == 8, "The Remix bridge requires a 64-bit process");

namespace bridge_platform {

#ifdef _WIN32
using Library = HMODULE;
using PathChar = wchar_t;

inline Library loadLibrary(const PathChar* path) {
    return LoadLibraryW(path);
}

inline void* librarySymbol(Library library, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(library, name));
}

inline void closeLibrary(Library library) {
    FreeLibrary(library);
}

inline void* glProcedure(const char* name) {
    return reinterpret_cast<void*>(wglGetProcAddress(name));
}
#else
using Library = void*;
using PathChar = char;

inline Library loadLibrary(const PathChar* path) {
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

inline void* librarySymbol(Library library, const char* name) {
    return dlsym(library, name);
}

inline void closeLibrary(Library library) {
    dlclose(library);
}

inline void* glProcedure(const char* name) {
    if (eglGetCurrentContext() != EGL_NO_CONTEXT) {
        return reinterpret_cast<void*>(eglGetProcAddress(name));
    }
    if (glXGetCurrentContext() != nullptr) {
        return reinterpret_cast<void*>(glXGetProcAddressARB(
                reinterpret_cast<const GLubyte*>(name)));
    }
    return nullptr;
}
#endif

} // namespace bridge_platform
