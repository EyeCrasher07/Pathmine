# Native Linux runtime port

The goal is to retain the original Minecraft capture pipeline and NVIDIA RTX
Remix renderer on Linux, with the same materials, lighting and frame processing.
A replacement path tracer is outside the scope of this port.

The Minecraft-independent JNI bridge builds as `libremixbridge.so` with CMake.
Linux loads `libremix.so` through `dlopen`, imports Vulkan images and timeline
semaphores through duplicated Linux file descriptors, and presents the composited
frame through Minecraft's OpenGL presentation path. The 26.3 integration uses
SDL3 for windows and input; earlier 1.21.1 builds use GLFW. Windows keeps the
existing DLL and Win32 handle paths.

```sh
./gradlew build testNativeLinux64 -Parch=linux64
```

This builds the lightweight mod and validates its JNI boundary. It does not
build or bundle a complete NVIDIA runtime for Linux. `setupRuntime` and `package`
require the ported runtime at `remix-runtime/linux64/libremix.so` and explicitly
reject a missing library, non-ELF file or missing core dependency. The directory
must also contain `libremix-core.so`, `libNRD.so`, `libusd_ms.so`, `libtbb.so.12`,
the native NGX libraries, USD resources/plugins and their license texts. The
packaged JAR carries that directory and the JNI bridge; extraction installs them
beside one another without a launcher-specific runtime setup.

The upstream runtime is `NVIDIAGameWorks/dxvk-remix`. Its Windows-specific
platform and dependency code must be ported while retaining the original RTX
passes, material processing, denoising and upscaling integrations. DXVK Native's
Linux platform support is a source reference for the common DXVK foundation.
The pinned Minecraft build uses Remix API 0.16; the available runtime source
uses API 0.6. The port isolates the unchanged API 0.16 header and implements its
Minecraft-used calls over that renderer.

The native runtime source port now executes the original path-tracing and NRD
pipeline on the RTX 2060 and exchanges color/depth with OpenGL through Vulkan
FDs. Its native host has separate frame, resize and minimized-window tests,
including GPU texture updates. API 0.16 components now connect textures, materials,
cameras, configuration layers and meshes to the original renderer. Mesh tests
exercise atlas wrapping and animated spritesheets through the original GPU
pipeline. Height-map parallax and opacity-micromap atlas bounds remain incomplete.
Retained and transient scene components now use the original instance/material
preservation paths. Tests cover updates, removal, cache recovery, skinning and
GPU instancing. Each transient `DrawInstance` call invalidates that object's
previous surface identity, matching the shipped renderer's temporal behavior.
Retained API objects preserve their history, and both paths reuse cached geometry.
Lighting components connect the original analytical light classes,
spot shaping and USD parameters. Native dome composition supports texture alpha,
opacity and the pinned runtime's 11 blend modes, preserving the mod's sky layers.
The separate `libremix.so` C API host supplies all 36 table slots; the 28 calls
used by Minecraft route to the original renderer, while unused legacy D3D9 and
object-picking calls return errors. Linux requires `remixapi_BeginFrame` before
camera capture so projections use the current framebuffer size. Tests load the
production library through `dlopen`, render and modify scenes, exchange images
with OpenGL, resize/minimize and restart it. A real Java startup test also covers
the small Java thread stack; native NGX initialization uses a joined native
thread to accommodate the driver's larger stack allocation.

Linux publishes the original Remix interface as a separate premultiplied RGBA8
image, alongside the world color and depth images under the same timeline
semaphores. The mod composites it after Minecraft's HUD and screens. Minecraft mouse,
keyboard, Unicode and focus events reach the existing ImGui controls through
two Linux extension exports, without changing the pinned API 0.16 table.
The native loader requires both exports. GPU tests cover a visible menu, clearing
its pixels on close, image resize/minimize and renderer restart. JNI tests check
input marshalling and shutdown. The Linux target has also rendered the menu in
Minecraft, and its mouse controls have been verified interactively.

