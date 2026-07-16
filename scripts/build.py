#!/usr/bin/env python3
"""Configure and build TurboJS."""
from __future__ import annotations

import argparse
from common import DEFAULT_PRESET, banner, cmake, ensure_configured, main_guard, run


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", default=DEFAULT_PRESET)
    parser.add_argument("--target", action="append", default=[], help="specific CMake target; repeatable")
    parser.add_argument("--parallel", type=int, default=0, help="parallel job count; 0 uses CMake default")
    parser.add_argument("--clean-first", action="store_true")
    parser.add_argument("--fresh", action="store_true", help="reconfigure from an empty build directory")
    args = parser.parse_args()

    banner(f"build ({args.preset})")
    ensure_configured(args.preset, fresh=args.fresh)
    command = [cmake(), "--build", "--preset", args.preset]
    if args.target:
        command.append("--target")
        command.extend(args.target)
    if args.parallel > 0:
        command.extend(["--parallel", str(args.parallel)])
    if args.clean_first:
        command.append("--clean-first")
    run(command)
    return 0


if __name__ == "__main__":
    main_guard(main)
