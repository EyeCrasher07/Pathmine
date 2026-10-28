#!/usr/bin/env python3
"""Validate the original Remix shader binaries before native renderer compilation."""

import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--validator", required=True)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    shaders = sorted(args.input.glob("*.spv"))
    if not shaders:
        parser.error("no compiled SPIR-V shaders were found")
    if args.jobs < 1:
        parser.error("--jobs must be positive")

    def validate(shader):
        result = subprocess.run(
            [args.validator, "--target-env", "vulkan1.4", "--scalar-block-layout", str(shader)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False,
        )
        return shader, result

    failures = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for shader, result in pool.map(validate, shaders):
            if result.returncode:
                failures += 1
                print(f"FAIL {shader.name}\n{result.stdout}", file=sys.stderr)
    print(f"Original Remix SPIR-V: {len(shaders) - failures}/{len(shaders)} validated")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
