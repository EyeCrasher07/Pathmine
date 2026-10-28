# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

import json
import os
import re
import shutil
import struct
import subprocess
from dataclasses import dataclass, field
from pathlib import Path, PureWindowsPath

import pytest

from rtx_tools import client
from rtx_tools.benchmark import util
from rtx_tools.client import LaunchBlocked, LaunchRequest


@pytest.fixture(autouse=True)
def no_cached_launcher(monkeypatch: pytest.MonkeyPatch) -> None:
    """Every test starts without a cached client launcher, so none reaches a real one an earlier test built."""
    monkeypatch.setattr(client, "_active", None)
    monkeypatch.setattr(
        util,
        "RUN_DIR",
        util.PRISM_DATA_DIR / "instances" / "rtx-remix-dev" / ".minecraft",
    )


def real_client_launch(args: object) -> str | None:
    """What a command line would start that no test may: Gradle's runClient or a Prism Launcher."""
    words = [str(arg) for arg in args] if isinstance(args, list | tuple) else str(args).split()
    names = [PureWindowsPath(word.strip('"')).name.lower() for word in words]
    if any(name in ("gradlew", "gradlew.bat") for name in names) and "runClient" in words:
        return "Gradle runClient"
    if names and names[0] in ("prismlauncher", "prismlauncher.exe"):
        return "Prism Launcher"
    return None


@pytest.fixture(autouse=True)
def no_real_client(monkeypatch: pytest.MonkeyPatch) -> None:
    """Fail a test the moment it would start a real game through util.run or subprocess, so a launch that skipped the
    active launcher fails here instead of spawning a client. A test that fakes either replaces its guard."""

    def refuse(args: object) -> None:
        if launch := real_client_launch(args):
            pytest.fail(f"a test tried to start {launch}: {args!r}")

    run = util.run

    def guarded_run(args, *rest, **kwargs):
        args = list(args)
        refuse(args)
        return run(args, *rest, **kwargs)

    class GuardedPopen(subprocess.Popen):
        def __init__(self, args, *rest, **kwargs) -> None:
            refuse(args)
            super().__init__(args, *rest, **kwargs)

    monkeypatch.setattr(util, "run", guarded_run)
    monkeypatch.setattr(subprocess, "Popen", GuardedPopen)


@pytest.fixture
def isolated_tools_config(tmp_path_factory: pytest.TempPathFactory, monkeypatch: pytest.MonkeyPatch) -> Path:
    """Opt-in shipped defaults with no developer-local state."""
    tools_dir = tmp_path_factory.mktemp("tools-config")
    shared = tools_dir / "config.json"
    shared.write_bytes(util.SHARED_CONFIG_PATH.read_bytes())
    monkeypatch.setattr(util, "SHARED_CONFIG_PATH", shared)
    monkeypatch.setattr(util, "TOOLS_DIR", tools_dir)
    return shared


