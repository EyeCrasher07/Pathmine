# SPDX-License-Identifier: GPL-3.0-only

"""Exercise the repository gate against a real, isolated Git index."""

import configparser
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from rtx_tools.benchmark import util


@pytest.fixture
def repository(tmp_path: Path) -> Path:
    root = util.PROJECT_DIR
    for name in (
        "tools/check_repository.py",
        "LICENSE",
        ".gitignore",
        ".gitattributes",
        ".gitmodules",
        "gradlew",
        "runtime/.gitignore",
        "runtime/native/dependencies.lock.json",
        "runtime/native/patches/nrd-4.13-vulkan-storage-formats.patch",
        "rtxremix-java/native/include/remix_c.h",
    ):
        target = tmp_path / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / name, target)
    subprocess.run(["git", "init", "--quiet", str(tmp_path)], check=True)
    subprocess.run(["git", "config", "core.autocrlf", "false"], cwd=tmp_path, check=True)
    subprocess.run(["git", "add", "--all"], cwd=tmp_path, check=True)
    subprocess.run(["git", "update-index", "--chmod=+x", "gradlew"], cwd=tmp_path, check=True)
    modules = configparser.ConfigParser()
    modules.read(root / ".gitmodules")
    for section in modules.sections():
        path = modules[section]["path"]
        revision = subprocess.check_output(["git", "rev-parse", f":{path}"], cwd=root, text=True).strip()
        subprocess.run(
            ["git", "update-index", "--add", "--cacheinfo", f"160000,{revision},{path}"], cwd=tmp_path, check=True
        )
    return tmp_path


def check(repository: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run([sys.executable, "tools/check_repository.py"], cwd=repository, text=True, capture_output=True)


def test_clean_source_index_passes(repository: Path):
    result = check(repository)
    assert result.returncode == 0, result.stderr


@pytest.mark.parametrize("name", [".agents/private.txt", "build/output.jar", "accounts.json"])
def test_forced_local_files_fail(repository: Path, name: str):
    path = repository / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("{}", encoding="utf-8")
    subprocess.run(["git", "add", "--force", name], cwd=repository, check=True)
    result = check(repository)
    assert result.returncode == 1
    assert name in result.stderr


def test_invalid_configuration_fails(repository: Path):
    (repository / "settings.json").write_text("{broken", encoding="utf-8")
    subprocess.run(["git", "add", "settings.json"], cwd=repository, check=True)
    result = check(repository)
    assert result.returncode == 1
    assert "Invalid configuration settings.json" in result.stderr


def test_changed_api_contract_fails(repository: Path):
    header = repository / "rtxremix-java/native/include/remix_c.h"
    header.write_bytes(header.read_bytes() + b"\n/* fixture */\n")
    result = check(repository)
    assert result.returncode == 1
    assert "API header does not match" in result.stderr
