#!/usr/bin/env python3
import argparse, json, sys
from pathlib import Path

def main():
    ap=argparse.ArgumentParser(description='Compare TurboJS benchmark JSON files and fail on regressions.')
    ap.add_argument('baseline'); ap.add_argument('current')
    ap.add_argument('--max-regression-percent',type=float,default=5.0)
    a=ap.parse_args()
    old=json.loads(Path(a.baseline).read_text())
    new=json.loads(Path(a.current).read_text())
    failed=False
    print(f"{'workload':20} {'baseline ms':>12} {'current ms':>12} {'change':>10}")
    for name,oldw in old.get('workloads',{}).items():
        if name not in new.get('workloads',{}):
            print(f'{name:20} missing'); failed=True; continue
        before=oldw['TurboJS']['median_ms']; after=new['workloads'][name]['TurboJS']['median_ms']
        pct=(after/before-1.0)*100.0
        print(f'{name:20} {before:12.3f} {after:12.3f} {pct:9.2f}%')
        if pct > a.max_regression_percent: failed=True
    return 1 if failed else 0
if __name__=='__main__': raise SystemExit(main())
