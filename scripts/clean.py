#!/usr/bin/env python3
"""Remove generated TurboJS build and benchmark artifacts."""
from __future__ import annotations

import argparse
import shutil
from common import ROOT, banner, main_guard


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", default="", help="clean only build/<preset>")
    parser.add_argument("--all", action="store_true", help="also remove local caches")
    args = parser.parse_args()

    banner("clean")
    paths = [ROOT / "build" / args.preset] if args.preset else [ROOT / "build"]
    if args.all:
        paths.extend([ROOT / ".pytest_cache", ROOT / ".cache"])
        paths.extend(ROOT.rglob("__pycache__"))

    seen = set()
    for path in paths:
        path = path.resolve()
        if path in seen or path == ROOT.resolve():
            continue
        seen.add(path)
        if path.exists():
            print(f"remove: {path}")
            shutil.rmtree(path)
    return 0


if __name__ == "__main__":
    main_guard(main)
