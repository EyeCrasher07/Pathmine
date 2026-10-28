# Copyright (c) 2026 EyeCrasher.
# SPDX-License-Identifier: MIT

import json
import subprocess
import sys
from pathlib import Path

import pytest

from rtx_tools import install, linuxproc, prism

pytestmark = pytest.mark.skipif(sys.platform != "linux", reason="Linux process and ELF integration")


def test_linux_profile_and_elf_architecture(tmp_path, monkeypatch):
    monkeypatch.delenv("APPDATA", raising=False)
    monkeypatch.delenv(prism.DATA_DIR_ENV, raising=False)
    monkeypatch.setenv("XDG_DATA_HOME", str(tmp_path / "data"))
    layout = prism.launcher_layout(Path("/usr/bin/prismlauncher"), tmp_path / "run", tmp_path)
    assert layout.data_dir == tmp_path / "data/PrismLauncher"
    assert prism.executable_arch(Path(sys.executable).resolve()) == "linux64"


def test_pidfd_follows_and_signals_only_the_discovered_child():
    with subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"]) as child:
        process = linuxproc.PidProcess(child.pid)
        assert process.poll() is None
        with pytest.raises(subprocess.TimeoutExpired):
            process.wait(0.001)
        process.terminate()
        child.wait(timeout=5)
        assert process.wait(timeout=5) == -1
        process.kill()


def test_linux_staging_refreshes_a_changed_core_with_an_unchanged_facade(tmp_path, monkeypatch):
    source, target = tmp_path / "source", tmp_path / "target"
    source.mkdir()
    (source / "libremix.so").write_bytes(b"facade")
    (source / "libremix-core.so").write_bytes(b"first core")
    bridge = tmp_path / "libremixbridge.so"
    bridge.write_bytes(b"bridge")
    monkeypatch.delenv("REMIX_FORCE_SDK_COPY", raising=False)
    install._stage_runtime(source, bridge, target, "linux64")
    (source / "libremix-core.so").write_bytes(b"second core")
    install._stage_runtime(source, bridge, target, "linux64")
    assert (target / "libremix-core.so").read_bytes() == b"second core"
    assert (target / "libremixbridge.so").read_bytes() == b"bridge"


@pytest.mark.parametrize("owned", [False, True])
def test_linux_migration_preserves_user_packs_bom_and_order(tmp_path, owned):
    legacy = tmp_path / "resourcepacks/rtx-remix-labpbr"
    legacy.mkdir(parents=True)
    (legacy / "BLOCK_TEXTURES.txt").write_text("baseline")
    (legacy / "pack.mcmeta").write_text(
        json.dumps(
            {
                "pack": {
                    "pack_format": 34,
                    "description": "RTX Remix LabPBR Development Pack" if owned else "My textures",
                }
            }
        )
    )
    options = tmp_path / "options.txt"
    options.write_bytes(
        b'\xef\xbb\xbfother:true\r\nresourcePacks:["file/user.zip","file/rtx-remix-labpbr","fabric"]\r\n'
    )
    install._migrate_linux_resource_packs(tmp_path)
    result = options.read_bytes()
    assert result.startswith(b"\xef\xbb\xbfother:true\r\n")
    assert result.endswith(b"\r\n")
    packs = json.loads(result.decode("utf-8-sig").splitlines()[1].split(":", 1)[1])
    assert packs[:3] == ["vanilla", "fabric", "file/user.zip"]
    assert ("file/rtx-remix-labpbr" in packs) is not owned
    assert legacy.exists() is not owned
