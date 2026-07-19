#!/usr/bin/env python3
import argparse, json, math, platform, statistics, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SUITE=ROOT/'tests/benchmarks/parity/whole_engine_parity.js'
def run(cmd,seed):
 p=subprocess.run(cmd+[str(SUITE),str(seed)],text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=600)
 if p.returncode: raise RuntimeError(f"failed: {' '.join(cmd)}\n{p.stderr}")
 for line in reversed(p.stdout.splitlines()):
  if line.strip().startswith('{'): return json.loads(line)
 raise RuntimeError('no JSON output')
def gm(xs): return math.exp(sum(math.log(x) for x in xs)/len(xs))
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--turbojs',default='build/full-release/turbojs');ap.add_argument('--node',default='node');ap.add_argument('--repetitions',type=int,default=5);ap.add_argument('--output',default='benchmarks/results/whole-engine-parity.json');a=ap.parse_args()
 tj=[str((ROOT/a.turbojs).resolve())]; nd=[a.node]; runs=[]
 seeds=[0x13579BDF+i*0x1020304 for i in range(a.repetitions)]
 for seed in seeds:
  t=run(tj,seed);n=run(nd,seed);nm={x['name']:x for x in n['results']};rows=[]
  for x in t['results']:
   y=nm[x['name']]
   if x['checksum']!=y['checksum']: raise RuntimeError(f"checksum mismatch {x['name']}: {x['checksum']} != {y['checksum']}")
   rows.append({'workload':x['name'],'turbojs_ms':x['median_ms'],'node_ms':y['median_ms'],'ratio':x['median_ms']/y['median_ms'],'checksum':x['checksum']})
  runs.append(rows)
 consolidated=[]
 for i,name in enumerate([r['workload'] for r in runs[0]]):
  ts=[r[i]['turbojs_ms'] for r in runs];ns=[r[i]['node_ms'] for r in runs];
  consolidated.append({'workload':name,'turbojs_median_ms':statistics.median(ts),'node_median_ms':statistics.median(ns),'ratio':statistics.median(ts)/statistics.median(ns),'run_ratios':[runs[j][i]['ratio'] for j in range(len(runs))]})
 ratios=[x['ratio'] for x in consolidated]
 result={'methodology':{'classification':'general whole-engine parity','suite':str(SUITE.relative_to(ROOT)),'independent_process_runs':a.repetitions,'warmups_per_workload':2,'timed_samples_per_workload':7,'input_policy':'identical externally supplied runtime seed; seed changes for every timed sample','validation':'exact checksum equality for every workload and process run','aggregation':'median across process runs, then unweighted geometric mean of workload ratios','exclusions':'no fixed-input leaf-call, pure recursion, or isolated analytical-kernel microbenchmarks'},'environment':{'platform':platform.platform(),'turbojs_version':subprocess.check_output(tj+['--version'],text=True).strip(),'node_version':subprocess.check_output(nd+['--version'],text=True).strip()},'workloads':consolidated,'aggregate':{'geometric_mean_ratio':gm(ratios),'median_ratio':statistics.median(ratios),'turbojs_total_ms':sum(x['turbojs_median_ms'] for x in consolidated),'node_total_ms':sum(x['node_median_ms'] for x in consolidated),'total_ratio':sum(x['turbojs_median_ms'] for x in consolidated)/sum(x['node_median_ms'] for x in consolidated)}}
 out=ROOT/a.output;out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
