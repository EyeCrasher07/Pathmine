# SPDX-FileCopyrightText: Copyright (c) 2026 EyeCrasher.
# SPDX-License-Identifier: Apache-2.0

"""Build, run and compare standalone API 0.16 renderer captures."""

import argparse
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from datetime import UTC, datetime
from pathlib import Path

import numpy as np
from PIL import Image

from .benchmark import util

LIBRARIES = (
    "joml-1.10.5.jar",
    "slf4j-api-2.0.9.jar",
    "lwjgl-3.3.3.jar",
    "lwjgl-glfw-3.3.3.jar",
    "lwjgl-opengl-3.3.3.jar",
    "lwjgl-natives-linux-3.3.3.jar",
    "lwjgl-glfw-natives-linux-3.3.3.jar",
    "lwjgl-opengl-natives-linux-3.3.3.jar",
    "lwjgl-natives-windows-3.3.3.jar",
    "lwjgl-glfw-natives-windows-3.3.3.jar",
    "lwjgl-opengl-natives-windows-3.3.3.jar",
)
IDENTITY_KEYS = ("suite.version", "probe.sha256", "api.sha256", "config.sha256", "capture.frames", "case.isolation")
SETUP_KEYS = ("capture.setup.frames", "capture.setup.scene")


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def read_properties(path: Path) -> dict[str, str]:
    root = ET.parse(path).getroot()
    if root.tag != "properties":
        raise ValueError(f"Expected Java properties XML: {path}")
    values = {}
    for entry in root.findall("entry"):
        key = entry.attrib["key"]
        if key in values:
            raise ValueError(f"Duplicate property {key}: {path}")
        values[key] = entry.text or ""
    return values


def write_properties(path: Path, values: dict[str, str]) -> None:
    root = ET.Element("properties")
    for key, value in sorted(values.items()):
        ET.SubElement(root, "entry", key=key).text = value
    header = b'<?xml version="1.0" encoding="UTF-8"?>\n'
    header += b'<!DOCTYPE properties SYSTEM "http://java.sun.com/dtd/properties.dtd">\n'
    path.write_bytes(header + ET.tostring(root, encoding="utf-8"))


def verify_bundle(bundle: Path) -> dict[str, str]:
    values = read_properties(bundle / "bundle.xml")
    for key, expected in values.items():
        if not key.startswith("sha256."):
            continue
        file = bundle / key.removeprefix("sha256.")
        if not file.resolve().is_relative_to(bundle.resolve()) or sha256(file) != expected:
            raise ValueError(f"Bundle checksum mismatch: {file}")
    for name in ("probe.jar", "api.jar", "rtx.conf", *[f"lib/{name}" for name in LIBRARIES]):
        if f"sha256.{name}" not in values:
            raise ValueError(f"Bundle manifest omits {name}")
    return values


def build_probe(artifact: Path, libraries: Path, java_home: Path, output: Path) -> None:
    if output.exists():
        raise ValueError(f"Use a new bundle directory: {output}")
    source = util.TOOLS_DIR / "parity"
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="parity-build-", dir=output.parent) as temporary:
        staging = Path(temporary) / "bundle"
        staging.mkdir()
        (staging / "lib").mkdir()
        for name in LIBRARIES:
            shutil.copy2(libraries / name, staging / "lib" / name)
        for name in (
            "RenderParityProbe.java",
            "rtx.conf",
            "views.xml",
            "observe-options.txt",
            "Run-Windows.ps1",
            "Run-PBR-Audit-Windows.ps1",
            "README.md",
        ):
            shutil.copy2(source / name, staging / name)
        shutil.copy2(util.PROJECT_DIR / "LICENSE", staging / "LICENSE")
        with zipfile.ZipFile(artifact) as jar, zipfile.ZipFile(staging / "api.jar", "w", zipfile.ZIP_DEFLATED) as api:
            names = [
                name
                for name in jar.namelist()
                if name.endswith(".class")
                and (
                    name.startswith("com/nvidia/rtxremix/")
                    and name.count("/") == 3
                    or name.startswith("com/nvidia/rtxremix/minecraft/gl/GlInterop")
                )
            ]
            if "com/nvidia/rtxremix/RemixAPI.class" not in names:
                raise ValueError("Artifact does not contain the Java API facade")
            if "com/nvidia/rtxremix/minecraft/gl/GlInterop.class" not in names:
                raise ValueError("Artifact does not contain the OpenGL interop implementation")
            for name in sorted(names):
                api.writestr(zipfile.ZipInfo(name), jar.read(name), compress_type=zipfile.ZIP_DEFLATED)
        classes = Path(temporary) / "classes"
        classpath = os.pathsep.join((str(staging / "api.jar"), str(staging / "lib" / "*")))
        subprocess.run(
            [
                str(java_home / "bin" / ("javac.exe" if os.name == "nt" else "javac")),
                "--release",
                "21",
                "-cp",
                classpath,
                "-d",
                str(classes),
                str(staging / "RenderParityProbe.java"),
            ],
            check=True,
        )
        with zipfile.ZipFile(staging / "probe.jar", "w", zipfile.ZIP_DEFLATED) as jar:
            for file in sorted(classes.rglob("*.class")):
                jar.writestr(
                    zipfile.ZipInfo(file.relative_to(classes).as_posix()),
                    file.read_bytes(),
                    compress_type=zipfile.ZIP_DEFLATED,
                )
        manifest = {"suite.version": "3", "api.source.sha256": sha256(artifact)}
        for file in sorted(staging.rglob("*")):
            if file.is_file():
                manifest["sha256." + file.relative_to(staging).as_posix()] = sha256(file)
        write_properties(staging / "bundle.xml", manifest)
        verify_bundle(staging)
        staging.rename(output)


