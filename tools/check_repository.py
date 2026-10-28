#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only

"""Check the actual Git file list; optionally run the mod/tooling build gates."""

import argparse
import configparser
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tomllib
import xml.etree.ElementTree as ET
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
PRIVATE_OR_GENERATED = {
    ".agents",
    ".sober",
    ".no-mistakes",
    ".git",
    ".gradle",
    ".idea",
    ".vscode",
    ".venv",
    "__pycache__",
    ".pytest_cache",
    ".ruff_cache",
    "node_modules",
    "build",
    "dist",
    "run-prism",
    "logs",
}
LOCAL_NAMES = {
    "accounts.json",
    "launcher_accounts.json",
    "config.local.json",
    "gametargets.conf",
    "CMakeCache.txt",
    "CMakeUserPresets.json",
    "compile_commands.json",
    ".env",
}
BINARY_OUTPUTS = {".class", ".dll", ".exe", ".so", ".a", ".o", ".obj", ".pdb", ".zip", ".jar"}
DIAGNOSTICS = {".log", ".jfr", ".heapdump", ".tmp", ".bak", ".dxvk-cache"}
GPL_SHA256 = "3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986"


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def check():
    errors = []
    files = {}
    for entry in git("ls-files", "--stage", "-z").split(b"\0"):
        if not entry:
            continue
        metadata, raw_path = entry.split(b"\t", 1)
        mode, revision, stage = metadata.decode("ascii").split()
        name = raw_path.decode("utf-8")
        path = PurePosixPath(name)
        if stage != "0":
            errors.append(f"Unresolved Git conflict: {name}")
        if set(path.parts) & PRIVATE_OR_GENERATED or path.name in LOCAL_NAMES or path.parts[0] == "remix-runtime":
            errors.append(f"Private or generated path is tracked: {name}")
        if path.name.startswith(".env.") and path.name != ".env.example":
            errors.append(f"Local environment file is tracked: {name}")
        if path.suffix.lower() in BINARY_OUTPUTS | DIAGNOSTICS and name != "gradle/wrapper/gradle-wrapper.jar":
            errors.append(f"Build output or diagnostic is tracked: {name}")
        if mode == "160000":
            files[name] = (mode, revision)
            continue
        if mode not in ("100644", "100755"):
            errors.append(f"Unexpected Git mode {mode}: {name}")
            continue
        disk_path = ROOT / name
        if not disk_path.is_file() or disk_path.is_symlink():
            errors.append(f"Missing file or unsupported symlink: {name}")
            continue
        data = disk_path.read_bytes()
        if len(data) > 10 * 1024 * 1024:
            errors.append(f"File exceeds 10 MiB; review storage/LFS needs: {name}")
        if data.startswith(b"version https://git-lfs.github.com/spec/"):
            errors.append(f"Unexpected LFS pointer: {name}")
        try:
            if path.suffix == ".json":
                json.loads(data)
            elif path.suffix == ".toml":
                tomllib.loads(data.decode("utf-8"))
            elif path.suffix == ".xml":
                ET.fromstring(data)
        except (ValueError, ET.ParseError) as error:
            errors.append(f"Invalid configuration {name}: {error}")
        files[name] = (mode, revision)

    if hashlib.sha256((ROOT / "LICENSE").read_bytes()).hexdigest() != GPL_SHA256:
        errors.append("The existing repository GPL-3.0 license was changed")
    if files.get("gradlew", (None,))[0] != "100755":
        errors.append("The POSIX Gradle wrapper must be executable in Git")
    modules = configparser.ConfigParser()
    modules.read(ROOT / ".gitmodules")
    declared = set()
    for section in modules.sections():
        path = modules[section]["path"]
        declared.add(path)
        if not modules[section]["url"].startswith("https://github.com/"):
            errors.append(f"Submodule must have a public HTTPS origin: {path}")
        if files.get(path, (None,))[0] != "160000":
            errors.append(f"Submodule has no pinned Git commit: {path}")
    gitlinks = {path for path, (mode, _) in files.items() if mode == "160000"}
    if gitlinks != declared:
        errors.append("Gitlinks and .gitmodules paths disagree")

    native = ROOT / "runtime/native"
    lock = json.loads((native / "dependencies.lock.json").read_text())
    api = lock["sources"]["minecraftApi"]
    if hashlib.sha256((ROOT / api["header"]).read_bytes()).hexdigest() != api["headerSha256"]:
        errors.append("Minecraft C API header does not match the dependency lock")
    for patch in lock["sources"]["nrd"]["patches"]:
        if hashlib.sha256((native / patch["file"]).read_bytes()).hexdigest() != patch["sha256"]:
            errors.append(f"Dependency patch checksum mismatch: {patch['file']}")
    for key in ("dlss", "rtxdi"):
        if files.get(f"runtime/submodules/{key}", (None, None))[1] != lock["sources"][key]["revision"]:
            errors.append(f"{key} Git pin does not match the native lock")

    if errors:
        print("\n".join(errors), file=sys.stderr)
        return False
    print(f"Repository checks passed: {len(files)} tracked entries, public dependency pins and intact licenses")
    return True


def build():
    windows = os.name == "nt"
    wrapper = ["cmd", "/d", "/c", "gradlew.bat"] if windows else ["./gradlew"]
    args = ["build", "test"] if windows else ["build", "testNativeLinux64", "-Parch=linux64"]
    subprocess.run([*wrapper, *args], cwd=ROOT, check=True)
    uv = shutil.which("uv")
    if not uv:
        raise SystemExit("Install uv >=0.13 to validate the locked Python tools")
    subprocess.run([uv, "run", "--locked", "--project", "tools", "rtx", "check", "--tests"], cwd=ROOT, check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build", action="store_true", help="also build/test the mod, JNI bridge and locked Python tools"
    )
    options = parser.parse_args()
    if not check():
        raise SystemExit(1)
    if options.build:
        build()
