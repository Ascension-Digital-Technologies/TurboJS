# TurboJS RC5 broad engine benchmark against Node.js/V8

This report replaces narrow-path headline comparisons with a broad, checksum-verified engine scorecard.

## Environment

- TurboJS: `0.16.0-rc.5`, Release, warnings-as-errors build
- Node.js: `v22.16.0` using its bundled V8 engine
- Host: Linux x86-64
- Both engines executed exactly the same JavaScript source
- Every workload produced matching checksums

## Methodology

The sustained suite runs all workloads inside one engine process. Each workload receives three warmup batches and nine measured batches. The reported value is the median measured batch. Repetition counts are selected to make the Node measurements large enough to be meaningful while keeping the full TurboJS run practical.

The aggregate is the unweighted geometric mean of the per-workload TurboJS/Node median ratios. It does not include startup, parser-only measurements, or earlier analytically eliminated loop benchmarks.

Raw files:

- `tests/benchmarks/results/rc5-full-engine.json`
- `tests/benchmarks/results/rc5-full-engine.csv`
- `tests/benchmarks/full_engine/full_engine_sustained.js`
- `scripts/benchmark_full_engine.py`

## Sustained warm execution

| Workload | TurboJS median | Node/V8 median | TurboJS / Node |
|---|---:|---:|---:|
| Integer mixing | 172.145 ms | 2.318 ms | 74.27x |
| Float numeric recurrence | 14.097 ms | 2.262 ms | 6.23x |
| Monomorphic calls | 298.257 ms | 1.293 ms | 230.61x |
| Polymorphic calls | 21.150 ms | 0.650 ms | 32.54x |
| Closures | 12.841 ms | 0.161 ms | 79.88x |
| Monomorphic objects | 38.332 ms | 0.923 ms | 41.52x |
| Polymorphic objects | 19.901 ms | 0.542 ms | 36.72x |
| Dense arrays | 45.425 ms | 0.682 ms | 66.58x |
| Holey arrays | 72.899 ms | 1.121 ms | 65.04x |
| Typed arrays | 178.677 ms | 3.362 ms | 53.15x |
| String operations | 13.148 ms | 1.520 ms | 8.65x |
| JSON parse/stringify | 30.533 ms | 5.885 ms | 5.19x |
| Regular expressions | 9.853 ms | 1.704 ms | 5.78x |
| Array sorting | 8.119 ms | 3.218 ms | 2.52x |
| Exceptions | 11.968 ms | 2.036 ms | 5.88x |
| Recursion | 216.454 ms | 14.979 ms | 14.45x |
| Allocation and GC pressure | 12.493 ms | 0.847 ms | 14.74x |
| Mixed mini application | 49.110 ms | 2.093 ms | 23.46x |

### Aggregate

- Geometric mean ratio: **22.31x Node/V8 time**
- Median workload ratio: **28.00x Node/V8 time**
- Sum of workload medians: **1,225.40 ms TurboJS vs 45.60 ms Node/V8**

This is the most representative current warm-throughput result. TurboJS is not yet near V8 on broad application code.

## Cold startup and parsing

| Measurement | TurboJS | Node/V8 | TurboJS / Node |
|---|---:|---:|---:|
| Empty-process startup | **8.935 ms** | 56.579 ms | **0.158x** |
| Parse/compile 4,000 functions and execute probes | 71.470 ms | **70.250 ms** | 1.017x |

TurboJS starts approximately **6.33x faster** than Node in this environment. On the generated parser/compile workload, the two are effectively tied within normal run variance.

## Memory and binary footprint

| Metric | TurboJS | Node/V8 |
|---|---:|---:|
| Peak RSS during the full suite | **8,400 KiB** | 158,756 KiB |
| Executable size | **1,654,392 bytes** | 121,509,208 bytes |

TurboJS used about **5.3%** of Node's peak RSS in this run and its executable was about **1.36%** of the Node executable size.

## Interpretation

TurboJS currently has two very different performance profiles:

1. **Strong lightweight-engine characteristics:** fast startup, low memory use, small executable, competitive parsing, and excellent performance on several recognized/reducible loop families.
2. **Weak general warmed throughput:** ordinary function calls, closures, property access, arrays, typed arrays, integer-heavy code, and recursion remain far behind V8.

Earlier results where TurboJS completed millions of logical iterations in microseconds were valid analytical reductions, but they are not evidence of general JavaScript execution parity. They are intentionally excluded from this aggregate.

## Optimization priority based on measured impact

1. **General call JIT and inlining** — monomorphic calls are the largest gap at 230.61x.
2. **General integer region lowering** — integer mixing is 74.27x slower and does not match the recognized recurrence families.
3. **Closure and environment specialization** — closures are 79.88x slower.
4. **Dense, holey, and typed-array lowering** — current gaps are 53–67x.
5. **Shape-based property access and polymorphic inline caches** — object gaps are 37–42x.
6. **Recursion and compiled frame transitions** — recursion is 14.45x slower.
7. **Allocation and GC throughput** — 14.74x slower despite the much smaller resident footprint.
8. **Strings, JSON, regex, sorting, and exceptions** — these are closer, ranging from 2.52x to 8.65x.

Future performance claims should publish this suite alongside any specialized benchmark and should not claim whole-engine V8 parity until the broad geometric mean approaches 1.0x.
