"""Exercise distribution checks against real compiled ELF dependencies."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from check_runtime import inspect_library


class RuntimeDistributionTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="remix runtime ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "original"
        self.root.mkdir()
        (self.root / "leaf.c").write_text("int leaf(void) { return 7; }\n")
        (self.root / "main.c").write_text(
            "#include <unistd.h>\nextern int leaf(void);\n"
            "int answer(void) { return leaf() + getpid(); }\n"
        )
        self.compiler = os.environ.get("CC", "cc")
        subprocess.run([self.compiler, "-shared", "-fPIC", str(self.root / "leaf.c"),
                        "-Wl,-soname,libleaf.so", "-o", str(self.root / "libleaf.so")], check=True)

    def build(self, rpath):
        path = self.root / "libmain.so"
        subprocess.run([self.compiler, "-shared", "-fPIC", str(self.root / "main.c"),
                        "-L" + str(self.root), "-lleaf", "-Wl,-rpath," + rpath,
                        "-o", str(path)], check=True)
        return path

    def inspect(self, path, maximum="99.0"):
        return inspect_library(path, path.parent, {"GLIBC": maximum, "GLIBCXX": "99.0"})

    def test_relocated_library_loads_without_original_directory(self):
        self.build("$ORIGIN")
        relocated = Path(self.temporary.name) / "relocated"
        shutil.copytree(self.root, relocated)
        shutil.rmtree(self.root)
        path = relocated / "libmain.so"
        self.assertEqual(self.inspect(path)[1], [])
        environment = dict(os.environ)
        environment.pop("LD_LIBRARY_PATH", None)
        subprocess.run([sys.executable, "-c",
                        "import ctypes, os, sys; "
                        "assert ctypes.CDLL(sys.argv[1]).answer() - os.getpid() == 7", str(path)],
                       check=True, env=environment)

    def test_absolute_build_path_is_rejected_even_when_present(self):
        _, errors = self.inspect(self.build(str(self.root)))
        self.assertTrue(any("non-relocatable" in error for error in errors), errors)

    def test_loader_path_cannot_leave_the_distribution(self):
        _, errors = self.inspect(self.build("$ORIGIN/.."))
        self.assertTrue(any("escapes runtime" in error for error in errors), errors)

    def test_missing_private_dependency_is_rejected(self):
        path = self.build("$ORIGIN")
        (self.root / "libleaf.so").unlink()
        _, errors = self.inspect(path)
        self.assertTrue(any("not reachable" in error for error in errors), errors)

    def test_imported_glibc_version_must_fit_release_baseline(self):
        _, errors = self.inspect(self.build("$ORIGIN"), maximum="2.0")
        self.assertTrue(any("requires GLIBC_" in error for error in errors), errors)

    def test_wrong_elf_architecture_is_rejected(self):
        path = self.build("$ORIGIN")
        data = bytearray(path.read_bytes())
        data[18:20] = b"\xb7\x00"
        path.write_bytes(data)
        _, errors = self.inspect(path)
        self.assertTrue(any("x86_64 ELF" in error for error in errors), errors)


if __name__ == "__main__":
    unittest.main()
