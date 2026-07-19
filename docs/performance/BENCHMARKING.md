# TurboJS vs Node/V8 benchmark baseline

This baseline compares TurboJS 0.15.1 with Node.js v22.16.0 on the same Linux host. Each workload used identical JavaScript source, two warmup processes, and seven measured processes. The table reports the median wall-clock time. Process startup is measured separately.

| Workload | TurboJS | Node/V8 | Relative result |
|---|---:|---:|---:|
| Int32 loop, 10M iterations | 723.346 ms | 66.315 ms | V8 10.91x faster |
| Float64 loop, 5M iterations | 252.258 ms | 93.782 ms | V8 2.69x faster |
| Function calls, 5M calls | 631.080 ms | 74.802 ms | V8 8.44x faster |
| Object read/write, 5M iterations | 459.405 ms | 69.464 ms | V8 6.61x faster |
| Dense arrays, 6M indexed operations | 313.731 ms | 108.054 ms | V8 2.90x faster |
| Short string build | 26.242 ms | 71.834 ms | TurboJS 2.74x faster end-to-end |


## Phase 40 update

Phase 40 reduces hot-loop bytecode dispatch through local accumulation and branch superinstructions. On the same benchmark host, TurboJS improved from 723.346 ms to 626.713 ms on the 10M Int32 loop and from 252.258 ms to 204.554 ms on the 5M Float64 loop. See `history/PERFORMANCE_PHASE40.md` and `tests/benchmarks/results/phase40-linux-node22.json`.

## Startup and memory

| Metric | TurboJS | Node/V8 |
|---|---:|---:|
| Empty-process startup median | 7.159 ms | 55.178 ms |
| Empty-process peak RSS | 4.3 MiB | 123.9 MiB |
| Dense-array peak RSS | 22.6 MiB | 169.8 MiB |
| TurboJS executable size | 1.45 MiB | Not reported; `node` may be a launcher or dynamically linked executable |

The short string result is dominated by process startup and should not be interpreted as TurboJS having higher string throughput. TurboJS's isolated native tier benchmark reports about 7 ns per simple native call, demonstrating that the machine-code emitter itself is not the principal bottleneck. The largest gaps are getting ordinary JavaScript into optimized code, preserving optimized execution across loops/calls, and eliminating VM dispatch and boxed-value overhead.

## Highest-priority optimization plan

1. **Complete real loop OSR.** Compile supported loop bodies and enter native code from the live backedge instead of only capturing and validating the frame.
2. **Broaden source-to-IR coverage.** The current native tier handles narrow arithmetic functions. Add loops, branches, locals, induction variables, calls, property operations, and arrays.
3. **Use linear-scan allocation in final lowering.** Replace fixed/stack-heavy temporary placement with allocated GPR/XMM locations, phi-edge moves, and call-clobber handling.
4. **Inline small functions.** The 8.44x function-call gap indicates call frames and tier boundaries are a major cost.
5. **Lower polymorphic property caches into machine code.** VM-level ICs help, but hot object loads/stores need shape guards and direct slot access inside optimized code.
6. **Native dense-array loops.** Add element-kind guards, hoisted length/shape checks, bounds-check elimination, direct indexed loads/stores, and deoptimization on mutation.
7. **Unboxed numeric locals.** Keep Int32 and Float64 values unboxed across bytecode operations, loop phis, and calls whenever feedback permits.
8. **Allocation and GC fast paths.** Add nursery bump allocation and generational collection after execution-tier bottlenecks are reduced.

## Reproducing

```bash
cmake -S . -B build/bench -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DTURBOJS_BUILD_TESTS=ON \
  -DTURBOJS_BUILD_ENGINE_TESTS=ON \
  -DTURBOJS_ENABLE_INSTALL=OFF
cmake --build build/bench --target turbojs turbojs-tier-throughput-benchmark
python3 scripts/benchmark_v8.py --runs 7 --warmups 2
```

Machine-readable results are written to `build/v8-comparison.json`.
