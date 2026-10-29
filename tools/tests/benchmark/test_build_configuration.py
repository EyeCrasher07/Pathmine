# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

import json
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ElementTree
import zipfile
from pathlib import Path

import pytest
from conftest import GradleProject

from rtx_tools import install, prism
from rtx_tools.benchmark import util

REPO_ROOT = util.PROJECT_DIR


def test_build_tool_versions_are_reproducible(gradle_project: GradleProject):
    model = gradle_project.model
    assert "SNAPSHOT" not in model["loomVersion"].upper()
    wrapper = model["wrapper"]
    assert wrapper["distributionUrl"].endswith(f"/gradle-{model['gradleVersion']}-bin.zip")
    checksum = wrapper["distributionSha256Sum"]
    assert len(checksum) == 64 and int(checksum, 16) >= 0


@pytest.mark.parametrize(
    "arch, platform", [("win64", "windows-x64"), ("arm64", "windows-arm64"), ("linux64", "linux-x64")]
)
def test_release_contains_only_the_verified_payload(gradle_project: GradleProject, arch: str, platform: str):
    project = gradle_project
    staging = project.root / "build" / "release"
    stale = staging / "mods" / "stale.jar"
    stale.parent.mkdir(parents=True, exist_ok=True)
    stale.write_bytes(b"stale release")
    project.run("releaseArchive", "publicationTestModel", f"-Parch={arch}")
    model = project.read_model()
    jar = Path(model["packageJar"])
    assert jar.parent == project.root / "build" / "distributions"
    minecraft = next(dependency for dependency in model["dependencies"] if dependency["name"] == "minecraft")
    assert jar.name == f"pathmine-fabric-{minecraft['version']}-{model['baseVersion']}-{platform}.jar"
    assert "verifyPackageJar" in model["packageDependencies"]
    assert "package" in model["stagingDependencies"]
    with zipfile.ZipFile(jar) as archive:
        names = set(archive.namelist())
        sdk = f"native/remix-sdk-{arch}/"
        assert {name for name in names if name.startswith("native/remix-sdk-") and not name.endswith("/")} <= {
            sdk + path.relative_to(project.root / "remix-runtime" / arch).as_posix()
            for path in (project.root / "remix-runtime" / arch).rglob("*")
            if path.is_file()
        }
        assert not any(name.endswith(".local-sdk-source") for name in names)
        bridge = "libremixbridge.so" if arch == "linux64" else "remixbridge.dll"
        assert archive.read(sdk + bridge) == b"synthetic fresh bridge"
        if arch != "linux64":
            assert sdk + "d3d9.dll" in names
            assert not names & {sdk + "dxvk.pdb", sdk + "NvRemixBridge.exe", sdk + "NvRemixBridge.pdb"}
        assert {
            name.removeprefix("native/textures/")
            for name in names
            if name.startswith("native/textures/") and not name.endswith("/")
        } == set(install.RUNTIME_TEXTURE_FILES)
        for name in install.RUNTIME_TEXTURE_FILES:
            assert archive.read("native/textures/" + name) == b"synthetic DDS input"
        for directory, prefix in (
            ("packaging/licenses", "packaging/licenses/"),
            ("packaging/rtx-remix", "native/rtx-remix/"),
        ):
            for source in (REPO_ROOT / directory).rglob("*"):
                if source.is_file():
                    assert (
                        archive.read(prefix + source.relative_to(REPO_ROOT / directory).as_posix())
                        == source.read_bytes()
                    )
    assert (project.root / "remix-runtime" / arch / ".local-sdk-source").is_file()
    expected = {
        "mods/" + jar.name,
        "install.bat",
        "installer.ps1",
        "migrate_resource_packs.ps1",
        "INSTALLER_REFERENCE.md",
        "README.md",
        "LICENSE",
        "NOTICE",
        "THIRD_PARTY_NOTICES.md",
    }
    for directory in ("LICENSES", "packaging/licenses"):
        expected.update(
            path.relative_to(REPO_ROOT).as_posix() for path in (REPO_ROOT / directory).rglob("*") if path.is_file()
        )
    assert {path.relative_to(staging).as_posix() for path in staging.rglob("*") if path.is_file()} == expected
    assert (staging / "mods" / jar.name).read_bytes() == jar.read_bytes()
    assert (staging / "README.md").read_bytes() == (REPO_ROOT / "packaging/INSTALL.md").read_bytes()
    with zipfile.ZipFile(model["releaseArchive"]) as release:
        assert {name for name in release.namelist() if not name.endswith("/")} == expected
        for name in expected:
            assert release.read(name) == (staging / name).read_bytes()


