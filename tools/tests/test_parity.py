# SPDX-FileCopyrightText: Copyright (c) 2026 EyeCrasher.
# SPDX-License-Identifier: Apache-2.0

import json
from pathlib import Path

import numpy as np
import pytest
from PIL import Image

from rtx_tools import parity


def capture(root: Path, pixel: tuple[int, int, int] = (20, 30, 40)) -> Path:
    root.mkdir()
    image = root / "diffuse.png"
    Image.new("RGB", (4, 3), pixel).save(image)
    properties = {
        "suite.version": "3",
        "probe.sha256": "probe",
        "api.sha256": "api",
        "config.sha256": "config",
        "capture.width": "4",
        "capture.height": "3",
        "capture.frames": "64",
        "case.count": "1",
        "case.isolation": "fresh-process",
        "case.0": "diffuse",
        "image.diffuse.sha256": parity.sha256(image),
        "runtime.sha256": root.name,
        "bridge.sha256": "bridge",
        "os": "test",
        "gl.renderer": "test GPU",
        "gl.version": "test driver",
        "completed": "true",
        "cleanup.completed": "true",
        "option.rtx.upscalerType": "0",
        "option.rtx.autoExposure.enabled": "false",
    }
    parity.write_properties(root / "metadata.xml", properties)
    return root


def change(root: Path, key: str, value: str) -> None:
    metadata = parity.read_properties(root / "metadata.xml")
    metadata[key] = value
    parity.write_properties(root / "metadata.xml", metadata)


def test_measurements_preserve_real_differences(tmp_path: Path):
    a = capture(tmp_path / "windows")
    b = capture(tmp_path / "linux", (23, 30, 40))
    output = tmp_path / "comparison"
    report = parity.compare_captures(a, b, output)
    row = report["measurements"][0]
    assert row["mean_absolute_channel_error"] == 1
    assert row["p95_absolute_channel_error"] == 3
    assert row["max_channel_error"] == 3
    assert row["different_pixel_fraction"] == 1
    assert report["status"] == "measured"
    assert json.loads((output / "comparison.json").read_text())["candidate"]["runtime.sha256"] == "linux"
    with Image.open(output / "diffuse.png") as strip:
        assert strip.size == (12, 3)
        assert strip.getpixel((8, 0)) == (24, 0, 0)


def test_matching_images_have_zero_error(tmp_path: Path):
    report = parity.compare_captures(capture(tmp_path / "a"), capture(tmp_path / "b"), tmp_path / "out")
    assert report["measurements"][0]["max_channel_error"] == 0


def settled(root: Path) -> Path:
    result = capture(root)
    for key, value in {
        "capture.setup.frames": "4",
        "capture.setup.scene": "empty-camera-no-mesh-material-light",
        "observed.settled.rtx.upscalerType": "0",
        "observed.settled.rtx.timeDeltaBetweenFrames": "16",
    }.items():
        change(result, key, value)
    return result


def test_settled_startup_compares_without_changing_pixels(tmp_path: Path):
    report = parity.compare_captures(settled(tmp_path / "a"), settled(tmp_path / "b"), tmp_path / "out")
    assert report["measurements"][0]["max_channel_error"] == 0
    assert report["candidate"]["capture.setup.frames"] == "4"


def test_setup_history_is_part_of_capture_identity(tmp_path: Path):
    a, b = capture(tmp_path / "a"), settled(tmp_path / "b")
    with pytest.raises(ValueError, match="Different test inputs: capture.setup.frames"):
        parity.compare_captures(a, b, tmp_path / "out")
    assert not (tmp_path / "out").exists()


def test_settled_observations_require_setup_metadata(tmp_path: Path):
    result = settled(tmp_path / "result")
    metadata = parity.read_properties(result / "metadata.xml")
    for key in parity.SETUP_KEYS:
        del metadata[key]
    parity.write_properties(result / "metadata.xml", metadata)
    with pytest.raises(ValueError, match="unsettled capture setup: capture.setup.frames"):
        parity.load_capture(result)


@pytest.mark.parametrize(
    ("key", "value"),
    [
        ("capture.setup.frames", "3"),
        ("capture.setup.scene", "scene-already-created"),
        ("observed.settled.rtx.upscalerType", "1"),
        ("observed.settled.rtx.timeDeltaBetweenFrames", "0"),
        ("capture.setup.frames", None),
        ("capture.setup.scene", None),
        ("observed.settled.rtx.upscalerType", None),
        ("observed.settled.rtx.timeDeltaBetweenFrames", None),
    ],
)
def test_unsettled_setup_is_rejected_before_comparison(tmp_path: Path, key: str, value: str | None):
    a, b = settled(tmp_path / "a"), settled(tmp_path / "b")
    metadata = parity.read_properties(b / "metadata.xml")
    if value is None:
        del metadata[key]
    else:
        metadata[key] = value
    parity.write_properties(b / "metadata.xml", metadata)
    with pytest.raises(ValueError, match=f"unsettled capture setup: {key}"):
        parity.compare_captures(a, b, tmp_path / "out")
    assert not (tmp_path / "out").exists()


