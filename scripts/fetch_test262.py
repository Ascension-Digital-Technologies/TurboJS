#!/usr/bin/env python3
"""Fetch or update TC39 Test262 without vendoring it in TurboJS archives."""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

DEFAULT_REPOSITORY = "https://github.com/tc39/test262.git"
DEFAULT_DESTINATION = Path("third_party/test262")


def run(command: list[str], cwd: Path | None = None) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=cwd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, default=DEFAULT_DESTINATION)
    parser.add_argument("--repository", default=DEFAULT_REPOSITORY)
    parser.add_argument("--ref", default="main", help="Branch, tag, or commit to check out")
    parser.add_argument("--depth", type=int, default=1, help="Clone depth; use 0 for a full clone")
    parser.add_argument("--force", action="store_true", help="Delete a non-Git destination before cloning")
    parser.add_argument("--clean", action="store_true", help="Reset local changes before updating")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    destination = args.destination if args.destination.is_absolute() else root / args.destination
    destination.parent.mkdir(parents=True, exist_ok=True)

    if shutil.which("git") is None:
        print("error: Git is required to fetch Test262", file=sys.stderr)
        return 2

    git_dir = destination / ".git"
    if destination.exists() and not git_dir.exists():
        if not args.force:
            print(
                f"error: {destination} exists but is not a Git checkout; use --force to replace it",
                file=sys.stderr,
            )
            return 2
        shutil.rmtree(destination)

    if not destination.exists():
        command = ["git", "clone"]
        if args.depth > 0:
            command += ["--depth", str(args.depth)]
        if args.ref:
            command += ["--branch", args.ref]
        command += [args.repository, str(destination)]
        run(command)
    else:
        if args.clean:
            run(["git", "reset", "--hard", "HEAD"], cwd=destination)
            run(["git", "clean", "-fd"], cwd=destination)
        run(["git", "fetch", "origin", args.ref] + (["--depth", str(args.depth)] if args.depth > 0 else []), cwd=destination)
        run(["git", "checkout", "--detach", "FETCH_HEAD"], cwd=destination)

    if not (destination / "test").is_dir() or not (destination / "harness").is_dir():
        print("error: fetched repository does not look like Test262", file=sys.stderr)
        return 3

    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=destination, text=True).strip()
    print(f"Test262 ready: {destination}")
    print(f"revision: {revision}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