@pytest.mark.parametrize("arch", ["win64", "arm64", "linux64"])
def test_package_verifier_rejects_python_payloads_only(gradle_project: GradleProject, arch: str):
    rejected = [
        "native/remix-sdk-win64/python310.dll",
        "native/remix-sdk-win64/python311.dll",
        "native/remix-sdk-win64/usd_python.dll",
        "native/remix-sdk-win64/python/",
        "native/remix-sdk-win64/python/pxr/Ar/_ar.pyd",
        "native/remix-sdk-win64/python/pxr/Ar/__pycache__/__init__.cpython-311.pyc",
        "native/remix-sdk-win64/lib/python310.dll",
        "native/remix-sdk-win64/libpython3.11.dll",
        "native/remix-sdk-win64/python311_d.dll",
        "native/remix-sdk-win64/boost_python310-vc142-mt-x64-1_76.dll",
        "native/remix-sdk-win64/python3.11/os.py",
        "native/remix-sdk-win64/lib/python3.11/site-packages/foo.pyc",
        "native/remix-sdk-win64/PYTHON311.DLL",
    ]
    accepted = [
        "native/remix-sdk-win64/another-runtime.dll",
        "native/remix-sdk-win64/usd/usd/resources/codegenTemplates/__init__.py",
        "native/remix-sdk-win64/usd/usd/resources/plugInfo.json",
        "python311.dll",
    ]
    rejected = [name.replace("remix-sdk-win64", f"remix-sdk-{arch}") for name in rejected]
    accepted = [name.replace("remix-sdk-win64", f"remix-sdk-{arch}") for name in accepted]
    (gradle_project.root / "fixtures/python-entries.json").write_text(json.dumps(rejected + accepted), encoding="utf-8")
    gradle_project.run("publicationTestPythonGuard", f"-Parch={arch}")
    results = json.loads((gradle_project.root / "build/publication-test-python.json").read_text(encoding="utf-8"))
    for name in rejected:
        assert results[name] == f"Distributable JAR must not contain a Python runtime: {name}"
    for name in accepted:
        assert results[name] is None


def test_runtime_textures_link_on_a_path_git_has_never_tracked():
    dependency = ElementTree.parse(REPO_ROOT / "packman/textures.packman.xml").find("dependency")
    assert dependency is not None
    assert dependency.attrib["linkPath"] == "../remix-runtime/textures"
    paths = [
        "remix-runtime/textures",
        "remix-runtime/win64",
        "remix-runtime/arm64",
        "remix-runtime/linux64",
    ]
    input_paths = b"\0".join(os.fsencode(path) for path in paths) + b"\0"
    ignored = subprocess.run(
        ["git", "check-ignore", "--no-index", "--stdin", "-z"],
        cwd=REPO_ROOT,
        input=input_paths,
        capture_output=True,
        check=True,
    )
    assert ignored.stdout == input_paths
    history = subprocess.run(
        ["git", "log", "--all", "--format=%h", "-1", "--", "remix-runtime"],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=True,
    )
    assert not history.stdout.strip()


