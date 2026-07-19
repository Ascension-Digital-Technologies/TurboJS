#!/usr/bin/env python3
import argparse, json, statistics, subprocess, time
from pathlib import Path

WORKLOADS = ["int_loop.js", "float_loop.js", "function_calls.js", "object_props.js", "dense_array.js", "string_build.js"]

def measure(command, script, runs, warmups):
    for _ in range(warmups):
        subprocess.run(command + [str(script)], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    values, output = [], ""
    for _ in range(runs):
        start = time.perf_counter_ns()
        proc = subprocess.run(command + [str(script)], check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        values.append((time.perf_counter_ns() - start) / 1_000_000.0)
        output = proc.stdout.strip()
    return {"median_ms": statistics.median(values), "mean_ms": statistics.mean(values), "min_ms": min(values), "max_ms": max(values), "runs_ms": values, "output": output}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--turbojs", default="build/phase72/turbojs")
    ap.add_argument("--node", default="node")
    ap.add_argument("--runs", type=int, default=7)
    ap.add_argument("--warmups", type=int, default=2)
    ap.add_argument("--output", default="tests/benchmarks/results/whole-engine.json")
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[1]
    result = {"runs": args.runs, "warmups": args.warmups, "workloads": {}}
    for name in WORKLOADS:
        script = root / "tests" / "benchmarks" / "js" / name
        tj = measure([args.turbojs], script, args.runs, args.warmups)
        node = measure([args.node], script, args.runs, args.warmups)
        result["workloads"][name] = {"TurboJS": tj, "Node/V8": node, "turbojs_node_ratio": tj["median_ms"] / node["median_ms"]}
    output = root / args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))

if __name__ == "__main__":
    main()