def recommended_heap_mib() -> int:
    """The -Xmx in packaging/installer.ps1's recommended JVM arguments, in MiB, read without prism.py's parser, so
    tests follow whatever heap the project recommends."""
    text = (util.PROJECT_DIR / "packaging" / "installer.ps1").read_text(encoding="utf-8")
    recommended = re.search(r"\$script:RECOMMENDED_JVM_ARGS\s*=\s*'([^']+)'", text)
    assert recommended is not None
    sizes = re.findall(r"(?<!\S)-Xmx(\d+)([kKmMgG]?)(?!\S)", recommended.group(1))
    assert sizes, recommended.group(1)
    number, unit = sizes[-1]
    unit_bytes = {"": 1, "k": 1024, "m": 1024**2, "g": 1024**3}[unit.lower()]
    return -(-int(number) * unit_bytes // 1024**2)


def write_jdk(home: Path, *, version: str = "21.0.5", machine: int = 0x8664) -> Path:
    """A JDK home as the launcher reads one: its release file and a bin/javaw.exe whose PE header names `machine`
    (0x8664 x64, 0xAA64 ARM64)."""
    (home / "bin").mkdir(parents=True, exist_ok=True)
    (home / "release").write_text(f'IMPLEMENTOR="Test"\nJAVA_VERSION="{version}"\n', encoding="utf-8")
    header = bytearray(0x40)
    header[:2] = b"MZ"
    header[0x3C:0x40] = struct.pack("<I", 0x40)
    (home / "bin" / "javaw.exe").write_bytes(bytes(header) + b"PE\0\0" + struct.pack("<H", machine))
    return home


class FakeProcess:
    """A game process that has already exited cleanly."""

    pid: int = 0

    def poll(self) -> int | None:
        return 0

    def wait(self, timeout: float | None = None) -> int:
        return 0

    def terminate(self) -> None:
        pass

    def kill(self) -> None:
        pass


@dataclass
class FakeLauncher:
    """A client launcher that records what it is asked to do and never starts a game."""

    game_dir: Path
    name: str = "fake"
    block: LaunchBlocked | None = None
    prepared: list[tuple[str, bool]] = field(default_factory=list)
    requests: list[LaunchRequest] = field(default_factory=list)
    extra_log: str = ""
    sign_in_opened: bool = False
    description: str = "Fake launcher"

    def describe(self) -> str:
        return self.description

    def check(self, *, interactive: bool = True) -> None:
        if self.block is not None:
            # As PrismLauncher does: an interactive check that blocks opens sign-in.
            if interactive:
                self.open_sign_in()
            raise self.block

    def open_sign_in(self) -> None:
        self.sign_in_opened = True

    def prepare(self, arch: str, *, build: bool) -> None:
        self.prepared.append((arch, build))

    def start(self, request: LaunchRequest) -> FakeProcess:
        self.requests.append(request)
        return FakeProcess()

    def log_files(self, since_ns: int) -> list[Path]:
        if not self.extra_log:
            return []
        log = self.game_dir / "logs" / "fake.log"
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(self.extra_log, encoding="utf-8")
        return [log]


@pytest.fixture
def fake_launcher(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> FakeLauncher:
    """Install a FakeLauncher as the active client launcher; the test's end restores the launcher and RUN_DIR."""
    fake = FakeLauncher(game_dir=tmp_path / "game")
    monkeypatch.setattr(client, "_active", None)
    monkeypatch.setattr(util, "RUN_DIR", util.RUN_DIR)
    client.use(fake)
    return fake


@dataclass
class GradleProject:
    root: Path
    model: dict = field(default_factory=dict)
    benchmark_model: dict = field(default_factory=dict)

    def run(self, *args: str, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
        wrapper = self.root / ("gradlew.bat" if os.name == "nt" else "gradlew")
        command = [str(wrapper), "--console=plain", "-I", str(self.root / "probe.gradle"), *args]
        result = subprocess.run(command, cwd=self.root, env=env, capture_output=True, text=True, check=False)
        assert result.returncode == 0, result.stdout + result.stderr
        return result

    def read_model(self) -> dict:
        return json.loads((self.root / "build" / "publication-test-model.json").read_text(encoding="utf-8"))


@pytest.fixture(scope="session")
def gradle_project(tmp_path_factory: pytest.TempPathFactory) -> GradleProject:
    import zipfile

    root = tmp_path_factory.mktemp("synthetic-gradle-project")
    for name in (
        "build.gradle",
        "settings.gradle",
        "gradle.properties",
        "gradlew",
        "gradlew.bat",
        "gradle",
        "packaging",
        "src/main/resources",
        "rtxremix-java/native/build_native.bat",
        "rtxremix-java/native/remixbridge.cpp",
        "rtxremix-java/native/bridge_platform.h",
        "rtxremix-java/native/include/remix_c.h",
        "LICENSE",
        "LICENSES",
        "NOTICE",
        "THIRD_PARTY_NOTICES.md",
    ):
        source = util.PROJECT_DIR / name
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if source.is_dir():
            shutil.copytree(source, target)
        else:
            shutil.copy2(source, target)
    shutil.copy2(util.TOOLS_DIR / "tests" / "gradle_probe.gradle", root / "probe.gradle")
    subprocess.run(["git", "init", "--quiet", str(root)], check=True)
    subprocess.run(
        [
            "git",
            "-c",
            "user.name=Synthetic Fixture",
            "-c",
            "user.email=fixture@example.invalid",
            "-c",
            "commit.gpgsign=false",
            "commit",
            "--quiet",
            "--allow-empty",
            "-m",
            "Synthetic packaging fixture",
        ],
        cwd=root,
        check=True,
    )
    fixtures = root / "fixtures"
    fixtures.mkdir()
    with zipfile.ZipFile(fixtures / "synthetic-remapped.jar", "w") as jar:
        jar.writestr("META-INF/MANIFEST.MF", "Manifest-Version: 1.0\n\n")
        jar.writestr("fabric.mod.json", '{"id":"synthetic-mod"}')
    for arch in ("win64", "arm64", "linux64"):
        sdk = root / "remix-runtime" / arch
        sdk.mkdir(parents=True)
        (sdk / ".local-sdk-source").write_text("source: C:/synthetic-sdk\nstaged_by: SyntheticUser\n", encoding="utf-8")
        for name in (
            "d3d9.dll",
            "usd_usd.dll",
            "usd/plugins/RemixParticleSystem.dll",
            "dxvk.pdb",
            "NvRemixBridge.exe",
            "NvRemixBridge.pdb",
            "remixbridge.dll",
            "remixbridge.pdb",
            "libremix.so",
            "libremix-core.so",
            "libNRD.so",
            "libusd_ms.so",
            "libtbb.so.12",
            "libremixbridge.so",
        ):
            path = sdk / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"synthetic SDK input")
        bridge = root / "build" / "native" / arch
        bridge.mkdir(parents=True)
        for name in ("remixbridge.dll", "remixbridge.pdb", "libremixbridge.so"):
            (bridge / name).write_bytes(b"synthetic fresh bridge")
    textures = root / "remix-runtime" / "textures"
    textures.mkdir()
    for name in ("overcast_thunderstorm_bc6u.dds", "water_normals_flipbook.dds"):
        (textures / name).write_bytes(b"synthetic DDS input")
    project = GradleProject(root)
    project.run("publicationTestModel", "processResources", "stageFabricApi", "-Parch=win64")
    project.model = project.read_model()
    project.run("publicationTestModel", "-Parch=win64", "-PbenchmarkJfrPath=C:/r/profile.jfr")
    project.benchmark_model = project.read_model()
    return project
