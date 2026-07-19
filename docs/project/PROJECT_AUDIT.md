# TurboJS Engineering Audit

This audit reviews the GitHub-ready TurboJS v1 tree with emphasis on correctness, hot-path cost, build structure, JIT/AOT safety, and release risk.

## Executive assessment

The focused JIT/AOT subsystem is coherent and well covered for its declared numeric subset. The largest remaining risks are in inherited full-engine code and build architecture, not in the isolated JIT library. This pass intentionally optimized measurable hot paths and fixed undefined behavior without performing broad semantic rewrites of the parser or runtime.

## Issues fixed in this pass

### 1. Linear code-cache lookup and expensive removal — High performance impact

The baseline code cache scanned every entry for lookup and used `memmove` when invalidating or evicting an entry. Lookup was O(n), removal was O(n), and hot functions paid this cost on every native dispatch.

**Resolution:** replaced the packed array with an open-addressed pointer-key hash table. Lookup and invalidation are now expected O(1). LRU eviction remains O(n), but it only runs when cache limits are reached. Cache storage is allocated once, avoiding repeated growth reallocations.

### 2. Signed-overflow undefined behavior in SSA constant folding — High correctness impact

The optimizer directly evaluated signed `int64_t` add, subtract, and multiply. Overflow in C is undefined behavior and could let the host compiler produce an invalid fold.

**Resolution:** constant folding now uses checked compiler builtins with portable fallbacks. Overflowing expressions remain unfused and preserve runtime semantics.

### 3. Non-cascading dead-value elimination — Medium performance impact

The previous reverse scan could leave newly dead producer chains in the graph after removing one consumer.

**Resolution:** dead-value elimination now uses a worklist. Removing a value decrements its operands and immediately schedules newly dead producers, producing a smaller optimized graph in one pass.

### 4. Counter and threshold wraparound — Medium correctness/longevity impact

Call counts, observations, transitions, bailouts, and exceptions could wrap to zero. Baseline-to-optimization threshold multiplication could overflow.

**Resolution:** counters saturate at their maximum value and optimization-threshold arithmetic is saturating.

### 5. SSA allocation growth overflow — Medium security/correctness impact

Capacity doubling and allocation-size multiplication lacked complete overflow checks.

**Resolution:** graph arrays now use a shared checked growth routine that validates doubling and byte-size calculations.

### 6. AOT module validation gaps — Medium security impact

Module loading allowed embedded NUL bytes and duplicate export names. Size arithmetic and offset representation needed stricter format checks.

**Resolution:** module deserialization rejects embedded NUL export names and duplicate exports. Serialization and loading now perform checked table/image arithmetic and format-limit validation.

### 7. Constant branch target selection — Medium correctness impact

Branch folding inferred fallthrough using a block-number shortcut.

**Resolution:** the optimizer now selects the recorded CFG successor and only folds when the destination is valid.

## Validation completed

- Focused CMake/Ninja build completed.
- 25 of 25 focused tests pass.
- Modified JIT library and tests compile with `-Wall -Wextra -Werror`.
- Added stress coverage for code-cache eviction, lookup, and invalidation.
- Added regression coverage proving `INT64_MAX + 1` is not folded.
- Architecture validator still reports 51 domain sources across 9 subsystems.

## Remaining issues and recommended priority

### P0 — Full-engine validation remains incomplete

The isolated JIT suite is strong, but the full parser/runtime engine should be built and tested in CI on Windows, Linux, and macOS. The generated unity translation unit makes this slow and obscures file-level diagnostics.

**Recommended:** make split subsystem compilation the default and keep unity mode as an optional release/LTO experiment.

### P0 — Test262 coverage must be published

No current repository artifact provides a reproducible, category-level Test262 result for this exact release.

**Recommended:** add pinned Test262 revision, exclusion metadata, timeout policy, and CI summary artifacts.

### P1 — Legacy QuickJS identity remains

Dozens of implementation/configuration files still contain QuickJS or QJS identifiers. Some are ABI inheritance; others are stale naming.

**Recommended:** classify each occurrence as ABI-required, specification-required, or removable. Rename only with compatibility and serialization migration plans.

### P1 — Optimizing lowering supports a narrow CFG subset

SSA analysis supports multiple blocks, but optimized native lowering still rejects graphs requiring general phi resolution.

**Recommended:** add edge splitting, phi parallel-move lowering, and linear-scan register allocation before expanding speculative optimizations.

### P1 — Full GC integration needs engine-level stress testing

Stack maps, safepoints, rooting hooks, and relocation contracts exist, but they need direct testing against the full collector and boxed `JSValue` lifecycle.

**Recommended:** collect on every allocation and every JIT safepoint in dedicated stress builds.

### AOT loader resource budgets — Resolved in follow-up optimization pass

The loader now accepts explicit limits for total image bytes, function count, export-name bytes, instructions per function, and aggregate instructions. The default API applies conservative limits, while embedders can supply tighter application-specific budgets.

### P2 — Large inherited source files

The generated engine unit is roughly 2 MB, while parser, module evaluator, interpreter, and Unicode files remain large. This increases build latency and review difficulty.

**Recommended:** preserve performance-sensitive unity builds only as generated outputs; compile and test subsystem sources individually by default.

### P2 — Remaining TODO/FIXME/XXX notes

The inherited engine contains many unresolved notes, including known slow paths and some conformance caveats. They should be converted into tracked issues with subsystem ownership rather than left as unprioritized comments.

### P2 — Benchmark precision

Current benchmark scripts mostly measure complete test-process runtime. That is useful for regression smoke testing but not for generated-code throughput.

**Recommended:** add in-process cycle/iteration benchmarks, compile-cost accounting, cache-hit measurements, and interpreter/baseline/optimized/AOT comparisons using identical workloads.

## Performance policy

TurboJS should prefer dense, predictable control flow in proven hot paths, but not globally sacrifice correctness or debuggability. Performance changes should include a benchmark, a differential test, or an algorithmic justification. Parser/runtime code should not be mechanically minified: the compiler already removes formatting, while unreadable source raises defect risk without improving machine code.

## Follow-up optimization results

- Added configurable AOT module resource budgets and regression coverage.
- Added a true in-process tier throughput benchmark.
- Renamed the remaining module-build control macros from `QUICKJS_NG_*` to `TURBOJS_*`.
- Confirmed 26/26 focused tests pass.
- Strict C11 warning validation passes for the AOT loader and its limit tests.

A representative debug-build benchmark on the validation host measured approximately 68.8 ns per interpreted call, 9.0 ns per baseline-native call, and 11.4 ns per optimizing-wrapper call for a one-add function. The optimizing path currently shares the same 92-byte native body and pays extra wrapper overhead; this identifies wrapper elision/direct optimized entry as the next measurable performance target rather than supporting a misleading claim that every optimized call is already faster.
