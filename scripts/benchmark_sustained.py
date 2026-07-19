#!/usr/bin/env python3
import argparse, json, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tests" / "benchmarks" / "js" / "sustained_scalar_dynamic.js"

def run(command):
    proc = subprocess.run(command + [str(SCRIPT)], check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    line = proc.stdout.strip().splitlines()[-1]
    return json.loads(line)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--turbojs", default="build/phase86/turbojs")
    ap.add_argument("--node", default="node")
    ap.add_argument("--output", default="tests/benchmarks/results/phase87-sustained.json")
    args = ap.parse_args()
    turbo = run([args.turbojs])
    node = run([args.node])
    result = {"TurboJS": turbo, "Node/V8": node, "ratios": {}}
    for key in ("int32_dynamic_bound", "float64_dynamic_bound", "multi_accumulator_int32", "affine_call_dynamic_bound"):
        result["ratios"][key] = turbo[key]["median_ms"] / node[key]["median_ms"]
        if abs(turbo[key]["checksum"] - node[key]["checksum"]) > max(1e-6, abs(node[key]["checksum"]) * 1e-12):
            raise SystemExit(f"checksum mismatch for {key}: {turbo[key]['checksum']} != {node[key]['checksum']}")
    out = ROOT / args.output
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))

if __name__ == "__main__":
    main()