@pytest.mark.parametrize("linked", [False, True], ids=["directories", "links"])
def test_runtime_sdk_paths_are_ignored(tmp_path: Path, linked: bool):
    shutil.copyfile(REPO_ROOT / ".gitignore", tmp_path / ".gitignore")
    subprocess.run(["git", "init", "--quiet", str(tmp_path)], check=True)
    excludes_file = tmp_path / ".git" / "empty-excludes"
    excludes_file.write_bytes(b"")
    subprocess.run(["git", "config", "core.excludesFile", str(excludes_file)], cwd=tmp_path, check=True)
    paths = [
        Path("remix-runtime/textures/example.dds"),
        Path("remix-runtime/win64/d3d9.dll"),
        Path("remix-runtime/arm64/d3d9.dll"),
        Path("remix-runtime/linux64/libremix.so"),
    ]
    for path in paths:
        staging = tmp_path / path.parent
        target = tmp_path / "build" / path.parent if linked else staging
        target.mkdir(parents=True, exist_ok=True)
        (target / path.name).write_bytes(b"synthetic runtime input")
        if linked:
            staging.parent.mkdir(parents=True, exist_ok=True)
            if os.name == "nt":
                subprocess.run(
                    ["cmd.exe", "/d", "/c", "mklink", "/J", str(staging), str(target)], check=True, capture_output=True
                )
            else:
                staging.symlink_to(target, target_is_directory=True)
    ignored_paths = [path.parent.as_posix() if linked else path.as_posix() for path in paths]
    input_paths = b"\0".join(os.fsencode(path) for path in ignored_paths) + b"\0"
    ignored = subprocess.run(
        ["git", "check-ignore", "--no-index", "--stdin", "-z"],
        cwd=tmp_path,
        input=input_paths,
        capture_output=True,
        check=True,
    )
    assert ignored.stdout == input_paths
    rejected = subprocess.run(["git", "add", "--", *ignored_paths], cwd=tmp_path, capture_output=True, text=True)
    assert rejected.returncode == 1, rejected.stderr
    subprocess.run(["git", "add", "--all"], cwd=tmp_path, check=True)
    tracked = subprocess.run(["git", "ls-files"], cwd=tmp_path, capture_output=True, text=True, check=True)
    assert tracked.stdout.splitlines() == [".gitignore"]


def test_runtime_staging_is_incremental_and_config_owns_one_file(gradle_project: GradleProject):
    project = gradle_project
    assert project.model["configOutputs"] == [str(project.root / "run/rtx.conf")]
    inputs = [project.root / "remix-runtime" / name / "stale-file" for name in ("win64", "textures")]
    for source in inputs:
        source.write_bytes(b"retired SDK input")
    project.run("syncRuntimeSdk", "syncRuntimeTextures", "copyRuntimeConfig", "-Parch=win64")
    run = project.root / "run"
    assert (run / "rtx.conf").read_bytes() == (REPO_ROOT / "packaging/rtx.conf").read_bytes()
    assert (run / "remix-sdk/remixbridge.dll").read_bytes() == b"synthetic fresh bridge"
    assert (run / "textures/water_normals_flipbook.dds").read_bytes() == b"synthetic DDS input"
    for source in inputs:
        source.unlink()
    unrelated = run / "unrelated-client-file"
    unrelated.write_bytes(b"client-owned")
    project.run("syncRuntimeSdk", "syncRuntimeTextures", "copyRuntimeConfig", "-Parch=win64")
    assert not list(run.rglob("stale-file"))
    result = project.run("syncRuntimeSdk", "syncRuntimeTextures", "copyRuntimeConfig", "-Parch=win64")
    for name in ("syncRuntimeSdk", "syncRuntimeTextures", "copyRuntimeConfig"):
        assert f"> Task :{name} UP-TO-DATE" in result.stdout
    assert unrelated.read_bytes() == b"client-owned"


def test_dev_instance_fabric_api_comes_from_the_pinned_dependency(gradle_project: GradleProject):
    (source,) = gradle_project.model["fabricSources"]
    assert Path(source).name == f"fabric-api-{prism.installer_constant('FABRIC_API_VERSION')}.jar"
    assert (gradle_project.root / "build/dev/fabric-api.jar").read_bytes() == Path(source).read_bytes()


