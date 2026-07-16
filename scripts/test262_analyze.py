#!/usr/bin/env python3
from __future__ import annotations
import argparse,json,re
from collections import Counter
from pathlib import Path

def main():
 p=argparse.ArgumentParser(); p.add_argument('report',type=Path); p.add_argument('--top',type=int,default=25); a=p.parse_args()
 r=json.loads(a.report.read_text(encoding='utf-8')); results=r.get('results',[])
 c=Counter(x['status'] for x in results); executed=sum(c.values()); denom=executed-c.get('skip',0)
 print(f"completed={r.get('completed')} executions={executed} elapsed={r.get('elapsed',0):.2f}s")
 print(' '.join(f'{k}={v}' for k,v in sorted(c.items())))
 print(f"pass_rate_excluding_skips={100*c.get('pass',0)/denom:.2f}%" if denom else 'pass_rate_excluding_skips=n/a')
 fails=[x for x in results if x['status'] in ('fail','timeout','harness-error')]
 for depth in (2,3):
  paths=Counter('/'.join(x['test'].split('/')[:depth]) for x in fails)
  print(f'\nTop failing paths (depth {depth})')
  for k,v in paths.most_common(a.top): print(f'{v:6d} {k}')
 sig=Counter()
 for x in fails:
  line=(x.get('detail') or '').splitlines()[0]
  line=re.sub(r'\d+','N',line)
  sig[line]+=1
 print('\nTop failure signatures')
 for k,v in sig.most_common(a.top): print(f'{v:6d} {k[:180]}')
 return 0
if __name__=='__main__': raise SystemExit(main())
