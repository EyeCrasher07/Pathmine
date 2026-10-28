"""Check the ELF dependencies and ABI of an extracted native runtime."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


SYSTEM_LIBRARIES = {
    "ld-linux-x86-64.so.2", "libc.so.6", "libm.so.6", "libdl.so.2",
    "libpthread.so.0", "librt.so.1", "libstdc++.so.6", "libgcc_s.so.1",
    "libvulkan.so.1", "libGL.so.1", "libOpenGL.so.0", "libEGL.so.1",
    "libGLX.so.0", "libX11.so.6",
}
REQUIRED_LIBRARIES = {
    "libremix.so", "libremix-core.so", "libNRD.so", "libusd_ms.so", "libtbb.so.12",
}
REQUIRED_RESOURCES = {
    "usd/plugInfo.json", "usd/plugins/plugInfo.json",
    "usd/plugins/RemixCategories/libRemixCategories.so",
    "usd/plugins/RemixCategories/resources/plugInfo.json",
    "usd/plugins/RemixCategories/generatedSchema.usda",
    "usd/plugins/RemixParticleSystem/libRemixParticleSystem.so",
    "usd/plugins/RemixParticleSystem/resources/plugInfo.json",
    "usd/plugins/RemixParticleSystem/generatedSchema.usda",
}


def version(value):
    return tuple(map(int, value.split(".")))


def inspect_library(path, root, limits):
    problems = []
    with path.open("rb") as stream:
        header = stream.read(20)
    if header[:6] != b"\x7fELF\x02\x01" or header[18:20] != b"\x3e\x00":
        return {}, ["not a Linux x86_64 ELF library"]
    dynamic = subprocess.run(
        ["readelf", "--wide", "--dynamic", str(path)],
        check=True, capture_output=True, text=True, env=dict(os.environ, LC_ALL="C"),
    ).stdout
    versions = subprocess.run(
        ["readelf", "--wide", "--version-info", str(path)],
        check=True, capture_output=True, text=True, env=dict(os.environ, LC_ALL="C"),
    ).stdout
    needed = re.findall(r"\(NEEDED\).*?\[([^\]]+)\]", dynamic)
    paths = re.findall(r"\((?:RUNPATH|RPATH)\).*?\[([^\]]*)\]", dynamic)
    search = []
    for entry in [item for value in paths for item in value.split(":")]:
        if entry != "$ORIGIN" and not entry.startswith("$ORIGIN/"):
            problems.append(f"non-relocatable loader path: {entry!r}")
            continue
        directory = (path.parent / entry.removeprefix("$ORIGIN").lstrip("/")).resolve()
        if not directory.is_relative_to(root):
            problems.append(f"loader path escapes runtime: {entry}")
            continue
        search.append(directory)
    for name in needed:
        if "/" in name:
            problems.append(f"dependency contains a path: {name}")
        elif name not in SYSTEM_LIBRARIES and not any((folder / name).is_file() for folder in search):
            problems.append(f"dependency is not reachable within runtime: {name}")
    requirements = {}
    # Only imported symbol versions constrain the host; exported versions do not.
    needs = versions.partition("Version needs section")[2]
    if "GLIBC_PRIVATE" in needs:
        problems.append("requires private glibc symbols")
    for family, maximum in limits.items():
        values = set(re.findall(r"Name: " + family + r"_([0-9.]+)", needs))
        if values:
            requirements[family] = max(values, key=version)
            if version(requirements[family]) > version(maximum):
                problems.append(f"requires {family}_{requirements[family]}, release maximum is {maximum}")
    return {"needed": needed, "loader_paths": paths, "symbol_versions": requirements}, problems


def main():
    lock = json.loads(Path(__file__).with_name("dependencies.lock.json").read_text(encoding="utf-8"))
    baseline = lock["linuxDistribution"]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runtime", type=Path)
    parser.add_argument("--max-glibc", default=baseline["glibc"])
    parser.add_argument("--max-glibcxx", default=baseline["glibcxx"])
    parser.add_argument("--max-cxxabi", default=baseline["cxxabi"])
    parser.add_argument("--ngx-source", type=Path, required=True,
                        help="Original DLSS feature-library directory for byte identity checks")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    root = args.runtime.resolve()
    errors = []
    records = []
    for name in sorted(REQUIRED_LIBRARIES):
        if not (root / name).is_file():
            errors.append(f"missing library: {name}")
    for name in sorted(REQUIRED_RESOURCES):
        if not (root / name).is_file():
            errors.append(f"missing USD resource: {name}")
    for path in sorted(root.rglob("*")):
        if path.is_symlink() and not path.resolve().is_relative_to(root):
            errors.append(f"symlink escapes runtime: {path.relative_to(root)}")
            continue
        if not path.is_file() or ".so" not in path.name:
            continue
        record, problems = inspect_library(path, root, {
            "GLIBC": args.max_glibc, "GLIBCXX": args.max_glibcxx, "CXXABI": args.max_cxxabi,
        })
        record["file"] = str(path.relative_to(root))
        records.append(record)
        errors.extend(f"{record['file']}: {problem}" for problem in problems)
    for feature in ("dlss", "dlssd"):
        originals = list(args.ngx_source.glob(f"libnvidia-ngx-{feature}.so.*"))
        if len(originals) != 1:
            errors.append(f"expected exactly one original NGX {feature} library")
            continue
        original = originals[0]
        staged = root / original.name
        if not staged.is_file():
            errors.append(f"missing NGX feature library: {original.name}")
        else:
            with staged.open("rb") as stream:
                staged_hash = hashlib.file_digest(stream, "sha256").digest()
            with original.open("rb") as stream:
                if staged_hash != hashlib.file_digest(stream, "sha256").digest():
                    errors.append(f"NGX feature library was modified: {original.name}")
    result = {"runtime": str(root), "libraries": records, "errors": errors}
    if args.json:
        args.json.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    for error in errors:
        print(f"FAIL {error}")
    print(f"Native runtime distribution: {len(records)} ELF libraries, {len(errors)} errors")
    return bool(errors)


if __name__ == "__main__":
    raise SystemExit(main())
