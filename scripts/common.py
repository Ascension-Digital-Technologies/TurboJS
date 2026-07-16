#!/usr/bin/env python3
"""Shared helpers for TurboJS developer scripts."""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Iterable, Mapping, Sequence

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PRESET = "jit-dev"


class ScriptError(RuntimeError):
    pass


def banner(title: str) -> None:
    width = 72
    print("\n" + "=" * width)
    print(f"TurboJS :: {title}")
    print("=" * width)


def executable(name: str) -> str:
    resolved = shutil.which(name)
    if not resolved:
        raise ScriptError(f"required executable not found in PATH: {name}")
    return resolved


def run(
    command: Sequence[str | os.PathLike[str]],
    *,
    cwd: Path = ROOT,
    env: Mapping[str, str] | None = None,
    check: bool = True,
    capture: bool = False,
) -> subprocess.CompletedProcess[str]:
    args = [str(item) for item in command]
    print("+", subprocess.list2cmdline(args))
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    return subprocess.run(
        args,
        cwd=cwd,
        env=merged_env,
        check=check,
        text=True,
        capture_output=capture,
    )


def cmake() -> str:
    return executable("cmake")


def ctest() -> str:
    return executable("ctest")


def build_dir_for_preset(preset: str) -> Path:
    return ROOT / "build" / preset


def _has_legacy_glob_regeneration_rule(build_dir: Path) -> bool:
    """Detect build trees generated before the Windows/Ninja glob-loop fix."""
    ninja_file = build_dir / "build.ninja"
    if not ninja_file.is_file():
        return False
    try:
        text = ninja_file.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return False
    return "VerifyGlobs.cmake" in text or "cmake.verify_globs" in text


def ensure_configured(preset: str, *, fresh: bool = False) -> Path:
    build_dir = build_dir_for_preset(preset)
    if build_dir.exists() and (fresh or _has_legacy_glob_regeneration_rule(build_dir)):
        if not fresh:
            print("Removing stale build tree created with recursive CONFIGURE_DEPENDS globbing.")
        shutil.rmtree(build_dir)
    cache = build_dir / "CMakeCache.txt"
    if not cache.exists():
        run([cmake(), "--preset", preset])
    return build_dir


def native_executable(build_dir: Path, name: str) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    candidates = [
        build_dir / f"{name}{suffix}",
        build_dir / "Debug" / f"{name}{suffix}",
        build_dir / "Release" / f"{name}{suffix}",
        build_dir / "RelWithDebInfo" / f"{name}{suffix}",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise ScriptError(f"built executable not found: {name} in {build_dir}")


def parse_defines(values: Iterable[str]) -> list[str]:
    result: list[str] = []
    for value in values:
        if not value:
            continue
        result.extend(["-D", value])
    return result


def fail(message: str, code: int = 1) -> "NoReturn":
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(code)


def main_guard(func) -> None:
    try:
        raise SystemExit(func())
    except ScriptError as exc:
        fail(str(exc))
    except subprocess.CalledProcessError as exc:
        fail(f"command failed with exit code {exc.returncode}", exc.returncode)