def require_idle_gpu() -> None:
    if sys.platform != "linux":
        raise ValueError("Use the bundle's Run-Windows.ps1 on Windows")
    for proc in Path("/proc").iterdir():
        if not proc.name.isdecimal():
            continue
        try:
            if (proc / "exe").resolve().name != "java":
                continue
            command = (proc / "cmdline").read_bytes()
        except FileNotFoundError, PermissionError, ProcessLookupError:
            continue
        if any(
            name in command
            for name in (b"org.prismlauncher.EntryPoint", b"KnotClient", b"net.minecraft.client.main.Main")
        ):
            raise ValueError(f"Close Minecraft PID {proc.name} before running the GPU comparison probe")


def run_probe(bundle: Path, runtime: Path, java: Path, output: Path, cases: list[str], view: str = "final") -> None:
    require_idle_gpu()
    verify_bundle(bundle)
    output.mkdir(parents=True, exist_ok=False)
    shutil.copy2(bundle / "rtx.conf", output / "rtx.conf")
    env = dict(os.environ)
    env["XDG_CONFIG_HOME"] = str(output / "config")
    env["LOCALAPPDATA"] = str(output / "localappdata")
    for key in ("JAVA_TOOL_OPTIONS", "_JAVA_OPTIONS", "JDK_JAVA_OPTIONS"):
        env.pop(key, None)
    classpath = os.pathsep.join(str(bundle / name) for name in ("probe.jar", "api.jar", "lib/*"))
    command = [str(java), "-cp", classpath, "RenderParityProbe", str(runtime), str(output / "result")]
    if view != "final":
        command.append("--view=" + view)
    command.extend(cases)
    with (
        (output / "stdout.log").open("w") as stdout,
        (output / "stderr.log").open("w") as stderr,
        subprocess.Popen(command, cwd=output, env=env, stdout=stdout, stderr=stderr, start_new_session=True) as process,
    ):
        try:
            code = process.wait(timeout=600)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            raise
        if code:
            raise subprocess.CalledProcessError(code, command)
    load_capture(output / "result")


def load_capture(result: Path) -> tuple[dict[str, str], list[str]]:
    metadata = read_properties(result / "metadata.xml")
    if metadata.get("completed") != "true" or metadata.get("cleanup.completed") != "true":
        raise ValueError(f"Capture or cleanup is incomplete: {result}")
    for key in (*IDENTITY_KEYS, "runtime.sha256", "bridge.sha256", "os", "gl.renderer", "gl.version"):
        if not metadata.get(key):
            raise ValueError(f"Missing capture identity {key}: {result}")
    if metadata["suite.version"] != "3":
        raise ValueError("Only captures from suite version 3 can be compared")
    if metadata["case.isolation"] != "fresh-process":
        raise ValueError("Each scene must run in a fresh native runtime process")
    if metadata.get("option.rtx.upscalerType") != "0":
        raise ValueError("The baseline requires upscaling disabled in the effective runtime options")
    if any(key in metadata for key in SETUP_KEYS) or any(key.startswith("observed.settled.") for key in metadata):
        setup = {
            "capture.setup.frames": "4",
            "capture.setup.scene": "empty-camera-no-mesh-material-light",
            "observed.settled.rtx.upscalerType": "0",
            "observed.settled.rtx.timeDeltaBetweenFrames": "16",
        }
        for key, expected in setup.items():
            if metadata.get(key) != expected:
                raise ValueError(f"Incomplete or unsettled capture setup: {key}")
    width, height = int(metadata["capture.width"]), int(metadata["capture.height"])
    count = int(metadata["case.count"])
    if min(width, height, count, int(metadata["capture.frames"])) <= 0:
        raise ValueError("Capture extent, frame count and scene count must be positive")
    cases = [metadata[f"case.{index}"] for index in range(count)]
    if len(set(cases)) != count or any(not re.fullmatch(r"[a-z0-9_]+", case) for case in cases):
        raise ValueError("Capture case names must be unique safe filenames")
    for case in cases:
        path = result / f"{case}.png"
        if sha256(path) != metadata[f"image.{case}.sha256"]:
            raise ValueError(f"Capture image checksum mismatch: {case}")
        with Image.open(path) as image:
            if image.size != (width, height) or image.mode != "RGB":
                raise ValueError(f"Capture extent or color mode mismatch: {case}")
    return metadata, cases