@pytest.mark.parametrize(
    ("key", "value", "message"),
    [
        ("completed", "false", "incomplete"),
        ("cleanup.completed", "false", "incomplete"),
        ("option.rtx.upscalerType", "1", "upscaling"),
        ("probe.sha256", "other probe", "Different test inputs"),
        ("capture.view", "geometry_normal", "Different test inputs: capture.view"),
        ("views.sha256", "other view mapping", "Different test inputs: views.sha256"),
        ("observations.sha256", "other keys", "Different test inputs: observations.sha256"),
        ("capture.frames", "32", "Different test inputs"),
        ("case.isolation", "shared-process", "fresh native runtime"),
        ("option.rtx.autoExposure.enabled", "true", "Different effective renderer options"),
    ],
)
def test_incompatible_results_are_rejected(tmp_path: Path, key: str, value: str, message: str):
    a, b = capture(tmp_path / "a"), capture(tmp_path / "b")
    change(b, key, value)
    with pytest.raises(ValueError, match=message):
        parity.compare_captures(a, b, tmp_path / "out")
    assert not (tmp_path / "out").exists()


def test_replaced_png_is_rejected(tmp_path: Path):
    result = capture(tmp_path / "result")
    Image.new("RGB", (4, 3), (1, 2, 3)).save(result / "diffuse.png")
    with pytest.raises(ValueError, match="checksum"):
        parity.load_capture(result)


def test_wrong_extent_is_rejected_even_with_matching_checksum(tmp_path: Path):
    result = capture(tmp_path / "result")
    image = result / "diffuse.png"
    Image.fromarray(np.zeros((2, 2, 3), dtype=np.uint8)).save(image)
    change(result, "image.diffuse.sha256", parity.sha256(image))
    with pytest.raises(ValueError, match="extent"):
        parity.load_capture(result)


def test_case_order_is_part_of_the_test_identity(tmp_path: Path):
    a, b = capture(tmp_path / "a"), capture(tmp_path / "b")
    for root in (a, b):
        image = root / "second.png"
        Image.new("RGB", (4, 3)).save(image)
        change(root, "case.count", "2")
        change(root, "case.1", "second")
        change(root, "image.second.sha256", parity.sha256(image))
    change(b, "case.0", "second")
    change(b, "case.1", "diffuse")
    with pytest.raises(ValueError, match="case selection or ordering"):
        parity.compare_captures(a, b, tmp_path / "out")


def test_bundle_paths_cannot_escape_the_bundle(tmp_path: Path):
    bundle = tmp_path / "bundle"
    bundle.mkdir()
    outside = tmp_path / "outside"
    outside.write_text("external content")
    parity.write_properties(bundle / "bundle.xml", {"sha256." + "../outside": parity.sha256(outside)})
    with pytest.raises(ValueError, match="checksum mismatch"):
        parity.verify_bundle(bundle)


def test_duplicate_xml_keys_are_rejected(tmp_path: Path):
    path = tmp_path / "bad.xml"
    path.write_text('<properties><entry key="completed">true</entry><entry key="completed">false</entry></properties>')
    with pytest.raises(ValueError, match="Duplicate"):
        parity.read_properties(path)


def test_observed_defaults_are_reported_without_overriding_them(tmp_path: Path):
    a, b = capture(tmp_path / "windows"), capture(tmp_path / "linux")
    change(a, "observed.capture.rtx.pathMaxBounces", "4")
    change(b, "observed.capture.rtx.pathMaxBounces", "2")
    change(b, "observed.capture.rtx.di.enableBestLightJitter", "true")
    report = parity.compare_captures(a, b, tmp_path / "out")
    assert report["measurements"][0]["max_channel_error"] == 0
    assert report["observation_differences"] == [
        {"key": "observed.capture.rtx.di.enableBestLightJitter", "reference": None, "candidate": "true"},
        {"key": "observed.capture.rtx.pathMaxBounces", "reference": "4", "candidate": "2"},
    ]
    assert parity.read_properties(b / "metadata.xml")["observed.capture.rtx.pathMaxBounces"] == "2"
