#!/usr/bin/env python3
"""Run TurboJS sustained benchmarks and enforce checksum/performance gates."""
from __future__ import annotations
import argparse, json, math, os, statistics, subprocess, sys
from pathlib import Path

def parse_suite(text: str) -> dict:
    for line in reversed(text.splitlines()):
        try:
            obj=json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(obj,dict) and isinstance(obj.get('results'),list):
            return obj
    raise RuntimeError('benchmark output did not contain a suite JSON object')

def run(exe: Path, script: Path, affinity: str|None) -> dict:
    cmd=[str(exe), str(script)]
    if affinity and sys.platform.startswith('linux'):
        cmd=['taskset','-c',affinity,*cmd]
    p=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=False)
    if p.returncode:
        raise RuntimeError(f"benchmark failed ({p.returncode})\n{p.stdout}")
    return parse_suite(p.stdout)

def aggregate(runs: list[dict]) -> dict:
    names=[r['name'] for r in runs[0]['results']]
    out=[]
    for idx,name in enumerate(names):
        rows=[r['results'][idx] for r in runs]
        checks={json.dumps(x['checksum'],sort_keys=True) for x in rows}
        if len(checks)!=1: raise RuntimeError(f'checksum mismatch between runs: {name}')
        out.append({'name':name,'median_ms':statistics.median(x['median_ms'] for x in rows),'checksum':rows[0]['checksum']})
    return {'suite':runs[0].get('suite'),'process_runs':len(runs),'results':out,
            'sum_medians_ms':sum(x['median_ms'] for x in out),
            'geomean_ms':math.exp(sum(math.log(max(x['median_ms'],1e-12)) for x in out)/len(out))}

def main()->int:
    ap=argparse.ArgumentParser()
    ap.add_argument('--engine',required=True,type=Path)
    ap.add_argument('--script',type=Path,default=Path('tests/benchmarks/full_engine/full_engine_sustained.js'))
    ap.add_argument('--runs',type=int,default=3)
    ap.add_argument('--affinity',default='0')
    ap.add_argument('--baseline',type=Path)
    ap.add_argument('--max-sum-regression-percent',type=float,default=3.0)
    ap.add_argument('--max-workload-regression-percent',type=float,default=10.0)
    ap.add_argument('--output',type=Path)
    args=ap.parse_args()
    if args.runs<1: ap.error('--runs must be positive')
    result=aggregate([run(args.engine,args.script,args.affinity) for _ in range(args.runs)])
    failures=[]
    if args.baseline:
        baseline=json.loads(args.baseline.read_text())
        base={x['name']:x for x in baseline['results']}
        cur={x['name']:x for x in result['results']}
        for name,row in cur.items():
            if name not in base: failures.append(f'missing baseline workload: {name}'); continue
            if json.dumps(row['checksum'],sort_keys=True)!=json.dumps(base[name]['checksum'],sort_keys=True):
                failures.append(f'checksum mismatch: {name}')
                continue
            ratio=row['median_ms']/base[name]['median_ms']
            if ratio>1+args.max_workload_regression_percent/100:
                failures.append(f'{name} regressed {(ratio-1)*100:.2f}%')
        base_sum=baseline.get('sum_medians_ms',sum(x['median_ms'] for x in baseline['results']))
        ratio=result['sum_medians_ms']/base_sum
        result['baseline_sum_ratio']=ratio
        if ratio>1+args.max_sum_regression_percent/100:
            failures.append(f'suite total regressed {(ratio-1)*100:.2f}%')
    rendered=json.dumps(result,indent=2,sort_keys=True)
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True); args.output.write_text(rendered+'\n')
    print(rendered)
    if failures:
        print('PERFORMANCE GATE FAILED:',file=sys.stderr)
        for f in failures: print(f'  - {f}',file=sys.stderr)
        return 1
    return 0
if __name__=='__main__': raise SystemExit(main())
