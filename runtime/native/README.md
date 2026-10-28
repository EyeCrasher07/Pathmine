# Native Linux source port

This directory builds the original NVIDIA Remix renderer and a Minecraft API
host with native Linux tools. The API host produces `libremix.so`, which links
`libremix-core.so`. Minecraft integration and visual parity are being verified
on Linux x86_64; this is not an upstream-supported Linux release.

The renderer, scene system, materials, Slang passes, RTXDI, SHARC and NRD remain
the original implementations. Platform changes provide POSIX services, Vulkan
FD handles and an offscreen host boundary. Minecraft owns its window and final
OpenGL presentation on Linux.

## Dependencies

Exact source revisions and compiler archive checksums are recorded in
[`dependencies.lock.json`](dependencies.lock.json). Renderer submodules retain
the revisions pinned by the root repository.

`runtime.revision` identifies the port's source base. `runtime.upstreamFixes`
records additional NVIDIA commits applied on top of that base, so the lock
describes selective backports without implying a rebase of the platform port.
An entry's `scope` identifies the included portions of a partial backport.

- Native Vulkan headers come from the pinned root submodule.
- RTXDI uses the locked revision paired with the best-light sampling backport.
  Its reservoir normalization and shared jittered lookup must agree with both
  the initial-sampling and temporal-reuse passes.
- DXVK Native supplies portable DirectX/COM type headers. Its Win32 API stubs
  are not included. CMake fetches and verifies the pinned header archives if
  local source directories are not specified.
- NRD is built from NVIDIA's 4.13 source with its original SPIR-V shaders.
  ShaderMake and MathLib must use the revisions in the lock file.
  Apply [the native storage-format patch](patches/nrd-4.13-vulkan-storage-formats.patch)
  and use the pinned DXC 1.9.2609. NRD selects image formats at runtime, so its
  SPIR-V storage images must use `Unknown` rather than infer a fixed float32
  format. The device enables the required read/write-without-format features.
  Shader algorithms and pool formats are preserved.
- OpenUSD 25.11 is built without Python or imaging, using oneTBB 2021.12.0.
  This TBB version is the one selected by OpenUSD's own build script.
- Native Slang 2026.20 preserves Remix's `-zero-initialize` shader option.
  The public Slang 2025.10.4 binary does not accept that option.
- Original fonts, MDL assets and extra NGX headers are fetched through the
  repository's native Packman launcher using the root package versions.
- Native NVIDIA architecture detection loads the driver's `libnvidia-ml.so.1`
  and matches the Vulkan adapter UUID. NVML declarations come from a pinned
  NVIDIA package. See [dependency notices](THIRD_PARTY_NOTICES.md).

The native targets compile ELF objects and load ELF libraries. The Windows
binaries contained in vendor SDK packages are not used by these targets.

Run the commands in this guide from `runtime/` in a recursive Pathmine clone.
Fetch only the four original asset/header packages used by the native target:

```sh
bash scripts-common/packman/packman pull packman-external.xml --include-tag native
```

Obtain NRD, ShaderMake, MathLib, OpenUSD and oneTBB from the public repositories
and exact commits under `sources` in `dependencies.lock.json`. Check out those
commits before configuring. Keep these dependency source/build/install directories
outside the tracked tree. Download the Slang and DXC archives from `tools` in the
lock and verify their SHA-256 values before extracting them. OpenUSD's standard
CMake build needs `PXR_ENABLE_PYTHON_SUPPORT=OFF`, `PXR_BUILD_IMAGING=OFF`,
`PXR_BUILD_USD_IMAGING=OFF`, `PXR_BUILD_MONOLITHIC=ON`, and `TBB_DIR` set to
the locked oneTBB installation. The original `pxr_DIR` for this build is the
OpenUSD installation prefix. NRD configuration follows below.

For the native NRD build, apply the patch to the pinned source and set
`DXC_SPIRV_PATH` explicitly. Use a fresh shader output directory when changing
compiler or flags: ShaderMake can retain bytecode produced by an earlier compiler.
The dependency source variables below must point at the locked revisions.

