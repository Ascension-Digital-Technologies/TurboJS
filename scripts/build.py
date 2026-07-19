#!/usr/bin/env python3
"""Configure and build a TurboJS CMake preset."""
from __future__ import annotations

import argparse

from common import DEFAULT_PRESET, banner, cmake, ensure_configured, main_guard, run


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", default=DEFAULT_PRESET)
    parser.add_argument("--fresh", action="store_true", help="recreate the preset build directory")
    parser.add_argument("--target", action="append", default=[], help="build only this target; may be repeated")
    parser.add_argument("--jobs", type=int, default=0, help="parallel build jobs; 0 uses the build tool default")
    args = parser.parse_args()

    banner(f"build ({args.preset})")
    ensure_configured(args.preset, fresh=args.fresh)
    command = [cmake(), "--build", "--preset", args.preset]
    for target in args.target:
        command.extend(["--target", target])
    if args.jobs > 0:
        command.extend(["--parallel", str(args.jobs)])
    run(command)
    return 0


if __name__ == "__main__":
    main_guard(main)
