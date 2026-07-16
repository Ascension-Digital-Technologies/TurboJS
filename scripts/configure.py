#!/usr/bin/env python3
"""Configure a TurboJS CMake preset."""
from __future__ import annotations

import argparse
import shutil
from common import DEFAULT_PRESET, ROOT, banner, build_dir_for_preset, cmake, main_guard, parse_defines, run


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", default=DEFAULT_PRESET)
    parser.add_argument("--fresh", action="store_true", help="delete the preset build directory first")
    parser.add_argument("-D", "--define", action="append", default=[], metavar="NAME=VALUE")
    args = parser.parse_args()

    banner(f"configure ({args.preset})")
    build_dir = build_dir_for_preset(args.preset)
    if args.fresh and build_dir.exists():
        shutil.rmtree(build_dir)
    run([cmake(), "--preset", args.preset, *parse_defines(args.define)], cwd=ROOT)
    print(f"configured: {build_dir}")
    return 0


if __name__ == "__main__":
    main_guard(main)