```sh
git -C "$remix_nrd_source" apply "$remix_port_source/native/patches/nrd-4.13-vulkan-storage-formats.patch"
cmake -S "$remix_nrd_source" -B "$remix_nrd_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DNRD_NORMAL_ENCODING=2 -DNRD_ROUGHNESS_ENCODING=1 \
  -DNRD_EMBEDS_DXBC_SHADERS=OFF -DNRD_EMBEDS_DXIL_SHADERS=OFF \
  -DNRD_EMBEDS_SPIRV_SHADERS=ON \
  -DDXC_SPIRV_PATH:FILEPATH="$remix_dxc" \
  -DNRD_SHADERS_PATH:PATH="$remix_nrd_build/shaders-dxc-1.9.2609-untyped" \
  -DFETCHCONTENT_SOURCE_DIR_SHADERMAKE="$remix_shadermake_source" \
  -DFETCHCONTENT_SOURCE_DIR_MATHLIB="$remix_mathlib_source"
cmake --build "$remix_nrd_build" --parallel 4
```

## Platform build

Requires a C++17 compiler, CMake, Ninja and Vulkan development files. To include
NRD validation, point CMake at the pinned NRD source and the built `libNRD.so`:

```sh
cmake -S native -B _NativePlatform -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DREMIX_NRD_SOURCE_DIR="$remix_nrd_source" \
  -DREMIX_NRD_LIBRARY="$remix_nrd_library"
cmake --build _NativePlatform --parallel 4
ctest --test-dir _NativePlatform --output-on-failure
```

`remix-vulkan-native`, `remix-util-native` and `remix-core-native` compile the
original Vulkan loader, platform/configuration services, format/extension code
and `RtxSemaphore`. Tests cover C API layout, ELF loading, UTF-8 paths and config
files, key identifiers, GPU semaphore FD exchange and native NRD dispatch data.
The NRD test validates shader/dispatch descriptions; it does not execute a
complete denoising frame. It checks runtime-compatible storage-image formats
across all 19 supported denoisers, including float and integer clears.

The renderer build also tests native OpenUSD geometry/material file exchange
and GPU quality selection. On the RTX 2060, the NVML test verifies the Turing
architecture, original low-memory defaults and rejection of an unmatched UUID.

## Renderer component build

Clang is required for the original runtime's MSVC aggregate extensions.
Configure a separate build with `-DCMAKE_CXX_COMPILER=clang++`,
`-DREMIX_BUILD_RUNTIME_COMPONENTS=ON`, the NRD paths above, `pxr_DIR` pointing
at the native OpenUSD installation and `TBB_DIR` pointing at its matching TBB
CMake package. This compiles original DXVK renderer components and GLSL passes.

`REMIX_BUILD_SHADERS=ON` additionally requires `REMIX_SLANGC`,
`REMIX_SLANG_LIBRARY`, `glslangValidator` and `spirv-val`. The `remix-rtx-shaders`
target runs the original shader compile script. Set `REMIX_BUILD_RTX_COMPONENTS`
to compile the original RTX source files registered in `src/dxvk/meson.build`.
These sources depend on `remix-validate-rtx-shaders`, which checks the compiled
SPIR-V with scalar block layout enabled for the runtime's Vulkan 1.4 environment.
With `REMIX_BUILD_RUNTIME_LIBRARY=ON`, these objects link with the original
utility, USD and ImGui code into `libremix-core.so`. Linking rejects unresolved
symbols. This is the original renderer core, without the Windows D3D9 API host.
The Minecraft adapter below supplies the separate `libremix.so` entry point.

With the locked dependencies built and variables pointing at their actual
locations, the complete renderer configuration is:

