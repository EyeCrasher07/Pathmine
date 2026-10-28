# Pathmine
Path-traced Minecraft Java through NVIDIA RTX Remix

Pathmine preserves NVIDIA's renderer and Minecraft scene capture while adding a
native Linux runtime. The current target is **Minecraft Java 1.21.1 with Fabric**.
The Linux x86_64 port is experimental; the original Windows release is the visual
comparison reference. Support for other game versions and loaders is future work.

This is an independent project, not an official NVIDIA release. Complete visual
parity, long-session stability and broad hardware support are still being tested.
[Current scope and known limitations](docs/NATIVE_LINUX_PORT.md) distinguish
verified behavior from unfinished work.

## Install

Packaged preview builds are distributed through [GitHub Releases](https://github.com/EyeCrasher07/Pathmine/releases).
Choose the JAR for your operating system, install Fabric for Minecraft Java
1.21.1 and the matching Fabric API, and place both JARs in the instance's `mods/`
directory. Use one Pathmine JAR per instance. Its native renderer and runtime
resources are bundled and extracted on first launch. Any compatible launcher
can run it. See [playing and troubleshooting](docs/PLAYING.md) for details.

Linux x86_64 preview packages require glibc 2.36 or newer and a compatible
NVIDIA Vulkan ray-tracing driver. Windows x86_64 uses the pinned NVIDIA runtime.
Preview builds retain the experimental scope and limitations described above.

## Build

Clone the source and pinned public renderer dependencies:

```sh
git clone --recurse-submodules https://github.com/EyeCrasher07/Pathmine.git
cd Pathmine
```

Use JDK 21 and set `JAVA_HOME` to that JDK. On Linux, install CMake, Ninja, a C++17 compiler and OpenGL/GLX/EGL
development libraries, then build the lightweight mod and JNI tests:

```sh
./gradlew build testNativeLinux64 -Parch=linux64
```

This creates `build/libs/pathmine-*.jar`; it does **not** contain a renderer.
Build the native runtime with the pinned dependencies using
[runtime/native/README.md](runtime/native/README.md), then stage its installed
`remix-sdk/` directory as `remix-runtime/linux64/`. Stage the two runtime DDS
textures pinned in `packman/textures.packman.xml` as `remix-runtime/textures/`.
The [native port guide](docs/NATIVE_LINUX_PORT.md) covers the integration boundary.

The native Packman bootstrap can fetch those textures from the repository root:

```sh
bash runtime/scripts-common/packman/packman pull packman/textures.packman.xml
```

```sh
./gradlew package -Parch=linux64
```

On Windows, use Visual Studio C++ build tools and JDK 21:

```powershell
.\gradlew.bat build test
.\gradlew.bat package -Parch=win64
```

The Windows build fetches the original pinned NVIDIA runtime through public
Packman. Its source base differs from the Linux port; see the dependency lock
and port guide before making parity claims. Building the Windows renderer from
source is described in [runtime/README.md](runtime/README.md).

Distribution JARs are written to `build/distributions/`. Install a packaged JAR
and the matching Fabric API into a Fabric 1.21.1 instance using your preferred
launcher. The mod extracts its bundled native payload on first launch. Prism is
used by the optional developer tools, not required by the mod.

The `package-windows` workflow builds and verifies a Windows x64 distribution
on `release/<version>` branches or `v<version>` tags and retains the JAR and
its SHA-256 checksum as build artifacts. It can also be run manually with a
version after the workflow reaches the default branch. Linux distribution
builds must compile both the renderer and JNI bridge against the ABI baseline
in `runtime/native/dependencies.lock.json`; a current-host JNI build alone
does not establish that baseline. Verify the extracted payload with
`runtime/native/check_runtime.py` before publishing it. Release assets are
published after their package and platform checks, separately from source PR
integration.

## Source layout

| Path | Purpose |
| --- | --- |
| `src/` | Minecraft integration, resources and JVM tests |
| `rtxremix-java/` | Java API facade and Windows/Linux JNI bridge |
| `runtime/` | NVIDIA renderer sources, shaders, platform port and native tests |
| `gradle/`, `build.gradle` | Pinned Gradle wrapper and mod build |
| `packman/` | Public SDK and runtime texture package pins |
| `packaging/` | Installer, runtime defaults, material profiles and licenses |
| `tools/` | Developer launcher, parity probes, benchmarks and repository checks |
| `docs/` | Player, resource-pack and development guides |

## Development checks

```sh
python3 tools/check_repository.py
./gradlew build testNativeLinux64 -Parch=linux64
uv run --locked --project tools rtx check --tests
```

Native renderer and GPU checks are documented in
[runtime/native/README.md](runtime/native/README.md). Stop clients using a staged
runtime before rebuilding or replacing it. Hosted CI checks the repository,
developer tools, mod and JNI boundary; it does not establish GPU or gameplay parity.

Before publishing a branch, install [no-mistakes](https://github.com/kunchenguid/no-mistakes),
run `no-mistakes init` once, commit your changes, then use:

```sh
no-mistakes axi run --intent "Describe the requested change and its constraints"
```

The repository configuration supplies repeatable checks after it is merged into
the default branch. no-mistakes deliberately does not trust executable settings
introduced only by an unmerged branch. Review findings and CI results before
merging. Keep fixes as additional commits and never force-push a shared branch.

## Guides

- [Playing and troubleshooting](docs/PLAYING.md)
- [Getting started with the developer tools](docs/GETTING_STARTED.md)
- [Settings](docs/SETTINGS.md), [commands and controls](docs/IN_GAME_GUIDE.md)
- [Resource packs](docs/RESOURCE_PACKS.md), [camera paths](docs/CAMERA_PATHS.md)
- [Contributing](CONTRIBUTING.md) and [issue tracker](https://github.com/EyeCrasher07/Pathmine/issues)

## License and credits

The repository's original [GPL-3.0 license](LICENSE) is preserved. Upstream
components keep their copyright notices and applicable licenses: the Minecraft
mod and Gradle wrapper derive from Apache-2.0 sources
([license text](LICENSES/Apache-2.0.txt)); the renderer includes zlib and MIT
components ([runtime/LICENSE](runtime/LICENSE), [runtime/LICENSE-MIT](runtime/LICENSE-MIT)).
[NOTICE](NOTICE), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and component
license files record the remaining terms. Vendor SDKs and optional downloaded
assets retain their own terms; the root license does not replace them.

Source bases and selective NVIDIA fixes are recorded in
[runtime/native/dependencies.lock.json](runtime/native/dependencies.lock.json).
Thanks to NVIDIA, the DXVK authors and the contributors to these upstream projects.

NOT AN OFFICIAL MINECRAFT PRODUCT. NOT APPROVED BY OR ASSOCIATED WITH MOJANG OR
MICROSOFT. Minecraft, NVIDIA, GeForce and RTX are trademarks of their respective owners.
