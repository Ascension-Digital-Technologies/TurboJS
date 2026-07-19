#!/usr/bin/env python3
import argparse, csv, json, math, os, platform, statistics, subprocess, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUITE = ROOT / 'tests/benchmarks/full_engine/full_engine_sustained.js'
EMPTY = ROOT / 'tests/benchmarks/full_engine/empty.js'
PARSE = ROOT / 'tests/benchmarks/full_engine/parse_compile.js'


def run_checked(command, script, timeout=300):
    p = subprocess.run(command + [str(script)], text=True, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, timeout=timeout)
    if p.returncode:
        raise RuntimeError(f"command failed ({p.returncode}): {' '.join(command)} {script}\n{p.stderr}")
    return p


def final_json(stdout):
    for line in reversed(stdout.splitlines()):
        line = line.strip()
        if line.startswith('{'):
            return json.loads(line)
    raise ValueError('no JSON result line found')


def process_median(command, script, warmups, runs):
    for _ in range(warmups): run_checked(command, script)
    vals=[]; output=''
    for _ in range(runs):
        t0=time.perf_counter_ns(); p=run_checked(command, script); vals.append((time.perf_counter_ns()-t0)/1e6); output=p.stdout.strip()
    return {'median_ms':statistics.median(vals),'mean_ms':statistics.mean(vals),'min_ms':min(vals),'max_ms':max(vals),'runs_ms':vals,'output':output}


def peak_rss_kib(command, script):
    with tempfile.NamedTemporaryFile(delete=False) as f: path=f.name
    try:
        p=subprocess.run(['/usr/bin/time','-f','%M','-o',path] + command + [str(script)],
                         stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, timeout=300)
        if p.returncode: raise RuntimeError(p.stderr)
        return int(Path(path).read_text().strip())
    finally:
        try: os.unlink(path)
        except OSError: pass


def geomean(xs): return math.exp(sum(math.log(x) for x in xs)/len(xs))


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--turbojs', default='build/bench/turbojs')
    ap.add_argument('--node', default='node')
    ap.add_argument('--output', default='tests/benchmarks/results/rc5-full-engine.json')
    ap.add_argument('--csv', default='tests/benchmarks/results/rc5-full-engine.csv')
    args=ap.parse_args()
    turbo=[str((ROOT/args.turbojs).resolve())] if not os.path.isabs(args.turbojs) else [args.turbojs]
    node=[args.node]
    EMPTY.write_text('// intentionally empty\n')
    if not PARSE.exists():
        lines=['let checksum = 0;']
        for i in range(4000): lines.append(f'function f{i}(x) {{ return ((x + {i}) * 3) | 0; }}')
        for i in range(0,4000,17): lines.append(f'checksum = (checksum + f{i}({i+1})) | 0;')
        lines.append('(typeof print === "function" ? print : console.log)(checksum);')
        PARSE.write_text('\n'.join(lines)+'\n')

    tp=run_checked(turbo,SUITE); np=run_checked(node,SUITE)
    tj=final_json(tp.stdout); nj=final_json(np.stdout)
    nmap={x['name']:x for x in nj['results']}
    rows=[]
    for t in tj['results']:
        n=nmap[t['name']]
        if isinstance(t['checksum'],float) or isinstance(n['checksum'],float):
            ok=abs(t['checksum']-n['checksum']) <= max(1e-7,abs(n['checksum'])*1e-12)
        else: ok=t['checksum']==n['checksum']
        if not ok: raise RuntimeError(f"checksum mismatch {t['name']}: {t['checksum']} != {n['checksum']}")
        rows.append({'workload':t['name'],'repetitions':t['repetitions'],'turbojs_median_ms':t['median_ms'],
                     'node_median_ms':n['median_ms'],'ratio':t['median_ms']/n['median_ms'],'checksum':t['checksum']})
    ratios=[r['ratio'] for r in rows]
    cold={'empty':{'TurboJS':process_median(turbo,EMPTY,3,21),'Node/V8':process_median(node,EMPTY,3,21)},
          'parse_compile':{'TurboJS':process_median(turbo,PARSE,2,15),'Node/V8':process_median(node,PARSE,2,15)}}
    for v in cold.values(): v['ratio']=v['TurboJS']['median_ms']/v['Node/V8']['median_ms']
    result={
      'methodology':{'suite':str(SUITE.relative_to(ROOT)),'warmups_per_workload':tj['warmups'],'measured_runs_per_workload':tj['runs'],
                     'aggregation':'median per workload; unweighted geometric mean of TurboJS/Node ratios',
                     'notes':['same JavaScript source and checksums','single-process sustained measurements','separate cold startup and parse/compile process measurements','no narrow-path results included in aggregate']},
      'environment':{'os':platform.platform(),'machine':platform.machine(),'python':platform.python_version(),
                     'turbojs_version':run_checked(turbo,Path('/dev/null')).stdout.strip() if False else subprocess.check_output(turbo+['--version'],text=True).strip(),
                     'node_version':subprocess.check_output(node+['--version'],text=True).strip()},
      'sustained':{'workloads':rows,'geometric_mean_ratio':geomean(ratios),'median_ratio':statistics.median(ratios),
                   'turbojs_total_of_medians_ms':sum(r['turbojs_median_ms'] for r in rows),
                   'node_total_of_medians_ms':sum(r['node_median_ms'] for r in rows)},
      'cold_process':cold,
      'binary_size_bytes':{'TurboJS':Path(turbo[0]).stat().st_size,'Node/V8':Path(subprocess.check_output(['which',args.node],text=True).strip()).stat().st_size}
    }
    out=ROOT/args.output; out.parent.mkdir(parents=True,exist_ok=True); out.write_text(json.dumps(result,indent=2)+'\n')
    cp=ROOT/args.csv; cp.parent.mkdir(parents=True,exist_ok=True)
    with cp.open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
    print(json.dumps(result,indent=2))

if __name__=='__main__': main()