```sh
cmake -S native -B _NativeRuntime -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ \
  -DREMIX_BUILD_RUNTIME_COMPONENTS=ON -DREMIX_BUILD_RUNTIME_LIBRARY=ON \
  -DREMIX_BUILD_RTX_COMPONENTS=ON -DREMIX_BUILD_SHADERS=ON \
  -DREMIX_NRD_SOURCE_DIR="$remix_nrd_source" \
  -DREMIX_NRD_LIBRARY="$remix_nrd_library" \
  -Dpxr_DIR="$remix_usd_install" -DTBB_DIR="$remix_tbb_cmake" \
  -DREMIX_SLANGC="$remix_slangc" -DREMIX_SLANG_LIBRARY="$remix_slang_library" \
  -DREMIX_GLSLANG="$remix_glslang" -DREMIX_SPIRV_VAL="$remix_spirv_val" \
  -DREMIX_MINECRAFT_API_HEADER="$PWD/../rtxremix-java/native/include/remix_c.h" \
  -DREMIX_OPENUSD_SOURCE_DIR="$remix_usd_source" \
  -DREMIX_TBB_SOURCE_DIR="$remix_tbb_source"
cmake --build _NativeRuntime --parallel 4
ctest --test-dir _NativeRuntime --output-on-failure
```

The full suite needs an RTX-capable Vulkan driver and a working OpenGL display.
The platform build described earlier requires no NRD/OpenUSD installation; passing it
does not validate a complete renderer.

CMake records the shader compiler's complete output manifest, including variant
headers. Generated bytecode headers are build dependencies of their C++ consumers,
so one build invocation also updates the library that embeds changed shaders.

Native DLSS Super Resolution and Ray Reconstruction libraries are staged beside
the core. Capability discovery works on the RTX 2060. Super Resolution GPU
evaluation passes the camera, resize and readback regression below on that GPU;
Ray Reconstruction evaluation still needs verification. NRC, DLSS Neural Rendering, Frame Generation, Reflex,
Aftermath and XeSS are unavailable when their native SDK or host integration is
missing. They are not advertised as working features.

`native-original-device` creates the original `DxvkInstance`, `DxvkDevice` and
scene/pass objects, executes GPU commands and verifies image pixels on readback.
It also checks 24 original-image FD exports/imports with changing extents,
borrowed FD ownership, allocation retry, complete import failure, image cleanup
and descriptor leak detection. Shared allocations cannot fall back to private
memory. The original texture streamer is stopped during device teardown.

`native-original-frame` exercises a small emissive mesh through the original
external scene submission, camera, path-tracing, native NRD and post-processing
pipeline. It verifies 16 GPU frames, camera translation, three image extents
and restoration of the original extent. PPM captures and color/depth readbacks
check that geometry follows the camera and separates from the background.

`native-original-frame-dlss` runs the same fixture with DLSS Super Resolution.
Its output barriers cover both compute writes and transfer clears issued by
NGX. Synchronization validation rejects the previous compute-only barriers.
The test skips when the device does not support DLSS.

`remix-native-output` supplies shared RGBA8 color and optional R32 float depth
images around the original device. `beginFrame()` acquires them from the external
API; `endFrame()` flushes the original context, copies the original depth when
supplied, releases ownership and signals a shared timeline semaphore. A requested
depth image without a source is cleared to the far plane.
`present()` records the client's completion value for the next acquire.
Queue submission uses the original device's queue lock. Command-buffer reuse
and resize wait for the corresponding GPU/client work. Exported FDs are borrowed;
consuming OpenGL imports must receive duplicates.

`remix-native-host` owns the original instance, device, context and offscreen
transport. Its frame order is `beginFrame(extent)`, scene capture, `submit()`,
client OpenGL work and `present(clientDoneValue)`. Extents come from the host
application. A zero dimension skips the frame before capture. Reentrant frames
and publication of failed frames are rejected. Frames without ray-traced output
publish cleared color and far-plane depth rather than stale scene images.
The client must finish its OpenGL work before destroying the host.

`native-original-output` checks 24 Vulkan/OpenGL color exchanges with repeated
resize, complete pixel checks, invalid frame transitions and FD leak detection.
`native-original-frame-gl` runs the original path-tracing fixture through this
transport and compares OpenGL color/depth against Vulkan readback byte for byte.
These OpenGL tests need GLFW, OpenGL development files and a working GPU display.
They use hidden windows. They do not validate Minecraft or desktop fullscreen.

`native-runtime-host` exercises startup/shutdown, invalid frame transitions,
empty frames, minimized-window recovery and resize. It uploads and changes an
sRGB texture on a mesh rendered through the original material/path-tracing
pipeline, then verifies the changed colors through OpenGL.

