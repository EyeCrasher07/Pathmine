# Renderer parity probe

This developer diagnostic sends identical API 0.16 scenes to an original Windows
runtime and the native Linux port. It renders hidden 320x240 OpenGL frames and
saves PNGs with the effective settings and SHA-256 identities. It does not launch
Minecraft or change its worlds, mods or configuration. Close Minecraft before
running GPU captures.

The 25 scenes cover textured/constant and thick/thin transmission, diffuse layers,
opaque color controls, unlit/directional/spherical lighting, smooth/rough metal,
normal maps, dielectric indices 1.000/1.333/1.520, and alpha cutout. Dielectric
scenes test material refraction, not the complete Minecraft water integration.
Each fresh runtime/JVM first renders four empty camera-only setup frames,
reapplying the requested options on each frame. It checks their resolved values
before creating any scene mesh, material or light. This lets queued startup
and hardware-preset changes settle before scene history begins. Metadata records
the setup frame count, empty-scene definition and resolved options at that point.
Every scene then uses a fixed camera and 64 frames. Denoiser history, frame
seeds and scene caches cannot carry over from the
previous case. Renderer time advances by a fixed 16 milliseconds per frame on
both systems; adaptive denoiser history otherwise depends on execution speed.
DLSS and Ray Reconstruction are
disabled for the baseline; these captures do not verify those integrations.

Build one bundle and use that exact bundle on both operating systems. A JDK 21
is needed only to build it; the runners use a Java 21 runtime. Supply the eleven
LWJGL 3.3.3, JOML 1.10.5 and SLF4J 2.0.9 JARs listed in `rtx_tools.parity.LIBRARIES`,
including both operating systems' native LWJGL JARs. These are the public Java
dependencies already used by Minecraft. The bundle extracts the facade and
OpenGL interop classes from the supplied mod JAR and compiles this probe against
them. It carries no Remix runtime, game content or account data.

From the repository root:

```sh
uv run --locked --project tools python -m rtx_tools.parity build \
  --artifact path/to/packaged-mod.jar --libraries path/to/java-libraries \
  --java-home path/to/jdk-21 --output build/parity-bundle

uv run --locked --project tools python -m rtx_tools.parity run \
  --bundle build/parity-bundle --runtime path/to/native-sdk/libremix.so \
  --java path/to/java-21/bin/java --output build/parity-linux
```

On Windows, copy the frozen bundle and invoke its PowerShell runner:

```powershell
.\Run-Windows.ps1 -Runtime 'path\to\original-sdk\d3d9.dll' -Java 'path\to\java-21\bin\java.exe'
```

Windows works in a fresh local application-data directory and exports captures
and logs into the bundle's `results/windows-*` directory. Driver caches stay in
the local work directory, so a bundle on removable storage does not receive a
separate driver cache for every scene.

Use a matching JNI bridge beside each runtime. Native SDK dependencies must stay
beside their API library. The Windows original and a changed Windows build are
separate reference targets; check their recorded runtime hashes when identifying
the result. No SDK is installed or replaced by a runner.

Each run gets a new work directory with its own `rtx.conf` and isolated Remix
persistent-settings directory. Requested options are reapplied after hardware
preset initialization and their resolved values are checked before scene
creation and after every scene.
Automation mode suppresses the first-use guide; the UI is kept closed. Black,
illuminated and primary-color controls reject a missing or obstructed render.
The platform-specific window/shutdown order preserves original Windows teardown
behavior. A successful result records both completed capture and completed
cleanup. Per-scene logs and metadata are retained under each result. A scene
timeout stops its child process; the runners also bound the whole suite and
terminate its process tree on timeout. Existing output directories are rejected.

Bring the Windows `result` directory back and compare it to Linux:

```sh
uv run --locked --project tools python -m rtx_tools.parity compare \
  path/to/windows/result build/parity-linux/result --output build/parity-comparison
```

Comparison rejects incomplete captures, changed images, different probe/API or
configuration identities, effective settings, extents, setup histories, frame
counts and scene selections. Fixed frame counts alone do not control time-dependent accumulation;
captures from an earlier bundle without the fixed time step are separate inputs.
Captures that declare the setup phase must record all four empty frames,
disabled upscaling and the fixed time step before scene creation. Legacy captures
without setup metadata remain readable for historical analysis, but cannot be
compared to captures declaring a setup phase.
Per-scene PNG strips show reference, candidate and the absolute
difference amplified eight times. `comparison.json` reports mean, 95th percentile
and maximum absolute channel error on the 0..255 scale, changed-pixel fraction
and full-image mean RGB. It records measurements without inventing pass thresholds.
Run twice on the same system to measure temporal variation before interpreting
cross-platform differences.

For a PBR discrepancy, the same bundle can capture the four PBR cases through
the intermediate views listed in `views.xml`:

```sh
uv run --locked --project tools python -m rtx_tools.parity run \
  --bundle build/parity-bundle --runtime path/to/native-sdk/libremix.so \
  --java path/to/java-21/bin/java --output build/parity-normals \
  --view shading_normal \
  --cases pbr_metal_smooth pbr_metal_rough pbr_normal_flat pbr_normal_tilted
```

On Windows, `Run-PBR-Audit-Windows.ps1 -Runtime ... -Java ...` runs all sixteen
views: final output, geometry and shading normals, diffuse and specular albedo,
roughness, direct/indirect diffuse/specular light before and after denoising,
and white/blue-noise controls for frame-seed comparisons.
Each view uses fresh processes and isolated settings. Normal and material views
request the first-GBuffer-hit setting. Signed normals use a linear -1..1 range,
albedo and roughness use 0..1, and radiance views use a linear 0..8 range.
These are 8-bit visualizations, so HDR values outside the displayed range clip.
They locate stages of a discrepancy rather than preserving full-precision GPU
buffers. View indices must match the runtime's debug-view definitions; sharing
an option name alone does not establish that an index has the same meaning.

`observe-options.txt` selects read-only option snapshots after startup, after
the empty setup phase and after capture. They include derived quality, sampling,
denoising and post-processing
defaults. An unregistered option is recorded explicitly; other API errors fail
the capture. The comparator reports differing observations separately from
requested input options, without changing either. Observation-list and view-map
hashes are part of the test identity. This diagnostic never changes the game's
settings or replaces a runtime.
The Windows audit also saves a view-to-capture index and before/after hashes of
SDK executables and global Remix settings, including after a failed view. A
changed file is reported without attempting to restore or replace it.

This version is distinct from earlier private probes; their results are not
interchangeable. Scene comparisons cover their recorded inputs. They do not
establish complete feature parity, Minecraft stability or hardware compatibility.
