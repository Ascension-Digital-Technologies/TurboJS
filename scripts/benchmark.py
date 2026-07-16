#!/usr/bin/env python3
"""Run repeatable TurboJS focused microbenchmarks."""
from __future__ import annotations

import argparse
import json
import statistics
import time
from datetime import datetime, timezone
from pathlib import Path
from common import DEFAULT_PRESET, ROOT, banner, cmake, ensure_configured, main_guard, native_executable, run

DEFAULT_TARGETS = [
    "turbojs-jit-differential-test",
    "turbojs-engine-bytecode-test",
    "turbojs-engine-locals-test",
    "turbojs-engine-control-flow-test",
    "turbojs-checked-arithmetic-test",
    "turbojs-runtime-safepoint-test",
    "turbojs-completion-pipeline-test",
    "turbojs-automatic-optimizing-tier-test",
]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", default=DEFAULT_PRESET)
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--target", action="append", default=[])
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "benchmark-results.json")
    parser.add_argument("--no-build", action="store_true")
    args = parser.parse_args()
    if args.runs < 1 or args.warmup < 0:
        parser.error("--runs must be >= 1 and --warmup must be >= 0")

    targets = args.target or DEFAULT_TARGETS
    banner(f"benchmark ({args.preset})")
    build_dir = ensure_configured(args.preset)
    if not args.no_build:
        command = [cmake(), "--build", "--preset", args.preset, "--target", *targets]
        run(command)

    report = {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "preset": args.preset,
        "warmup_runs": args.warmup,
        "measured_runs": args.runs,
        "benchmarks": [],
    }

    print(f"{'benchmark':42} {'median ms':>12} {'mean ms':>12} {'min ms':>12}")
    print("-" * 82)
    for target in targets:
        program = native_executable(build_dir, target)
        for _ in range(args.warmup):
            run([program], capture=True)

        samples: list[float] = []
        for _ in range(args.runs):
            start = time.perf_counter_ns()
            completed = run([program], capture=True)
            elapsed_ms = (time.perf_counter_ns() - start) / 1_000_000.0
            samples.append(elapsed_ms)
            if completed.returncode != 0:
                raise RuntimeError(f"benchmark failed: {target}")

        entry = {
            "name": target,
            "median_ms": statistics.median(samples),
            "mean_ms": statistics.fmean(samples),
            "min_ms": min(samples),
            "max_ms": max(samples),
            "samples_ms": samples,
        }
        report["benchmarks"].append(entry)
        print(f"{target:42} {entry['median_ms']:12.3f} {entry['mean_ms']:12.3f} {entry['min_ms']:12.3f}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"\nreport: {args.output}")
    return 0


if __name__ == "__main__":
    main_guard(main)
