#!/usr/bin/env python3
"""Create a clean TurboJS source archive."""
from __future__ import annotations

import argparse
import shutil
import tempfile
from datetime import datetime
from pathlib import Path
from common import ROOT, banner, main_guard

EXCLUDED_NAMES = {"build", ".git", ".cache", "__pycache__", ".pytest_cache"}


def ignore(directory: str, names: list[str]) -> set[str]:
    del directory
    return {name for name in names if name in EXCLUDED_NAMES or name.endswith((".pyc", ".pyo"))}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", default=f"turbojs-{datetime.now():%Y%m%d}")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist")
    args = parser.parse_args()

    banner("package")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="turbojs-package-") as temp:
        staging = Path(temp) / args.name
        shutil.copytree(ROOT, staging, ignore=ignore)
        archive = shutil.make_archive(str(args.output_dir / args.name), "zip", staging.parent, staging.name)
    print(f"archive: {archive}")
    return 0


if __name__ == "__main__":
    main_guard(main)
