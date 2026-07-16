#!/usr/bin/env python3
"""Regenerate checked-in TurboJS REPL and standalone bytecode C sources."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, type=Path)
    return parser.parse_args()


def generate(compiler: Path, source: str, output: str, symbol: str, module: bool) -> None:
    command = [
        str(compiler),
        "-s",
        "-s",
        "-N",
        symbol,
        "-n",
        Path(source).name,
        "-o",
        str(ROOT / output),
    ]
    if module:
        command.append("-m")
    command.append(str(ROOT / source))
    subprocess.run(command, cwd=ROOT, check=True)


def main() -> int:
    args = parse_args()
    compiler = args.compiler.resolve()
    if not compiler.is_file():
        raise SystemExit(f"compiler not found: {compiler}")
    generate(compiler, "runtime/repl/repl.js", "src/generated/runtime/repl.c", "qjsc_repl", False)
    generate(
        compiler,
        "runtime/bootstrap/standalone.js",
        "src/generated/runtime/standalone.c",
        "qjsc_standalone",
        True,
    )
    print("TurboJS runtime bytecode sources regenerated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