The packaged JAR has been extracted and launched in Minecraft 1.21.1 on the
Linux target. A saved Windows reference world renders terrain, water, entities
and shadows with the native runtime and author defaults. The compiler's Remix
pending-work counter is explicitly initialized, so an empty queue cannot block
tracing by inheriting allocator bytes. A poisoned-storage regression checks this
independently of the synchronous shader settings in most GPU fixtures.
This is separate evidence from the synthetic GPU and JNI tests. Exact visual
parity with the private runtime and long-session stability remain unverified.
The renderer core must not be installed as `libremix.so` on its own.

Thick textured glass retains its color through volume
attenuation: material copies share a medium identity, and matching translucent
hits supply their sampled transmittance. The selection follows the shipped
Windows shaders. Other in-world materials still need comparison.

The maintained [renderer parity probe](../tools/parity/README.md) freezes one
Java/API bundle for both systems and captures 25 material and lighting scenes.
Each scene starts a fresh native runtime process with isolated user settings.
Two original Windows passes and two native Linux passes use the same bundle,
with renderer time fixed at 16 milliseconds per frame because adaptive denoiser
history depends on frame duration. On the tested RTX 2060, 21 of the 25 scenes
differ by at most 3/255 per channel, including all ten glass cases at 1/255 or
less. Four PBR reflection scenes remain beyond same-system repeat variation,
so controlling frame time has not resolved those differences. The original
Windows runtime and native port also have different source revisions and driver
versions; these captures do not isolate an operating-system or driver cause.
The comparison checks input identities and effective options before reporting
per-pixel differences without pass thresholds.
The probe also captures normal, material and noisy/denoised lighting views,
with read-only snapshots of derived settings. Intermediate views are written
after postprocessing so tonemapping and filters do not alter their values.
White/blue-noise controls help distinguish sample-seed differences. These are
8-bit visualizations, not full-precision buffer exports. Matching original
Windows captures identified an API normal-texture conversion difference:
the native adapter now preserves the original RG8 sphere-octahedral encoding
and byte rounding. Flat normal-map shading now matches exactly; the tilted
case's mean channel error is below 0.0001/255 with a maximum of 3/255 in the
tested scene. Geometry normals, diffuse albedo, roughness and the two noise
controls match exactly in all four PBR scenes. Indirect-lighting differences
remain. Custom startup also preserves the original measured radiance-cache
defaults; matching those defaults alone did not resolve the indirect-lighting
difference.
Startup ordering also matters: API configuration must be resolved before device
quality initialization, so a requested Custom preset retains the configured
defaults instead of inheriting an automatic preset. Matching selected option
values after startup alone does not establish matching derived quality settings.

The mod is launcher-independent. The repository's Prism tools provide one local
verification route; they are not a requirement for using the finished mod.

The native distribution target now installs libraries with SDK-relative loader
paths and checks their imported glibc, libstdc++ and C++ ABI symbol versions.
The core and dependencies have been rebuilt against a glibc 2.36 environment.
The extracted packaged payload, including JNI and USD schema plugins, loads
there with developer home and build directories hidden. The resulting JAR has
also been launched in Minecraft on the Linux target. These binary/load checks
do not establish graphics driver support across distributions or other GPUs.

Current source target: Minecraft 26.3 with Fabric, Java 25, and native Windows
or Linux x86_64 runtimes. The packaged 26.3 port has been exercised on both
systems on an RTX 2060, including terrain, entities, menus, weather, and RTX
toggling. The deferred terrain recapture fix for missing block-entity models
has been confirmed on both systems. The latest held-glass material correction
has Linux gameplay evidence; its Windows gameplay retest is still pending.
The native renderer and Windows DLL payloads are unchanged by that correction.

The 1.21.1 reference comparisons above remain evidence for that version, not a
claim of exact 26.3 parity. Crowded multiplayer performance, long sessions,
other GPUs/distributions, and the remaining planned game versions and loaders
still need validation. Native JNI and GPU interop tests remain separate
evidence from gameplay and visual parity.
