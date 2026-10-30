# SPDX-FileCopyrightText: Copyright (c) 2026 Lazar Nikolic
# SPDX-License-Identifier: GPL-3.0-only

"""Execute the installer reuse boundary without downloads or a real Prism profile."""

import json
import os
import shutil
import subprocess
from pathlib import Path

import pytest

from rtx_tools.benchmark import util

POWERSHELL = shutil.which("powershell.exe") or shutil.which("pwsh")
pytestmark = pytest.mark.skipif(POWERSHELL is None, reason="PowerShell is required to execute the installer")
HARNESS = r"""
param([string]$Installer, [string]$Loaded, [string]$Profile)
$ast = [System.Management.Automation.Language.Parser]::ParseFile($Installer, [ref]$null, [ref]$null)
$entryPoint = $ast.EndBlock.Statements[-1]
if ($entryPoint -isnot [System.Management.Automation.Language.TryStatementAst]) {
    throw 'Missing installer entry point'
}
$start = $ast.ParamBlock.Extent.EndOffset
[IO.File]::WriteAllText($Loaded, $ast.Extent.Text.Substring($start, $entryPoint.Extent.StartOffset - $start),
    [Text.UTF8Encoding]::new($true))
function cmd { }
. $Loaded
function Sync-JvmArgs { Set-Content -LiteralPath (Join-Path $Profile 'mutated.txt') -Value 'JVM settings changed' }
function Set-PrismJavaPin { }
function Get-JavaMajor { return [int]$env:TEST_JAVA_MAJOR }
function Test-JavaArchMatchesHost { return $env:TEST_JAVA_ARCH -eq 'native' }
try {
    $state = [pscustomobject]@{ PrismDataDir = $Profile }
    $instance = Initialize-Prism -State $state -Unattended
    Set-Content -LiteralPath (Join-Path $Profile 'mutated.txt') -Value 'Ready to replace mod payload'
    Write-Output 'INSTANCE_ACCEPTED'
} catch {
    Write-Output $_.Exception.Message
    exit 1
}
"""


def snapshot(directory: Path) -> dict[str, bytes]:
    return {p.relative_to(directory).as_posix(): p.read_bytes() for p in directory.rglob("*") if p.is_file()}


@pytest.fixture
def profile(tmp_path: Path) -> Path:
    root = tmp_path / "prism"
    instance = root / "instances" / "Pathmine"
    game = instance / ".minecraft"
    for name, data in {
        "mods/pathmine-old.jar": b"working previous mod",
        "mods/fabric-api-old.jar": b"working previous API",
        "saves/owner-world/level.dat": b"owner world must survive",
    }.items():
        target = game / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    (instance / "instance.cfg").write_text("[General]\nInstanceType=OneSix\nJavaVersion=21\n", encoding="utf-8")
    (instance / "mmc-pack.json").write_text(
        json.dumps(
            {
                "formatVersion": 1,
                "components": [
                    {"uid": "net.minecraft", "version": "26.3"},
                    {"uid": "net.fabricmc.fabric-loader", "version": "0.20.1"},
                ],
            }
        ),
        encoding="utf-8",
    )
    (root / "prismlauncher.cfg").write_text(
        "[General]\nAutomaticJavaSwitch=true\nAutomaticJavaDownload=true\n", encoding="utf-8"
    )
    return root


def run_installer(profile: Path, tmp_path: Path, java_major: int = 25, architecture: str = "native"):
    assert POWERSHELL is not None
    script = tmp_path / "check.ps1"
    script.write_text(HARNESS, encoding="utf-8-sig")
    env = dict(os.environ, TEST_JAVA_MAJOR=str(java_major), TEST_JAVA_ARCH=architecture)
    for key in ("LOCALAPPDATA", "APPDATA", "USERPROFILE"):
        env[key] = str(tmp_path)
    return subprocess.run(
        [
            POWERSHELL,
            "-NoProfile",
            "-NonInteractive",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(script),
            "-Installer",
            str(util.PROJECT_DIR / "packaging" / "installer.ps1"),
            "-Loaded",
            str(tmp_path / "loaded.ps1"),
            "-Profile",
            str(profile),
        ],
        env=env,
        capture_output=True,
        text=True,
        timeout=30,
    )


