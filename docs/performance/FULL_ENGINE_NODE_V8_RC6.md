# TurboJS RC6 full-engine benchmark against Node/V8

RC6 repeats the same broad 18-workload benchmark used for RC5. The aggregate excludes analytically eliminated microbenchmarks and measures ordinary warmed JavaScript in one process.

## Result

| Metric | RC5 | RC6 |
|---|---:|---:|
| TurboJS sum of workload medians | 1,225.40 ms | **1,103.44 ms** |
| Node/V8 sum of workload medians | 45.60 ms | 45.04 ms |
| Broad geometric-mean ratio | 22.31x | 22.41x |
| Median workload ratio | 28.00x | **21.94x** |

The geometric mean was effectively unchanged because several Node measurements moved between runs and most engine subsystems were untouched. The absolute TurboJS workload total improved by approximately 10.0%.

## Primary improvement

| Workload | RC5 TurboJS | RC6 TurboJS | Improvement | RC6 vs Node |
|---|---:|---:|---:|---:|
| Monomorphic calls | 298.257 ms | **176.968 ms** | **40.7% lower** | 162.79x |

The call path now decodes supported tiny numeric leaf functions once, caches the plan on the bytecode object, and directly executes affine forms such as `(x * C + K) | 0`. RC5 rescanned and interpreted the leaf bytecode on every call.

## Current conclusion

RC6 materially improves the largest individual bottleneck, but TurboJS remains far behind V8 on general warmed throughput. The next broad priorities remain general call-site compilation, closure specialization, integer-loop compilation, object/array lowering, and typed-array lowering.

Raw data:

- `tests/benchmarks/results/rc6-full-engine.json`
- `tests/benchmarks/results/rc6-full-engine.csv`
