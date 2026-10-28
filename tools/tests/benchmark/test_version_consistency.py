# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

import json
import unittest

from conftest import GradleProject

from rtx_tools import prism
from rtx_tools.benchmark import util

REPO_ROOT = util.PROJECT_DIR


def test_packaging_and_runtime_versions_match_gradle(gradle_project: GradleProject):
    gradle_project.run("publicationTestModel", "processResources", "-Parch=win64")
    model = gradle_project.read_model()
    assert prism.installer_constant("APP_VERSION") == model["baseVersion"]
    dependencies = {(entry["group"], entry["name"]): entry["version"] for entry in model["dependencies"]}
    for coordinate, variable in (
        (("com.mojang", "minecraft"), "MC_VERSION"),
        (("net.fabricmc", "fabric-loader"), "FABRIC_LOADER_VER"),
        (("net.fabricmc.fabric-api", "fabric-api"), "FABRIC_API_VERSION"),
    ):
        assert prism.installer_constant(variable) == dependencies[coordinate]
    metadata = json.loads((gradle_project.root / "build/resources/main/fabric.mod.json").read_text(encoding="utf-8"))
    assert metadata["version"] == model["version"]
    assert metadata["depends"]["minecraft"] == dependencies["com.mojang", "minecraft"]
    assert metadata["depends"]["fabricloader"] == ">=" + dependencies["net.fabricmc", "fabric-loader"]
    assert metadata["depends"]["fabric-api"] == ">=" + dependencies["net.fabricmc.fabric-api", "fabric-api"]


class VersionConsistencyTests(unittest.TestCase):
    def test_fabric_metadata_matches_repository_contract(self):
        metadata = json.loads(
            (REPO_ROOT / "src" / "main" / "resources" / "fabric.mod.json").read_text(encoding="utf-8")
        )
        self.assertEqual("GPL-3.0-only", metadata["license"])
        self.assertNotIn("fabric-api", metadata.get("suggests", {}))

        icon = metadata.get("icon")
        if icon:
            self.assertTrue((REPO_ROOT / "src" / "main" / "resources" / icon).is_file())

        sources = metadata.get("contact", {}).get("sources", "")
        self.assertNotIn("rtxremix-java", sources)

    def test_readme_license_matches_repository_license(self):
        license_text = (REPO_ROOT / "LICENSE").read_text(encoding="utf-8")
        readme = (REPO_ROOT / "README.md").read_text(encoding="utf-8")

        self.assertIn("GNU GENERAL PUBLIC LICENSE", license_text)
        self.assertIn("GPL-3.0", readme)
        self.assertIn("Apache-2.0", readme)
        self.assertNotRegex(readme, r"(?m)^MIT\s+[\u2014-]\s+See LICENSE file$")


if __name__ == "__main__":
    unittest.main()