def test_development_jvm_defaults_match_the_installer(gradle_project: GradleProject):
    assert gradle_project.model["vmArgs"] == prism.installer_constant("RECOMMENDED_JVM_ARGS").split()


def test_linux_facade_changes_invalidate_native_test_compilation(gradle_project: GradleProject):
    project = gradle_project
    facade = project.root / "rtxremix-java/src/com/nvidia/rtxremix/RemixAPI.java"
    original = facade.read_bytes()

    def compile_with(*, up_to_date: bool):
        result = project.run(
            "compileNativeLinux64",
            "-Parch=linux64",
            "-PpublicationTestNativeInputs",
            f"-PpublicationTestPython={sys.executable}",
        )
        status = " UP-TO-DATE" if up_to_date else "\n"
        assert f"> Task :compileNativeLinux64{status}" in result.stdout

    try:
        compile_with(up_to_date=False)
        compile_with(up_to_date=True)
        facade.write_bytes(original + b"\n")
        compile_with(up_to_date=False)
        compile_with(up_to_date=True)
    finally:
        facade.write_bytes(original)


@pytest.mark.parametrize("task", ["compileNativeWin64", "compileNativeArm64"])
def test_windows_native_headers_invalidate_compilation(gradle_project: GradleProject, task: str):
    project = gradle_project
    bridge = project.root / "rtxremix-java/native"
    platform = bridge / "bridge_platform.h"
    bundled = bridge / "include/remix_c.h"
    original_platform = platform.read_bytes()
    original_api = bundled.read_bytes()
    relative_include = "relative includes"
    absolute_include = project.root / "fixtures/absolute includes"
    for directory in (bridge / relative_include, absolute_include):
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "remix_c.h").write_bytes(original_api)

    def compile_with(include: str | None, *, up_to_date: bool):
        environment = dict(os.environ)
        environment.pop("REMIX_INCLUDE", None)
        if include is not None:
            environment["REMIX_INCLUDE"] = include
        result = project.run(
            task,
            "publicationTestModel",
            "-PpublicationTestNativeInputs",
            f"-PpublicationTestPython={sys.executable}",
            env=environment,
        )
        status = " UP-TO-DATE" if up_to_date else "\n"
        assert f"> Task :{task}{status}" in result.stdout
        inputs = project.read_model()["nativeInputs"][task]
        selected = bridge / (include or "include") / "remix_c.h"
        assert set(inputs["files"]) == {
            str(bridge / "build_native.bat"),
            str(bridge / "remixbridge.cpp"),
            str(platform),
            str(selected),
        }
        assert inputs["include"] == (include or "include")

    try:
        compile_with(None, up_to_date=False)
        compile_with(None, up_to_date=True)
        compile_with("", up_to_date=True)
        platform.write_bytes(original_platform + b"\n")
        compile_with(None, up_to_date=False)
        compile_with(None, up_to_date=True)
        bundled.write_bytes(original_api + b"\n")
        compile_with(None, up_to_date=False)
        compile_with(None, up_to_date=True)
        compile_with(relative_include, up_to_date=False)
        compile_with(relative_include, up_to_date=True)
        relative_header = bridge / relative_include / "remix_c.h"
        relative_header.write_bytes(original_api + b"\n\n")
        compile_with(relative_include, up_to_date=False)
        bundled.write_bytes(original_api + b"\n\n\n")
        compile_with(relative_include, up_to_date=True)
        absolute_header = absolute_include / "remix_c.h"
        absolute_header.write_bytes(relative_header.read_bytes())
        compile_with(str(absolute_include), up_to_date=False)
        compile_with(str(absolute_include), up_to_date=True)
        absolute_header.write_bytes(original_api + b"\n\n\n\n")
        compile_with(str(absolute_include), up_to_date=False)
        compile_with(str(absolute_include), up_to_date=True)
        bundled.write_bytes(original_api)
        compile_with(None, up_to_date=False)
        compile_with(None, up_to_date=True)
    finally:
        platform.write_bytes(original_platform)
        bundled.write_bytes(original_api)
