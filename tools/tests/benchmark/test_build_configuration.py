# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

import json
import subprocess
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


@pytest.mark.parametrize("arch", ["win64", "arm64", "linux64"])
def test_release_contains_only_the_verified_payload(gradle_project: GradleProject, arch: str):
    project = gradle_project
    staging = project.root / "build" / "release"
    stale = staging / "mods" / "stale.jar"
    stale.parent.mkdir(parents=True, exist_ok=True)
    stale.write_bytes(b"stale release")
    project.run("releaseArchive", "publicationTestModel", f"-Parch={arch}")
    model = project.read_model()
    jar = Path(model["packageJar"])
    assert jar.parent == project.root / "build" / "distributions"
    assert model["version"] in jar.name
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
    ignored = subprocess.run(
        ["git", "check-ignore", "--no-index", "--stdin"],
        cwd=REPO_ROOT,
        input="remix-runtime/textures/example.dds\nremix-runtime/win64/d3d9.dll\nremix-runtime/arm64/d3d9.dll\n",
        capture_output=True,
        text=True,
        check=True,
    )
    assert ignored.stdout.splitlines() == [
        "remix-runtime/textures/example.dds",
        "remix-runtime/win64/d3d9.dll",
        "remix-runtime/arm64/d3d9.dll",
    ]
    history = subprocess.run(
        ["git", "log", "--all", "--format=%h", "-1", "--", "remix-runtime"],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=True,
    )
    assert not history.stdout.strip()


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
