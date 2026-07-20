#!/usr/bin/env python3
"""Run a JavaScript workload with TurboJS JIT telemetry and summarize OSR blockers."""
import argparse
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SUITE = ROOT / "tests/benchmarks/parity/whole_engine_parity.js"

CATEGORIES = [
    ("calls", "osr_rejections_calls"),
    ("indexed access", "osr_rejections_indexed"),
    ("property operations", "osr_rejections_properties"),
    ("numeric semantics", "osr_rejections_numeric"),
    ("control flow", "osr_rejections_control_flow"),
    ("other", "osr_rejections_other"),
]

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", default="build/full-release/turbojs")
    parser.add_argument("--script", default=str(DEFAULT_SUITE))
    parser.add_argument("--seed", default="324508639")
    parser.add_argument("--output", default="benchmarks/results/osr-rejection-profile.json")
    args = parser.parse_args()

    engine = Path(args.engine)
    if not engine.is_absolute():
        engine = ROOT / engine
    script = Path(args.script)
    if not script.is_absolute():
        script = ROOT / script
    command = [str(engine), "--jit-stats-json", str(script), str(args.seed)]
    result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=600)
    if result.returncode:
        raise SystemExit(result.stderr or f"TurboJS exited with {result.returncode}")
    objects = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if line.startswith("{"):
            try:
                objects.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    if not objects:
        raise SystemExit("TurboJS produced no JSON telemetry")
    stats = objects[-1]
    unsupported = int(stats.get("osr_rejections_unsupported", 0))
    rows = []
    for label, key in CATEGORIES:
        count = int(stats.get(key, 0))
        rows.append({"category": label, "counter": key, "count": count,
                     "percent": (100.0 * count / unsupported) if unsupported else 0.0})
    rows.sort(key=lambda row: row["count"], reverse=True)
    report = {
        "engine": str(engine),
        "script": str(script),
        "seed": args.seed,
        "unsupported_total": unsupported,
        "categories": rows,
        "jit_stats": stats,
    }
    output = Path(args.output)
    if not output.is_absolute():
        output = ROOT / output
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"OSR unsupported loops: {unsupported}")
    for row in rows:
        print(f"  {row['category']:<20} {row['count']:>5}  {row['percent']:6.2f}%")
    print(f"Wrote {output}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
