# Playing

This guide covers playing Pathmine from a packaged preview JAR on Windows or
native Linux.

## What You Need

- Minecraft Java Edition 1.21.1, on a Microsoft account that owns the game.
- A 64-bit Java 21 runtime, Fabric Loader, and Fabric API for that game version.
  The [Fabric metadata](../src/main/resources/fabric.mod.json) records the
  Loader and Fabric API minimum versions.
- For Windows: Windows 10 or 11 on x86_64, and a GeForce RTX 30-series or newer
  GPU with NVIDIA driver 616.92 or newer.
- For Linux: an x86_64 distribution meeting the `linuxDistribution` ABI
  baseline in the [dependency lock](../runtime/native/dependencies.lock.json),
  and an NVIDIA Vulkan ray-tracing driver. The runtime runs natively without Wine.
- An internet connection to download the mod and game dependencies.
- 16 GB of system memory is recommended.

The [native port guide](NATIVE_LINUX_PORT.md) records the tested Linux hardware
and remaining compatibility and visual-parity limits.

## Before You Start

This is a preview. The mod began as a technology showcase and was tuned against
one showcase world, built for this project by
[BlockWorks](https://www.blockworks.uk/), so the places that world visits are
where it looks and runs best. It ships with
[known issues](https://github.com/EyeCrasher07/Pathmine/issues),
and there are bugs yet to be found. Two things to know before a long session:

- **Other mods.** It has had limited testing alongside other mods, and mods
  that change rendering or chunk loading are likely to conflict with it.
- **Scope.** It targets Minecraft 1.21.1 only. Blocks, mobs, dimensions, and
  situations the showcase never reaches have had less care, and some will look
  wrong or run slowly.

When something breaks, [report it](#reporting-a-bug); that is how it gets
fixed.

## Download

See [Install](../README.md#install) for preview downloads and operating-system
selection. Source builders can follow the [build guide](../README.md#build).

## Which Launcher

Use any launcher that can run a Fabric instance with the requirements above.
The optional Windows archive installer can configure Prism or the Microsoft
launcher; its [installation guide](../packaging/INSTALL.md#detailed-installation)
explains where each keeps the game, worlds, and settings.

## Install

For a Windows installer archive built from source, follow
[packaging/INSTALL.md](../packaging/INSTALL.md#detailed-installation).

### Installing Without The Installer

Follow [Install](../README.md#install) to add the preview JAR to a Fabric
instance in your launcher. A source-built Windows archive also carries its
packaged JAR in `mods/`.

Optionally give the instance the JVM arguments the installer would set,
listed as `RECOMMENDED_JVM_ARGS` in
[`packaging/installer.ps1`](../packaging/installer.ps1).

The archive installer's Upgrade, Repair, and Uninstall modes do not manage a
manual install. See [Updating](#updating) and [Uninstalling](#uninstalling).

## First Launch

The first launch of a new version is slower than usual. The mod unpacks the
runtime into the game folder before the game window appears, which can look
like the game has frozen, especially on a hard drive. It happens once per
version.

Once you are in a world, the renderer compiles shaders and draws nothing until
it finishes. A dark, flat, or half-built scene right after is expected. Let it
settle before judging the picture.

The baseline PBR textures ship inside the mod JAR, so no separate baseline
pack needs enabling. User packs override the built-in baseline.

## Check It Worked

Type `/rtx status` in chat. Check these two lines:

```text
  Renderer Initialized: true
  SDK Available: true
```

Both `true` means the renderer is initialized and the SDK was found. The same
status output reports `Enabled (master)`, which says whether RTX rendering is
enabled. For a direct comparison, press `F8` to toggle path tracing, or type
`/rtx off`, look around, then `/rtx on`.

## Settings

The mod's own settings screen is in **Options**, then **RTX Remix Settings...**.
Its pages cover distance and framerate, sky and clouds, lighting, and
materials. The renderer's quality tier lives in Remix's own Graphics Settings
menu, which the **RTX Remix Graphics Settings** button on that screen opens.
See [Settings](SETTINGS.md) for how personal changes and map lighting fit
together.

**Alt+X** opens something different: the RTX Remix runtime's own developer
menu, aimed at people tuning the renderer itself. Anything you change there is
saved by Remix and overrides the mod's settings until you undo it. The
**RTX Remix Graphics Settings** button opens the runtime's simpler menu the
same way. While either is open the game receives no keyboard or mouse input,
Escape included; close the menu with its own close button, or press Alt+X
again, to carry on playing. The runtime's First Use Guide opens at each launch
until you choose **Never Show This Again** in it. It holds input the same way,
so the title screen ignores clicks until you close the guide or press Alt+X.

## Multiplayer

Path tracing runs entirely on your own machine, and the `/rtx` commands and
keys change only what you see. The Copper Beacon is a real block, though: to
place one on a server, the server needs the mod installed as well. On a server:

- Zones you define with `/rtx zone` apply to your own client for that session
  only. In single-player they are saved into the world.
- The chunk pacing that keeps single-player frames steady while the world loads
  applies only to single-player, so frame times on a server can be less even.
- Multiplayer has had less testing than single-player.

## Crash Reports And Privacy

The mod itself collects no data. On Windows, the RTX Remix runtime can send
diagnostic data to NVIDIA, and its First Use Guide asks about it with two
checkboxes, both on by default:

- **Basic session and system information**, collected each time RTX Remix
  launches: the game name, the Remix version, your GPU model and driver, the
  Windows version, the amount of memory, and session configuration flags.
- **Technical crash information**, sent when RTX Remix crashes: the same system
  information plus the crash type and the GPU's diagnostic state. A crash report
  carries a memory snapshot of the crashed process and the runtime's
  `remix-dxvk.log`.

Closing the guide keeps the boxes as they are, so uncheck either one there to
turn it off. You can change both at any time in the Permissions tab of the
**RTX Remix Graphics Settings** menu.

Crash data waits under `rtx-remix\logs\.sentry-native\` in the game folder
until it is sent, and is deleted after a successful upload. NVIDIA uses the data
to find and fix problems and to decide what to work on, under the
[NVIDIA Privacy Policy](https://www.nvidia.com/en-us/about-nvidia/privacy-policy/).
The runtime's
[crash reporting documentation](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/documentation/SentryCrashReporting.md)
has the details.

The native Linux runtime is built with Sentry reporting disabled.

## Troubleshooting

| Symptom | What to do |
|---|---|
| The scene is dark, flat, or half-built on first entering a world | Wait for shader compilation to settle before judging the picture. |
| The first launch looks frozen before the window appears | The runtime may be unpacking after an install, update, or runtime restore. Hard drives take longer than SSDs. |
| Blocks look flat or plastic, like paper cutouts | Baseline PBR textures ship with the mod. Disable extra resource packs to check whether one overrides those textures or sidecars. |
| On Windows, the game closes during startup, and `latest.log` says `NVIDIA driver is below the minimum supported by RTX Remix; closing Minecraft` | Update your NVIDIA driver from the NVIDIA app or <https://www.nvidia.com/en-us/drivers/>, then relaunch. The mod closes Minecraft when the driver is too old for the runtime. |
| `/rtx status` reports `Renderer Initialized: false` | Look for `Failed to initialize Pathmine renderer` in `logs/latest.log` and read the error that follows. If it names a conf file, check that file; an existing file that fails to load is not automatically replaced. Include the logs when reporting the failure. |
| On a Windows laptop: the game runs but looks like ordinary Minecraft, or is extremely slow | The display is being driven by the built-in graphics chip. In Windows Settings, Display, Graphics, add `javaw.exe` and set it to High performance, then relaunch. |
| Very choppy at a high render distance | Lower Minecraft's render distance, or pick a lower tier in Options, RTX Remix Settings, RTX Remix Graphics Settings. |
| The game will not start at all | Launch the Fabric instance containing the Pathmine JAR and Fabric API, and confirm the launcher is set to Java 21. |
| A crash, a black screen, or wrong rendering with other mods installed | Remove the other mods and try again. Mods that change rendering or chunk loading are the likely conflict. If one mod turns out to be the cause, say which in your report. |

If none of that helps, open your instance's game folder in the launcher and
read `logs/latest.log`. Windows archive installer defaults are:

- Prism, when the installer downloaded a portable copy:
  `%LOCALAPPDATA%\RtxRemixMinecraft\PrismLauncher\instances\Pathmine\.minecraft\logs\latest.log`
- Prism, when you already had it: under your own Prism data folder, usually
  `%APPDATA%\PrismLauncher\instances\Pathmine\.minecraft\logs\latest.log`
- Microsoft launcher: `%APPDATA%\.minecraft\logs\latest.log`

Paste the matching Windows path into the File Explorer address bar to open it.

## Reporting a Bug

Open an issue on the project's
[issue tracker](https://github.com/EyeCrasher07/Pathmine/issues)
for anything about the mod: what
is in the world and how it looks, the settings screen, `/rtx` commands and
keys, lighting profiles, the native Linux port, the installer, and crashes.
For Windows, denoiser, DLSS, or
frame-generation artifacts, Remix's own menus (Alt+X and RTX Remix Graphics
Settings), and anything that also happens in another RTX Remix game belong to
the runtime. Report those on the
[RTX Remix tracker](https://github.com/NVIDIAGameWorks/rtx-remix/issues/new?template=runtime_bug_report.yml),
say you are playing Pathmine, and give the `DXVK_Remix:` line
from the top of `rtx-remix\logs\remix-dxvk.log` as the version. If you are not
sure which it is, report it here and it will be moved.

Rendering problems are hard to act on without specifics, so include all of the
following:

| What to include | Where to find it |
|---|---|
| The mod file name | The `mods/` folder in the game directory. The name ends in a build stamp, which identifies the exact build. |
| Which launcher you used | The launcher and instance you started. |
| Your GPU and driver version | The NVIDIA app, or `nvidia-smi` in a terminal. |
| Whether a laptop display is driven by the built-in graphics chip | On Windows, check Settings, Display, Graphics. |
| Your operating system and architecture | Include the Linux distribution/version and `uname -m` output, or the Windows version and `systeminfo \| findstr "System Type"` output. |
| Which resource packs are on | Options, Resource Packs. Material bugs depend on this. |
| The output of `/rtx status` | Type it in chat and copy the lines. |
| `latest.log` and `remix-dxvk.log` | `latest.log` is at the paths above, and `remix-dxvk.log` is in `rtx-remix/logs/` in the same game folder. Attach the files rather than pasting part of them. |
| For a crash, the crash files | Zip whatever the game folder has of `crash/`, `crash-reports/`, and `hs_err_pid*.log`, and attach the zip. |
| A screenshot of the problem | F2 saves one to the `screenshots/` folder. |
| For a visual bug, a matching pair | Take the shot, type `/rtx off`, take a second shot from the same spot, then `/rtx on`. The pair shows what changed. |

## Updating

Stop the game, download the updated preview JAR for your operating system
through [Install](../README.md#install), and replace the old Pathmine JAR in
the same instance. Keep only one Pathmine JAR installed. For a Windows archive
installation, follow the [installer's update guide](../packaging/INSTALL.md#updating).
Your worlds, settings, and any lighting profiles you have edited are kept,
and profiles added by the new release are placed beside them.
To restore a shipped profile's default, delete that profile file and relaunch.
Missing shipped files under `rtx-remix/` and a missing `rtx.conf` return on
the next launch even when `.rtx-remix-version` matches. Existing profiles,
including edited files, are never overwritten. Deleting `remix-sdk/` or its
runtime library (`d3d9.dll` on Windows or `libremix.so` on Linux) also triggers
recovery on the next launch. Deleting `.rtx-remix-version` forces full
re-extraction of the runtime and `rtx.conf`, but still preserves existing profiles.

## Uninstalling

For a manual install, remove the Pathmine JAR from the instance. The
[uninstall file list](../packaging/INSTALL.md#uninstall) identifies the extracted
runtime files and the worlds, settings, and authored profiles to preserve.
That guide also covers uninstalling a Windows archive installation and its
optional Prism cleanup, which deletes worlds and authored profiles inside
the selected instance or portable directory.

## Going Further

| You want to | Read |
|---|---|
| Configure the renderer | [Settings](SETTINGS.md) |
| Look up a command, a key, or a mod-added block | [In-Game Guide](IN_GAME_GUIDE.md) |
| Record and replay a camera flythrough | [Camera Paths](CAMERA_PATHS.md) |
| Give your own map its own lighting | [Gameplay-driven profiles](../packaging/rtx-remix/mods/minecraft-logic/README.md) |
| Make a texture pack that this renderer understands | [Resource Packs](RESOURCE_PACKS.md) |