NGX capability discovery runs on a worker. A requested DLSS render size is
resolved before the native host allocates shared depth or accepts the first
camera. Unsupported-feature fallback waits for completed capability discovery.
`native-ngx-startup-shutdown` destroys two fresh hosts without waiting explicitly
for NGX. `native-ngx-startup-none` and `native-ngx-startup-dlss` check the first
camera, internal render size and exported depth together. The DLSS case checks
downscaling when supported and full-resolution sizing otherwise. These tests
use Vulkan directly and do not require a visible window.

The original `RemixCategories` and `RemixParticleSystem` USD modules are built
as native shared libraries under `usd/plugins/`. `native-usd-plugins` verifies
module loading, type registration, property defaults and stage serialization.

## Minecraft API adapter components

The adapter consumes the unchanged API 0.16 header from the matching Minecraft
source checkout. CMake verifies its SHA-256 against the dependency lock. The
header's types are isolated from the original API 0.6 types, including opaque
handles. The public engine header is unchanged.

To include the adapter components in the renderer build:

```sh
cmake -S native -B _NativeRuntime \
  -DREMIX_MINECRAFT_API_HEADER:FILEPATH="$minecraft_source/rtxremix-java/native/include/remix_c.h"
cmake --build _NativeRuntime --parallel 4
```

`remix-api016-runtime` builds the C API library. Its 36-slot table implements
the 28 calls used by Minecraft; unused legacy D3D9 and object-picking calls
return errors. An incompatible version is rejected before writing the table.
`remixapi_BeginFrame(width, height)` supplies the framebuffer extent before
camera capture. The Linux JNI bridge requires this extension. The application
then captures, submits, imports the output, signals its completion and presents.
A zero extent skips capture and GPU output until the window is restored.

`native-runtime-api` loads the actual shared library through `dlopen` and tests
the C interface, GPU rendering, OpenGL exchange, resize, minimization and restart.
Native NGX initialization uses a joined native thread because the Linux driver
loader's stack buffer exceeds the default Java render-thread stack.

Deploy the API library together with the core, NRD, OpenUSD, matching TBB, USD
resources/plugins, native NGX feature libraries and their license texts. Keep
relative loader paths for redistributed dependencies. NVIDIA NGX feature
libraries must remain byte-identical to their SDK originals; do not strip them
or modify their RPATH. Never install the core under the API library's name.

### Distribution build

The release ABI ceilings are glibc 2.36, libstdc++ `GLIBCXX_3.4.30` and
`CXXABI_1.3.13`, recorded in the dependency lock. Build the
runtime, NRD, OpenUSD and oneTBB against that baseline. A build on a newer
distribution may work locally while importing newer symbols that older hosts
cannot supply. Shader compilers run on the build host; their generated SPIR-V
does not depend on the target C library. The native dependency lock still applies.

NRD's pinned DXC runs on a host with glibc 2.38 or newer. Generate its shader
headers with the NRD command above, then compile its C++ library in the target
environment using the same source revision, patch and encoding settings:

```sh
cmake -S "$remix_nrd_target_source" -B "$remix_nrd_target_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DNRD_NORMAL_ENCODING=2 -DNRD_ROUGHNESS_ENCODING=1 \
  -DNRD_EMBEDS_DXBC_SHADERS=OFF -DNRD_EMBEDS_DXIL_SHADERS=OFF \
  -DNRD_EMBEDS_SPIRV_SHADERS=ON -DNRD_DISABLE_SHADER_COMPILATION=ON \
  -DCMAKE_CXX_FLAGS="-I$remix_nrd_shader_headers" \
  -DFETCHCONTENT_SOURCE_DIR_MATHLIB="$remix_mathlib_source"
cmake --build "$remix_nrd_target_build" --parallel 4
```

`remix_nrd_shader_headers` names the generated header directory from the host
build. SPIR-V embedding remains enabled. NRD generates `NRDEncoding.hlsli` in
its source directory, so this target source checkout must be writable. For the
core, use the locked Slang compiler and build the locked SPIRV-Tools validator
against its matching SPIRV-Headers revision to validate the Vulkan 1.4 shaders.

Provide `REMIX_OPENUSD_SOURCE_DIR` and `REMIX_TBB_SOURCE_DIR` when configuring
a distribution build so their original license and notice files are included.
Then build the distribution target:

