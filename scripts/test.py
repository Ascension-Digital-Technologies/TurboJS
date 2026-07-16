#!/usr/bin/env python3
"""Build and run the TurboJS test suite."""
from __future__ import annotations

import argparse
from common import DEFAULT_PRESET, banner, cmake, ctest, ensure_configured, main_guard, run


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", default=DEFAULT_PRESET)
    parser.add_argument("--filter", default="", help="CTest regular expression")
    parser.add_argument("--exclude", default="", help="CTest exclusion regular expression")
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--jobs", type=int, default=0)
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    banner(f"test ({args.preset})")
    ensure_configured(args.preset)
    if not args.no_build:
        run([cmake(), "--build", "--preset", args.preset])

    command = [ctest(), "--preset", args.preset, "--output-on-failure"]
    if args.filter:
        command.extend(["-R", args.filter])
    if args.exclude:
        command.extend(["-E", args.exclude])
    if args.repeat > 1:
        command.extend(["--repeat", f"until-fail:{args.repeat}"])
    if args.jobs > 0:
        command.extend(["-j", str(args.jobs)])
    if args.verbose:
        command.append("-V")
    run(command)
    return 0


if __name__ == "__main__":
    main_guard(main)
