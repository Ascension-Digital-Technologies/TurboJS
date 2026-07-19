#!/usr/bin/env python3
"""Measure JavaScript warm throughput separately from process startup.

A benchmark source must define benchmark(iterations) and return a numeric checksum.
The generated wrapper performs warmup inside one engine process, then reports only
steady-state milliseconds using Date.now().
"""
from __future__ import annotations
import argparse, json, pathlib, statistics, subprocess, tempfile, time

def main() -> int:
    ap=argparse.ArgumentParser()
    ap.add_argument('--engine', required=True)
    ap.add_argument('--source', required=True)
    ap.add_argument('--iterations', type=int, default=10)
    ap.add_argument('--warmup', type=int, default=3)
    ap.add_argument('--runs', type=int, default=7)
    ap.add_argument('--report', default='build/warm-benchmark.json')
    args=ap.parse_args()
    source=pathlib.Path(args.source).read_text(encoding='utf-8')
    values=[]; checks=[]; wall=[]
    wrapper=source+f'''\nfor (let w=0;w<{args.warmup};w++) benchmark({args.iterations});\nlet __t0=Date.now();\nlet __check=benchmark({args.iterations});\nlet __t1=Date.now();\n(typeof print === "function" ? print : console.log)("TURBOJS_WARM " + (__t1-__t0) + " " + __check);\n'''
    with tempfile.TemporaryDirectory(prefix='turbojs-warm-') as td:
        path=pathlib.Path(td)/'bench.js'; path.write_text(wrapper,encoding='utf-8')
        for _ in range(args.runs):
            t=time.perf_counter()
            cp=subprocess.run([args.engine,str(path)],capture_output=True,text=True,encoding='utf-8',errors='backslashreplace')
            wall.append((time.perf_counter()-t)*1000)
            if cp.returncode: raise SystemExit(cp.stderr or cp.stdout)
            line=next((x for x in cp.stdout.splitlines() if x.startswith('TURBOJS_WARM ')),None)
            if not line: raise SystemExit('benchmark did not emit TURBOJS_WARM marker')
            _,ms,check=line.split(' ',2); values.append(float(ms)); checks.append(check)
    result={'engine':args.engine,'source':args.source,'iterations':args.iterations,'warmup':args.warmup,'runs':args.runs,
            'warm_ms':{'median':statistics.median(values),'min':min(values),'max':max(values),'samples':values},
            'process_ms':{'median':statistics.median(wall),'samples':wall},'checksum':checks[-1]}
    out=pathlib.Path(args.report); out.parent.mkdir(parents=True,exist_ok=True); out.write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2)); return 0
if __name__=='__main__': raise SystemExit(main())