def compare_captures(reference: Path, candidate: Path, output: Path) -> dict:
    a, a_cases = load_capture(reference)
    b, b_cases = load_capture(candidate)
    required = (*IDENTITY_KEYS, "capture.width", "capture.height")
    for key in required:
        if a[key] != b[key]:
            raise ValueError(f"Different test inputs: {key}")
    for key in (*SETUP_KEYS, "capture.view", "views.sha256", "observations.sha256"):
        if a.get(key) != b.get(key):
            raise ValueError(f"Different test inputs: {key}")
    a_options = {key: value for key, value in a.items() if key.startswith("option.")}
    b_options = {key: value for key, value in b.items() if key.startswith("option.")}
    if a_options != b_options:
        raise ValueError("Different effective renderer options")
    if a_cases != b_cases:
        raise ValueError("Different case selection or ordering")
    output.mkdir(parents=True, exist_ok=False)
    measurements = []
    for case in a_cases:
        with Image.open(reference / f"{case}.png") as image:
            left = np.array(image, dtype=np.int16)
        with Image.open(candidate / f"{case}.png") as image:
            right = np.array(image, dtype=np.int16)
        difference = np.abs(left - right)
        measurements.append(
            {
                "case": case,
                "mean_absolute_channel_error": float(difference.mean()),
                "p95_absolute_channel_error": float(np.percentile(difference, 95)),
                "max_channel_error": int(difference.max()),
                "different_pixel_fraction": float(np.any(difference != 0, axis=2).mean()),
                "reference_mean_rgb": left.mean(axis=(0, 1)).tolist(),
                "candidate_mean_rgb": right.mean(axis=(0, 1)).tolist(),
            }
        )
        strip = np.concatenate((left, right, np.minimum(difference * 8, 255)), axis=1).astype(np.uint8)
        Image.fromarray(strip).save(output / f"{case}.png")
    report = {
        "status": "measured",
        "scope": "Selected API scenes; measurements do not certify full visual or gameplay parity.",
        "image_columns": ["reference", "candidate", "absolute difference multiplied by 8"],
        "reference": a,
        "candidate": b,
        "measurements": measurements,
        "observation_differences": [
            {"key": key, "reference": a.get(key), "candidate": b.get(key)}
            for key in sorted(set(a) | set(b))
            if key.startswith("observed.") and a.get(key) != b.get(key)
        ],
    }
    (output / "comparison.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    build = sub.add_parser("build", help="freeze one portable probe bundle for both operating systems")
    build.add_argument("--artifact", type=Path, required=True)
    build.add_argument("--libraries", type=Path, required=True)
    build.add_argument("--java-home", type=Path, required=True)
    build.add_argument("--output", type=Path, required=True)
    run = sub.add_parser("run", help="capture scenes on native Linux without launching Minecraft")
    run.add_argument("--bundle", type=Path, required=True)
    run.add_argument("--runtime", type=Path, required=True)
    run.add_argument("--java", type=Path, required=True)
    run.add_argument("--output", type=Path)
    run.add_argument("--cases", nargs="*", default=[])
    run.add_argument("--view", default="final", help="diagnostic view from the frozen bundle's views.xml")
    compare = sub.add_parser("compare", help="measure captures with matching test identities and options")
    compare.add_argument("reference", type=Path)
    compare.add_argument("candidate", type=Path)
    compare.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == "build":
            build_probe(
                args.artifact.resolve(), args.libraries.resolve(), args.java_home.resolve(), args.output.resolve()
            )
            print(f"Frozen probe bundle: {args.output}")
        elif args.command == "run":
            output = args.output or args.bundle / "results" / datetime.now(UTC).strftime("linux-%Y%m%dT%H%M%S%f")
            run_probe(
                args.bundle.resolve(),
                args.runtime.resolve(),
                args.java.resolve(),
                output.resolve(),
                args.cases,
                args.view,
            )
            print(f"Capture completed: {output / 'result'}")
        else:
            report = compare_captures(args.reference, args.candidate, args.output)
            print(f"Measured {len(report['measurements'])} scenes: {args.output / 'comparison.json'}")
    except (OSError, ValueError, KeyError, ET.ParseError, subprocess.SubprocessError) as error:
        parser.exit(1, f"{error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
