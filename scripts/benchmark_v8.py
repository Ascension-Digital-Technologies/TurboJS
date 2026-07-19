#!/usr/bin/env python3
import argparse, json, os, statistics, subprocess, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def run(cmd):
    t=time.perf_counter_ns(); p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE); ns=time.perf_counter_ns()-t
    if p.returncode: raise RuntimeError(f"failed {cmd}: {p.stderr.decode('utf-8','replace')}")
    return ns/1e6,p.stdout.decode('utf-8','replace').strip()

def rss_kb(cmd):
    p=subprocess.run(['/usr/bin/time','-f','%M','-o','/tmp/tjs_rss.txt']+cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    if p.returncode: return None
    return int(Path('/tmp/tjs_rss.txt').read_text().strip())

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--turbojs',default=str(ROOT/'build/bench/turbojs')); ap.add_argument('--node',default='node'); ap.add_argument('--runs',type=int,default=9); ap.add_argument('--warmups',type=int,default=2); ap.add_argument('--output',default=str(ROOT/'build/v8-comparison.json')); a=ap.parse_args()
    workloads=['int_loop.js','float_loop.js','function_calls.js','object_props.js','dense_array.js','string_build.js']
    engines={'TurboJS':[a.turbojs],'Node/V8':[a.node]}; report={'runs':a.runs,'warmups':a.warmups,'engines':{},'workloads':{}}
    for n,c in engines.items():
        v=subprocess.run(c+['--version'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True).stdout.strip()
        report['engines'][n]={'command':c,'version':v,'binary_bytes':Path(c[0]).stat().st_size if Path(c[0]).exists() else None}
    for w in workloads:
        report['workloads'][w]={}; expected=None
        for n,c in engines.items():
            cmd=c+[str(ROOT/'tests/benchmarks/js'/w)]
            for _ in range(a.warmups): run(cmd)
            vals=[]; out=''
            for _ in range(a.runs): ms,out=run(cmd); vals.append(ms)
            if expected is None: expected=out
            if out!=expected: raise RuntimeError(f'output mismatch {w}: {n}={out!r} expected={expected!r}')
            report['workloads'][w][n]={'median_ms':statistics.median(vals),'mean_ms':statistics.mean(vals),'min_ms':min(vals),'max_ms':max(vals),'rss_kb':rss_kb(cmd),'output':out}
    for n,c in engines.items():
        vals=[]
        for _ in range(a.runs):
            ms,_=run(c+['-e','']); vals.append(ms)
        report['engines'][n]['startup_median_ms']=statistics.median(vals); report['engines'][n]['startup_rss_kb']=rss_kb(c+['-e',''])
    for w,d in report['workloads'].items(): d['v8_speedup']=d['TurboJS']['median_ms']/d['Node/V8']['median_ms']
    Path(a.output).parent.mkdir(parents=True,exist_ok=True); Path(a.output).write_text(json.dumps(report,indent=2)+'\n')
    print(f"{'workload':20} {'TurboJS ms':>12} {'Node/V8 ms':>12} {'V8 speedup':>12}")
    for w,d in report['workloads'].items(): print(f"{w:20} {d['TurboJS']['median_ms']:12.3f} {d['Node/V8']['median_ms']:12.3f} {d['v8_speedup']:11.2f}x")
    print('\nstartup')
    for n,d in report['engines'].items(): print(n, f"{d['startup_median_ms']:.3f} ms", f"{d['startup_rss_kb']} KB")
if __name__=='__main__': main()
