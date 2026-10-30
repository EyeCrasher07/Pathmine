# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Install a built development mod into an existing Fabric game directory."""

from __future__ import annotations

import json
import os
import re
import shutil
from pathlib import Path

from .benchmark import util

# The runtime textures the mod loads from <gameDir>/textures/. build.gradle checks the same files after every pull.
RUNTIME_TEXTURE_FILES = ("overcast_thunderstorm_bc6u.dds", "water_normals_flipbook.dds")


def runtime_source(project_dir: Path, arch: str) -> Path:
    """The runtime directory install uses, with relative overrides anchored at the checkout."""
    return (project_dir / (os.environ.get("REMIX_BUILD") or f"remix-runtime/{arch}")).absolute()


def _stage_runtime(source: Path, bridge: Path, destination: Path, arch: str) -> None:
    # A game directory may point at a shared SDK cache. Detach the link before
    # reading its marker so even an unchanged SDK receives a private bridge copy.
    if destination.is_junction():
        destination.rmdir()
    elif destination.is_symlink():
        destination.unlink()

    marker = destination / ".install-marker"
    runtime_name = "libremix.so" if arch == "linux64" else "d3d9.dll"
    stamp = (source / runtime_name).stat()
    marker_value = f"{arch};{source};{stamp.st_size} {stamp.st_mtime_ns}"
    if arch == "linux64":
        marker_value += ";" + ";".join(
            f"{path.relative_to(source)}:{path.stat().st_size}:{path.stat().st_mtime_ns}"
            for path in sorted(source.rglob("*"))
            if path.is_file()
        )
    current = marker.read_bytes().split(b"\n", 1)[0].rstrip(b"\r") if marker.is_file() else b""
    if not os.environ.get("REMIX_FORCE_SDK_COPY") and current == marker_value.encode("utf-8"):
        print(f"remix-sdk already staged for {arch} from {source}; skipping SDK copy.")
    else:
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(source, destination)
        marker.write_text(marker_value + "\n", encoding="utf-8")
    shutil.copy2(bridge, destination / bridge.name)


def _migrate_linux_resource_packs(game_dir: Path) -> None:
    legacy_name = "rtx-remix-labpbr"
    legacy = game_dir / "resourcepacks" / legacy_name
    removable = not legacy.exists()
    if (
        legacy.is_dir()
        and not legacy.is_symlink()
        and not (legacy / ".rtx-remix-world-pack").exists()
        and (legacy / "BLOCK_TEXTURES.txt").is_file()
    ):
        try:
            metadata = json.loads((legacy / "pack.mcmeta").read_text(encoding="utf-8-sig"))
        except OSError, ValueError:
            metadata = None
        removable = metadata == {"pack": {"pack_format": 34, "description": "RTX Remix LabPBR Development Pack"}}
        if removable:
            shutil.rmtree(legacy)
    options = game_dir / "options.txt"
    if not options.is_file():
        return
    original = options.read_bytes()
    text = original.decode("utf-8-sig")
    match = re.search(r"(?m)^resourcePacks:[^\r\n]*", text)
    packs = json.loads(match[0].split(":", 1)[1]) if match else []
    ordered = [
        "vanilla",
        "fabric",
        *(
            name
            for name in packs
            if name not in {"vanilla", "fabric"} and (not removable or name != f"file/{legacy_name}")
        ),
    ]
    line = "resourcePacks:" + json.dumps(ordered, separators=(",", ":"))
    newline = "\r\n" if "\r\n" in text else "\n"
    updated = (
        text[: match.start()] + line + text[match.end() :]
        if match
        else text + (newline if text and not text.endswith("\n") else "") + line + newline
    )
    if updated != text:
        temporary = options.with_suffix(".txt.tmp")
        temporary.write_bytes(
            (b"\xef\xbb\xbf" if original.startswith(b"\xef\xbb\xbf") else b"") + updated.encode("utf-8")
        )
        temporary.replace(options)


