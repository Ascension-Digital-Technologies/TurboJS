#!/usr/bin/env python3
"""Configure, build, and test TurboJS with sanitizers."""
from __future__ import annotations

import argparse
import os
import shutil
from common import ROOT, banner, cmake, ctest, main_guard, run


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kind", choices=["address", "undefined", "address-undefined"], default="address-undefined")
    parser.add_argument("--fresh", action="store_true")
    args = parser.parse_args()

    banner(f"sanitizers ({args.kind})")
    if os.name == "nt":
        raise RuntimeError("sanitizer helper currently requires Clang or GCC on a Unix-like host")

    flags = {
        "address": "-fsanitize=address -fno-omit-frame-pointer",
        "undefined": "-fsanitize=undefined -fno-omit-frame-pointer",
        "address-undefined": "-fsanitize=address,undefined -fno-omit-frame-pointer",
    }[args.kind]
    build_dir = ROOT / "build" / f"sanitize-{args.kind}"
    if args.fresh and build_dir.exists():
        shutil.rmtree(build_dir)

    run([
        cmake(), "-S", ROOT, "-B", build_dir, "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Debug",
        "-DTURBOJS_BUILD_TESTS=ON",
        "-DTURBOJS_BUILD_ENGINE_TESTS=OFF",
        "-DTURBOJS_BUILD_EXAMPLES=OFF",
        f"-DCMAKE_C_FLAGS={flags}",
        f"-DCMAKE_EXE_LINKER_FLAGS={flags}",
    ])
    run([cmake(), "--build", build_dir, "--target", "turbojs-jit-tests"])
    run([ctest(), "--test-dir", build_dir, "--output-on-failure"])
    return 0


if __name__ == "__main__":
    main_guard(main)