@pytest.mark.parametrize(
    "fault", ["old-game", "old-loader", "missing-loader", "duplicate-game", "malformed", "missing", "wrong-type"]
)
def test_incompatible_reuse_refuses_before_any_instance_changes(profile: Path, tmp_path: Path, fault: str):
    instance = profile / "instances" / "Pathmine"
    pack = instance / "mmc-pack.json"
    manifest = json.loads(pack.read_text())
    if fault == "old-game":
        manifest["components"][0]["version"] = "1.21.1"
    elif fault == "old-loader":
        manifest["components"][1]["version"] = "0.16.5"
    elif fault == "missing-loader":
        manifest["components"].pop()
    elif fault == "duplicate-game":
        manifest["components"].append(manifest["components"][0])
    pack.write_text(json.dumps(manifest), encoding="utf-8")
    if fault == "malformed":
        pack.write_text("{invalid", encoding="utf-8")
    elif fault == "missing":
        pack.unlink()
    elif fault == "wrong-type":
        (instance / "instance.cfg").write_text("InstanceType=Legacy\n", encoding="utf-8")
    before = snapshot(profile)

    result = run_installer(profile, tmp_path)

    assert result.returncode == 1, result.stdout + result.stderr
    assert "Create a separate Minecraft 26.3 Fabric instance" in result.stdout
    assert snapshot(profile) == before


def test_matching_instance_accepts_prism_automatic_java_selection(profile: Path, tmp_path: Path):
    result = run_installer(profile, tmp_path)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "INSTANCE_ACCEPTED" in result.stdout


def test_new_instance_uses_the_current_game_loader_and_java(profile: Path, tmp_path: Path):
    instance = profile / "instances" / "Pathmine"
    shutil.rmtree(instance)
    result = run_installer(profile, tmp_path)
    assert result.returncode == 0, result.stdout + result.stderr
    manifest = json.loads((instance / "mmc-pack.json").read_text(encoding="utf-8-sig"))
    versions = {component["uid"]: component["version"] for component in manifest["components"]}
    assert versions == {"net.minecraft": "26.3", "net.fabricmc.fabric-loader": "0.20.1"}
    assert "JavaVersion=25" in (instance / "instance.cfg").read_text().splitlines()


def test_cached_java_version_does_not_override_an_unknown_actual_java(profile: Path, tmp_path: Path):
    (profile / "prismlauncher.cfg").write_text("[General]\nJavaVersion=25\n", encoding="utf-8")
    before = snapshot(profile)
    result = run_installer(profile, tmp_path)
    assert result.returncode == 1, result.stdout + result.stderr
    assert snapshot(profile) == before


@pytest.mark.skipif(os.name != "nt", reason="Native Windows Java executable paths")
@pytest.mark.parametrize(
    "java_major,architecture,accepted", [(25, "native", True), (21, "native", False), (25, "x86", False)]
)
def test_pinned_java_must_match_the_game_and_native_architecture(
    profile: Path, tmp_path: Path, java_major: int, architecture: str, accepted: bool
):
    instance = profile / "instances" / "Pathmine"
    (instance / "instance.cfg").write_text(
        f"[General]\nInstanceType=OneSix\nOverrideJavaLocation=true\nJavaPath={tmp_path / 'java.exe'}\n",
        encoding="utf-8",
    )
    before = snapshot(profile)
    result = run_installer(profile, tmp_path, java_major, architecture)
    assert result.returncode == (0 if accepted else 1), result.stdout + result.stderr
    if not accepted:
        assert snapshot(profile) == before