def install_development_build(project_dir: Path, *, game_dir: Path | None = None, arch: str | None = None) -> int:
    """Stage built artifacts, returning 1 on missing prerequisites or a failed migration.

    Relative game and REMIX_BUILD paths are anchored at the checkout. This does
    not build the mod, install Fabric, enable user packs, or launch the game.
    Filesystem failures propagate to the CLI's error handler.
    """
    selected_arch = arch or os.environ.get("REMIX_ARCH")
    if not selected_arch:
        selected_arch = (
            "linux64"
            if os.name != "nt"
            else ("arm64" if os.environ.get("PROCESSOR_ARCHITECTURE", "").lower() == "arm64" else "win64")
        )
    if selected_arch.lower() not in {"win64", "arm64", "linux64"}:
        print(f'ERROR: REMIX_ARCH must be win64, arm64 or linux64, got "{selected_arch}".')
        return 1
    selected_arch = selected_arch.lower()

    if game_dir is None:
        appdata = os.environ.get("APPDATA")
        if not appdata:
            print("ERROR: APPDATA is not set; pass --game-dir DIR.")
            return 1
        game_dir = Path(appdata) / ".minecraft"
    game_dir = (project_dir / game_dir).absolute()
    source = runtime_source(project_dir, selected_arch)
    native_linux = selected_arch == "linux64"
    bridge = (
        project_dir / "build" / "native" / selected_arch / ("libremixbridge.so" if native_linux else "remixbridge.dll")
    )
    runtime_name = "libremix.so" if native_linux else "d3d9.dll"
    if not (source / runtime_name).is_file():
        print(f'ERROR: {runtime_name} not found in "{source}".')
        print(f"       Stage one with tools\\rtx.bat sdk local <runtime dir> --arch {selected_arch}.")
        return 1
    if not bridge.is_file():
        task = (
            "compileNativeLinux64"
            if native_linux
            else ("compileNativeArm64" if selected_arch == "arm64" else "compileNativeWin64")
        )
        print(f'ERROR: JNI bridge not found at "{bridge}".')
        print(f"       Build it with .\\gradlew.bat {task}.")
        return 1
    jars = (
        jar
        for jar in (project_dir / "build" / "libs").glob("pathmine-*.jar")
        if jar.is_file() and "-sources" not in jar.name.lower()
    )
    mod_jar = max(jars, key=lambda jar: jar.stat().st_mtime_ns, default=None)
    if mod_jar is None:
        print("ERROR: no mod jar found in build\\libs\\.")
        return 1
    textures = project_dir / "remix-runtime" / "textures"
    if not textures.is_dir():
        print(f'ERROR: runtime textures not found at "{textures}".')
        print("       Pull them with .\\gradlew.bat fetchRuntimeTextures.")
        return 1
    unusable = [
        name
        for name in RUNTIME_TEXTURE_FILES
        if not (textures / name).is_file() or (textures / name).stat().st_size == 0
    ]
    if unusable:
        print(f'ERROR: runtime textures at "{textures}" are missing or empty: {", ".join(unusable)}.')
        print("       Run .\\gradlew.bat fetchRuntimeTextures; when it reports the same files, delete the folder it")
        print("       names and run it again, and Packman downloads the package afresh.")
        return 1

    mods = game_dir / "mods"
    mods.mkdir(parents=True, exist_ok=True)
    for pattern in ("pathmine.jar", "pathmine-*.jar", "rtx-remix-renderer.jar", "rtx-remix-renderer-*.jar"):
        for previous in mods.glob(pattern):
            if previous.is_file():
                previous.unlink()
    shutil.copy2(mod_jar, mods / "pathmine.jar")
    if not any(mods.glob("fabric-api-*.jar")):
        print(f"WARNING: no fabric-api jar in {mods}.")
        print("         The mod requires fabric-api >=0.165.0+26.3 (https://modrinth.com/mod/fabric-api).")

    _stage_runtime(source, bridge, game_dir / "remix-sdk", selected_arch)
    packaging = project_dir / "packaging"
    shutil.copy2(packaging / "rtx.conf", game_dir / "rtx.conf")
    # Shipped profiles are refreshed; extra author-created files stay intact.
    shutil.copytree(packaging / "rtx-remix", game_dir / "rtx-remix", dirs_exist_ok=True)
    shutil.copytree(textures, game_dir / "textures", dirs_exist_ok=True)
    if native_linux:
        _migrate_linux_resource_packs(game_dir)
        print(f"Mod jar: {mod_jar}\nRuntime: {source} [{selected_arch}]\nInstalled to {game_dir}")
        return 0
    migration = util.run(
        [
            "powershell",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            packaging / "migrate_resource_packs.ps1",
            "-McDir",
            game_dir,
            "-LegacyPackName",
            "rtx-remix-labpbr",
        ],
        cwd=project_dir,
        check=False,
    )
    if migration.returncode != 0:
        print(f"ERROR: resource-pack migration failed ({migration.returncode}).")
        return 1
    print(f"Mod jar: {mod_jar}")
    print(f"Runtime: {source} [{selected_arch}]")
    print(f"Installed to {game_dir}")
    return 0