```sh
cmake --build _NativeRuntime --target remix-package-runtime
```

This installs the SDK under `_NativeRuntime/remix-sdk/` using the installed
`$ORIGIN` loader paths. It includes the original USD resources and both Remix
schema plugins. The build tool `patchelf` adjusts only the copied OpenUSD
library's search path for the flat SDK layout. CMake strips only its own installed targets; the copied NGX
feature libraries remain unchanged. Use this installed tree when staging the
mod rather than copying shared libraries directly from the development build.

`check_runtime.py` rejects absolute or escaping loader paths, unreachable
non-system dependencies, wrong ELF architecture and symbols above the release
ABI ceiling. It also compares both NGX feature libraries against their SDK
originals. The check can be run on a relocated or extracted SDK:

```sh
python3 native/check_runtime.py /path/to/remix-sdk \
  --ngx-source submodules/dlss/lib/Linux_x86_64/rel
```

This checks the binary dependency contract. A clean-host load and the GPU/game
checks are separate requirements; passing it alone does not establish graphics
driver support or visual parity.

Native overlays use `remixapi_GetOverlayInteropInfo` and
`remixapi_InjectOverlayInput`, separate Linux exports that preserve the pinned
36-slot interface. The first query enables a shared premultiplied RGBA8 image
for subsequent frames. Its FDs are borrowed, and its ownership transfers under
the color/depth timeline semaphores. The host renders the original ImGui menus
into that image; Minecraft composites it after its own HUD and screens. Input
types 0 through 5 are pointer position, mouse button, wheel, GLFW key, Unicode
character and focus. Buttons/keys use x for the pressed state and y for GLFW
modifier bits. Coordinates are framebuffer pixels. Focus loss releases held input.
The C ABI GPU fixture verifies menu pixels, clearing on close, resize/minimize
and restart through the production exports.

The native render-pass dependency graph is invariant across load/store variants
so the framebuffer and graphics pipeline remain compatible. The ImGui backend
borrows its owning device instead of retaining a reference cycle. The shader
cache's pending Remix counter starts at zero; a poisoned-storage regression
checks the empty queue because ordinary fresh allocations can conceal this bug.

`remix-api016-resources` implements textures, materials, cameras, lights and configuration
around the original runtime. Memory/file texture ownership and batched patches
use the original device and texture manager. Memory uploads support
the nine API formats. Normal conversion preserves the API 0.16 runtime's
RG8 sphere-octahedral texture encoding, including the DirectX Y convention.
Height mips use the original maximum-filter shader. Color
mips preserve sRGB transfer behavior. Patches validate their bounds and row pitch
before any texture in the batch is changed. Existing material references keep
their GPU resources after handle destruction. Each changed texture regenerates
its complete mip chain once per batch; limiting regeneration to dirty regions
remains a performance task.

The Custom graphics preset retains the API 0.16 radiance-cache defaults:
an update downscale factor of 4 and up to 100 accumulated frames. Hardware
quality presets and explicit configuration layers retain their normal priority.

Material conversion uses the original opaque, subsurface, translucent and portal
classes. Material handles carry generations, updates retain texture ownership,
and recreated images with the same asset hash have distinct GPU cache identities.
Camera setup uses the original camera manager and projection code, including
camera-medium attenuation. The frame extent must be set before camera capture.
Configuration uses the original typed option resolver, reference-counted file
layers and strength/threshold blending. Resolved reads report NUL-inclusive UTF-8
buffer capacities. Unsaved controls use the runtime's Derived option layer.
API writes made before `Startup` are resolved after system configuration loads
and before the device selects quality presets. A requested Custom preset therefore
keeps the configured defaults instead of inheriting automatic hardware settings.
`native-runtime-api` checks the resulting bounce limit and postprocessing state,
with upscaling disabled, across shutdown and restart.

