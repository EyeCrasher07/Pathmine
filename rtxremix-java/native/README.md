# Native JNI bridge

The bridge marshals the Java facade into the Remix C API. It is not a renderer:
a matching runtime library with that API is also required. The Windows DLL
cannot be loaded into the Linux JVM.

Windows builds continue to use `build_native.bat` and produce
`build/native/{win64|arm64}/remixbridge.dll` from the repository root.

On Linux, use a 64-bit JDK 21, CMake, a C++17 compiler, and the OpenGL, GLX and
EGL development headers/libraries:

```sh
JAVA_HOME=/path/to/jdk-21 cmake -S rtxremix-java/native -B build/native/linux64 \
    -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/native/linux64 --parallel 2
```

This produces `build/native/linux64/libremixbridge.so`. A custom CMake library
output directory is also supported. Linux loads an ELF runtime with `dlopen`
and resolves the API table with `dlsym`. The loader/version checks use the same
copied `include/remix_c.h` as Windows; `bridge_platform.h` adapts its optional
Windows loader declarations without modifying the header or its version.

The Java facade loads the host bridge beside the selected runtime, or from
`java.library.path`. Linux's `remixbridge_probe_runtime` accepts a UTF-8 path;
Windows retains its UTF-16 path. The C API's `remixapi_Path` stays the header's
native `wchar_t` string; a Linux implementation must consume native wide code
points, while Windows consumes UTF-16.

The native Linux runtime also exports `remixapi_BeginFrame`,
`remixapi_GetOverlayInteropInfo` and `remixapi_InjectOverlayInput`. The loader
requires these extensions while preserving the published API table. Overlay
RGBA8 pixels are premultiplied, share the world frame's semaphores and are
composited after Minecraft's own GUI. Input events carry GLFW key/modifier codes,
framebuffer pointer coordinates, Unicode characters or focus changes. Windows
continues to use its runtime's existing window-input and presentation path.

The Linux build does not install the Windows crash handler or rewrite process
arguments. Windows-only command-line redaction and D3D9 COM/legacy helpers
explicitly throw `UnsupportedOperationException` on Linux. This does not
disable the JVM's own fatal-error reports or the operating system's core dumps.

Linux external-memory/semaphore imports must duplicate the runtime's file
descriptors before handing them to OpenGL. `duplicateFileDescriptor` returns an
owned close-on-exec descriptor; a successful GL import consumes it. Call
`closeFileDescriptor` only if the import failed before taking ownership.

## Binding and handshake tests

The tests compile the actual Java facade using its existing JOML dependency,
load the ELF bridge into Java 21 with `-Xcheck:jni`, resolve every declared JNI
method, verify descriptor ownership errors, and test missing-library/export,
version mismatch, incomplete tables, Unicode paths, and shutdown cleanup.

```sh
JAVA_HOME=/path/to/jdk-21 cmake -S rtxremix-java/native -B build/native/linux64 \
    -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    -DREMIX_JOML_JAR=/path/to/joml-1.10.5.jar
cmake --build build/native/linux64 --parallel 2
ctest --test-dir build/native/linux64 --output-on-failure
```

The shared libraries under `tests/` are loader/handshake fixtures only. They
provide no device, scene or frame rendering, and must never be distributed as
a Remix runtime. Passing these tests establishes JNI loading and host-boundary
behavior; it does not establish working ray tracing or gameplay.
