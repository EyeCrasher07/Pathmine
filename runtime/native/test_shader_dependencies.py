# SPDX-License-Identifier: GPL-3.0-only

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

RUNTIME = Path(__file__).resolve().parent.parent
VENDOR_HEADERS = (
    "submodules/rtxdi/rtxdi-sdk/include/rtxdi/Fixture.hlsli",
    "submodules/rtxdi/rtxdi-sdk/include/volumetrics/Fixture.hlsli",
    "submodules/rtxcr/shaders/include/Fixture.hlsli",
    "submodules/sharc/include/SharcCommon.h",
    "submodules/nrc/Include/Nrc.hlsli",
    "external-nrd/Shaders/Include/NRD.hlsli",
)
COMPILER = r"""#!/usr/bin/env python3
import hashlib
import json
import os
import sys
from pathlib import Path
args = sys.argv[1:]
output = Path(args[args.index('-o') + 1])
inputs = [Path(name) for name in json.loads(os.environ['FIXTURE_SHADER_INPUTS'])]
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(hashlib.sha256(b''.join(path.read_bytes() for path in inputs)).hexdigest())
for dependency_option in ('--depfile', '-depfile'):
    if dependency_option in args:
        Path(args[args.index(dependency_option) + 1]).write_text(str(output) + ': ' + ' '.join(map(str, inputs)) + '\n')
with open(os.environ['FIXTURE_SHADER_LOG'], 'a') as log:
    log.write(str(output) + '\n')
"""


class ShaderDependencyTests(unittest.TestCase):
    def test_headers_and_compilers_invalidate_native_shader_commands(self):
        with tempfile.TemporaryDirectory(prefix="shader-dependencies-") as temporary:
            root = Path(temporary)
            native = root / "native"
            native.mkdir()
            for name in (
                "CMakeLists.txt",
                "headers.cmake",
                "dependencies.lock.json",
                "runtime_components.cmake",
                "glsl_shaders.cmake",
            ):
                shutil.copy2(RUNTIME / "native" / name, native / name)
            with (native / "CMakeLists.txt").open("a") as cmake:
                cmake.write("\ninclude(glsl_shaders.cmake)\n")
            shutil.copytree(
                RUNTIME / "src",
                root / "src",
                ignore=lambda directory, names: (
                    ["shaders"] if Path(directory) == RUNTIME / "src/dxvk" else []
                ),
            )
            shutil.copytree(RUNTIME / "scripts-common", root / "scripts-common")
            shaders = root / "src/dxvk/shaders"
            (shaders / "rtx").mkdir(parents=True)
            local = shaders / "rtx/fixture.comp"
            local.write_text("#version 450\nvoid main() {}\n")
            slang_local = shaders / "rtx/fixture_slang.comp.slang"
            slang_local.write_text("[shader(\"compute\")] void main() {}\n")
            (shaders / "fixture.frag").write_text("#version 450\nvoid main() {}\n")
            headers = [root / name for name in VENDOR_HEADERS]
            for header in headers:
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text("fixture header\n")
            portable = root / "portable/include/native/windows/windows.h"
            portable.parent.mkdir(parents=True)
            portable.touch()
            (root / "directx").mkdir()
            (root / "directx/d3d9.h").touch()
            tools = root / "tools"
            tools.mkdir()
            glslang = tools / "glslangValidator"
            glslang.write_text(COMPILER)
            glslang.chmod(0o755)
            slangc = tools / "slangc"
            slangc.write_text(COMPILER)
            slangc.chmod(0o755)
            slang_library = tools / "libslang.so"
            slang_library.write_text("synthetic compiler library input")
            validator = tools / "spirv-val"
            validator.touch(mode=0o755)
            log = root / "compiler.log"
            environment = dict(
                os.environ,
                FIXTURE_SHADER_INPUTS=json.dumps(list(map(str, [local, slang_local, *headers]))),
                FIXTURE_SHADER_LOG=str(log),
            )
            build = root / "build"

            def run(*args):
                result = subprocess.run(
                    args, env=environment, capture_output=True, text=True
                )
                self.assertEqual(0, result.returncode, result.stdout + result.stderr)

            run(
                "cmake",
                "-S",
                str(native),
                "-B",
                str(build),
                "-G",
                "Ninja",
                "-DBUILD_TESTING=OFF",
                "-DREMIX_BUILD_SHADERS=ON",
                f"-DPython3_EXECUTABLE={sys.executable}",
                f"-DREMIX_DXVK_NATIVE_SOURCE_DIR={root / 'portable'}",
                f"-DREMIX_DIRECTX_HEADERS_DIR={root / 'directx'}",
                f"-DREMIX_NRD_SOURCE_DIR={root / 'external-nrd'}",
                f"-DREMIX_GLSLANG={glslang}",
                f"-DREMIX_SLANGC={slangc}",
                f"-DREMIX_SLANG_LIBRARY={slang_library}",
                f"-DREMIX_SPIRV_VAL={validator}",
            )

            def compile_shaders():
                run(
                    "cmake",
                    "--build",
                    str(build),
                    "--target",
                    "remix-rtx-shaders",
                    "remix-glsl-shaders",
                )
                return log.read_text().splitlines()

            initial = compile_shaders()
            self.assertIn(str(build / "rtx_shaders/fixture.h"), initial)
            self.assertIn(str(build / "rtx_shaders/fixture_slang.spv"), initial)
            self.assertIn(str(build / "glsl_shaders/fixture.h"), initial)
            slang_header = build / "rtx_shaders/fixture_slang.h"
            self.assertTrue(slang_header.is_file())
            self.assertEqual(len(initial), len(compile_shaders()))
            output = build / "rtx_shaders/fixture.h"
            previous = output.read_bytes()
            for header in headers:
                with self.subTest(header=header.relative_to(root)):
                    before = len(log.read_text().splitlines())
                    header.write_text("changed fixture header\n")
                    self.assertEqual(before + 2, len(compile_shaders()))
                    self.assertNotEqual(previous, output.read_bytes())
                    previous = output.read_bytes()
                    self.assertEqual(before + 2, len(compile_shaders()))
            for compiler, count in (
                (glslang, len(initial)),
                (slangc, 2),
                (slang_library, 2),
            ):
                with self.subTest(compiler=compiler.name):
                    before = len(log.read_text().splitlines())
                    compiler.write_text(compiler.read_text() + "\n")
                    self.assertEqual(before + count, len(compile_shaders()))
                    self.assertEqual(before + count, len(compile_shaders()))
            for name in ("shader_xxd.py", "depfile.py"):
                with self.subTest(helper=name):
                    before = len(log.read_text().splitlines())
                    previous_header = slang_header.read_bytes()
                    helper = root / "scripts-common" / name
                    with helper.open("a") as script:
                        script.write("\n")
                        if name == "shader_xxd.py":
                            script.write("with open(args.output, 'a') as header:\n    header.write('\\n')\n")
                    self.assertEqual(before + 2, len(compile_shaders()))
                    if name == "shader_xxd.py":
                        self.assertEqual(previous_header + b"\n", slang_header.read_bytes())
                    self.assertEqual(before + 2, len(compile_shaders()))


if __name__ == "__main__":
    unittest.main()
