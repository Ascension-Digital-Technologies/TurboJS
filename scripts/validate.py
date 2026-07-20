#!/usr/bin/env python3
"""Run architecture, formatting hygiene, build, and focused tests."""
from __future__ import annotations

import argparse
import compileall
import sys
from pathlib import Path
from common import DEFAULT_PRESET, ROOT, banner, cmake, ctest, ensure_configured, main_guard, run


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", default=DEFAULT_PRESET)
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()

    banner(f"validate ({args.preset})")
    if not compileall.compile_dir(ROOT / "scripts", quiet=1, force=True):
        raise RuntimeError("Python script syntax validation failed")

    validator = ROOT / "tools" / "validation" / "check_architecture.py"
    if validator.exists():
        run([sys.executable, validator])

    build_dir = ensure_configured(args.preset)
    if not args.skip_build:
        run([cmake(), "--build", "--preset", args.preset])
    run([ctest(), "--preset", args.preset, "--output-on-failure"])

    forbidden = [
        ROOT / "src" / "compat",
        ROOT / "src" / "generated",
        ROOT / "src" / "cli",
        ROOT / "src" / "engine.c",
        ROOT / "benchmark-results",
        ROOT / "ROADMAP.md",
        ROOT / "RELEASE_STATUS.md",
    ]
    existing = [str(path.relative_to(ROOT)) for path in forbidden if path.exists()]
    if existing:
        raise RuntimeError("obsolete repository paths reappeared: " + ", ".join(existing))

    print(f"validation complete: {build_dir}")
    return 0


if __name__ == "__main__":
    main_guard(main)
