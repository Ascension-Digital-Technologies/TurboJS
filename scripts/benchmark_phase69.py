#!/usr/bin/env python3
"""Run matched high-resolution TurboJS and Node nested-loop benchmarks."""
from __future__ import annotations
import argparse, json, pathlib, subprocess
ROOT=pathlib.Path(__file__).resolve().parents[1]
def run_json(cmd):
    p=subprocess.run(cmd,capture_output=True,text=True,check=True)
    return json.loads(p.stdout)
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--build',default='build/phase69');ap.add_argument('--node',default='node');ap.add_argument('--runs',type=int,default=15);ap.add_argument('--repetitions',type=int,default=5);ap.add_argument('--warmups',type=int,default=3);ap.add_argument('--output',default='build/phase69-highres.json');a=ap.parse_args()
    turbo=run_json([str(ROOT/a.build/'turbojs-region-observability-benchmark'),str(a.runs),str(a.repetitions),str(a.warmups)])
    node=run_json([a.node,str(ROOT/'tests/benchmarks/js/warm_int_loop_hrtime_node.js'),str(a.runs),str(a.repetitions),str(a.warmups)])
    report={'workload':'nested integer loop','turbojs':turbo,'node':node,'turbojs_over_node':turbo['median_ns']/node['median_ns']}
    out=ROOT/a.output;out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
if __name__=='__main__': main()