`native-api016-header` verifies the isolated ABI layouts. `native-api016-textures`
checks GPU uploads/readbacks, normal conventions, height maxima, sRGB mips,
patches, reference lifetime and a DDS texture rendered through the original
asset streamer and path tracer. It uses a hidden OpenGL window.
`native-api016-materials` renders material and texture updates, alpha transitions
and simultaneous images sharing an asset hash. Its fixed-exposure, emissive-lit
scene also checks distinct red/blue transmission through thick and thin glass.
Copies of a translucent material share the first surface's medium index, so a
hit on another face can use that material's sampled transmittance. Finite-volume
attenuation uses the sampled color only for a matching translucent medium with
a transmission texture and no diffuse layer. Other hits and infinite distances
use the constant medium color. This selection matches the shipped Windows
runtime's shader behavior and has been compared with the same standalone
API 0.16 material probe on both operating systems.
`native-api016-cameras` checks
camera motion, resize, depth projection and medium attenuation/restoration.
`native-api016-options` checks startup writes, typed blending, layer lifetime,
UTF-8 buffers, menu controls and concurrent resolved reads.

`ApiMeshes` copies the pinned vertex/index/skinning data into owned buffers and
registers surfaces with the original mesh registry. Mesh handles use generations
and do not depend on deprecated asset hashes. Packed atlas bounds survive the
original CPU/GPU geometry interleavers and feed ordinary material texture reads.
Animated spritesheets transform bounds with their UVs. `native-api016-meshes`
checks ownership, skinning layout, stale handles, interleaving and six GPU atlas
fixtures. Height-map parallax and opacity-micromap baking still need atlas-bound
integration, and coarser-mip filtering needs further validation.

`ApiInstances` copies instance descriptions into `ExternalScene`. Retained objects
own distinct renderer instances until removed, including coincident objects that
share a mesh. Stable frames use the original preservation path. Material changes,
mesh replacement and cache resets update or recover the original scene data.
Transient API draws expire after a missed frame. External skinning records before
BLAS construction on the frame context; the legacy D3D9 path keeps its own context.
`native-api016-instances` checks 72 unchanged frames, updates/removal/recovery,
generation safety, CPU/GPU skinning readback, GPU instancing and active-scene
shutdown. Empty scenes discard stale surface records and publish cleared output.

`ApiLights` submits the original sphere, rectangle, disk, cylinder and distant
light classes, including spot shaping, volumetric scaling and USD parameters.
Light hashes identify resources and support updates. Draws activate a light for
the current frame. Updates preserve temporal GPU identity; destruction removes
pending activation, including when the same hash is immediately recreated.

Dome lights retain their texture references and blend in draw order. GPU
composition applies texture alpha, clamped opacity and all 11 pinned blend modes
to an HDR environment at the input textures' resolution. Dome textures are
published before bindless descriptors. Legacy domes retain first-selection and
opaque RGB behavior. This compatibility pass feeds the original renderer's
environment input; equivalence with the private runtime's full sky rasterizer
and filtering has not been established. `native-api016-lights` renders analytical
lights and checks dome GPU readback, transforms, texture patches, DDS first-frame
residency, ownership, resource-kind changes and empty-scene frame visibility.

For Vulkan diagnostics, install the Khronos validation layer and enable it only
for the test process. Synchronization validation is configured through a layer
settings file:

```sh
mkdir -p _NativeRuntime/validation
cat > _NativeRuntime/validation/vk_layer_settings.txt <<'EOF'
khronos_validation.validate_sync = true
EOF
env DXVK_ENABLE_VALIDATION_LAYERS=1 \
  DXVK_LOG_CALLSTACKS_ON_VALIDATION_LAYER_ERRORS=0 \
  VK_LAYER_SETTINGS_PATH="$PWD/_NativeRuntime/validation" \
  ctest --test-dir _NativeRuntime -R '^native-(original-|runtime-host|api016-)' --output-on-failure
```

Tests reject validation errors, image-format mismatches and synchronization
hazards. Passing these synthetic GPU tests does not establish Minecraft stability.

## Remaining validation

Minecraft 1.21.1 now loads the packaged native runtime and renders a saved
Windows comparison world on the RTX 2060. The original Remix menus and their
mouse controls have been verified interactively. Visual parity, long-session
resource lifetimes, fullscreen transitions and the remaining private-runtime
material/atlas behavior still need comparison. The original Windows release is
the reference; the changed Windows build has not been verified.
