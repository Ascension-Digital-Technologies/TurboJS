# TurboJS

**A compact, embeddable JavaScript engine built to pursue V8-class sustained performance with dramatically lower startup cost, memory usage, binary size, and integration overhead.**

TurboJS is designed for applications that need serious JavaScript execution without carrying the weight of a browser-scale runtime. Its goal is ambitious and direct: deliver performance that can move increasingly close to V8 on long-running workloads while remaining much smaller, faster to initialize, easier to embed, and more economical in memory.

Rather than paying the full cost of an optimizing compiler at startup, TurboJS begins with a lean interpreter and progressively invests in hotter code. Baseline compilation, runtime feedback, inline caching, on-stack replacement, speculative optimization, deoptimization, native code caching, and AOT support work together as one execution pipeline.

> **Current release candidate:** `0.16.0-rc.6`  
> **Validation:** 293 full-release build targets and 97/97 native tests passing  
> **Platforms:** Windows, Linux, and macOS; x86-64 JIT with ARM64 backend development

## The goal

Modern JavaScript engines can achieve extraordinary throughput, but that performance often comes with substantial startup work, memory commitment, binary size, platform complexity, and integration cost. TurboJS is being built around a different balance.

The project aims to combine:

- sustained execution performance that moves toward V8-class territory;
- significantly lower startup latency;
- substantially lower memory usage;
- a much smaller deployable engine;
- a direct and stable C embedding interface;
- progressive optimization that spends resources only on code proven to be hot;
- a clean source tree whose runtime and compiler boundaries remain understandable.

This makes TurboJS especially compelling for native applications, developer tools, game engines, edge software, embedded systems, command-line runtimes, sandboxed execution, and other environments where a full browser runtime is unnecessary or too expensive.

### Current TurboJS vs V8 whole-engine parity snapshot

This comparison is designed to represent **general engine behavior**, not isolated best-case kernels. It excludes fixed-input leaf-call loops, pure recursion microbenchmarks, and analytical workloads that can collapse into unusually specialized native paths.

Both engines execute identical JavaScript with identical externally supplied runtime seeds. The seed changes for every timed sample, preventing the compiler from treating the workload as a fixed result. Every workload is validated by exact checksum equality between TurboJS and Node/V8. The table reports the median of five independent process runs; each process performs two warmups and seven timed samples per workload.

The ratio is **TurboJS time ÷ Node/V8 time**. `1.00x` is parity, and lower is better.

| Whole-engine workload | TurboJS median | Node/V8 median | TurboJS / V8 |
|---|---:|---:|---:|
| Numeric simulation | 18.166 ms | 0.595 ms | 30.51x |
| Order processing | 35.590 ms | 9.930 ms | 3.58x |
| AST processing pipeline | 8.558 ms | 1.301 ms | 6.58x |
| Polymorphic event routing | 49.721 ms | 4.728 ms | 10.52x |
| Graph analytics | 12.975 ms | 1.233 ms | 10.53x |
| Log parsing and text indexing | 16.199 ms | 4.924 ms | 3.29x |
| Collection transforms | 30.075 ms | 5.099 ms | 5.90x |
| Dynamic state machine | 55.276 ms | 1.828 ms | 30.25x |
| Configuration and template rendering | 120.518 ms | 6.492 ms | 18.56x |
| Allocation lifecycle | 77.184 ms | 4.988 ms | 15.47x |
| **Total of workload medians** | **424.264 ms** | **41.117 ms** | **10.32x** |
| **Geometric-mean ratio** | — | — | **10.30x** |
| **Median workload ratio** | — | — | **10.52x** |

The most defensible current whole-engine result is therefore approximately **10.3x Node/V8 time by geometric mean** on this host. TurboJS is closest on order processing, text indexing, collection transforms, and AST-style application code. The largest remaining gaps are dynamic numeric loops, branch-heavy state machines, allocation-heavy object lifecycles, and mixed string/object template generation.

This result intentionally does not include the earlier monomorphic-call and fixed-recursion outliers. Those measurements remain useful for validating specific optimization paths, but they are not representative of general whole-engine parity.

The reproducible suite is [`tests/benchmarks/parity/whole_engine_parity.js`](tests/benchmarks/parity/whole_engine_parity.js), the runner is [`scripts/benchmark_parity.py`](scripts/benchmark_parity.py), and the consolidated raw results are retained in [`benchmarks/results/whole-engine-parity.json`](benchmarks/results/whole-engine-parity.json).

Benchmark results are machine- and build-specific and should be reproduced on target hardware before making deployment decisions.

TurboJS does not claim complete V8 parity today. It is an active engine focused on closing the sustained-performance gap while preserving the compactness and responsiveness that motivated the project from the beginning.

## Why TurboJS

TurboJS is not a thin command wrapper and it is not only a bytecode interpreter. It contains the machinery expected from a modern high-performance JavaScript engine:

- a JavaScript parser, compiler, bytecode format, and interpreter;
- a fast baseline compilation tier;
- a feedback-directed SSA optimizing compiler;
- runtime inline caches and generation-checked compiled call entries;
- on-stack replacement for hot loops;
- precise deoptimization back into safe lower-tier execution;
- native code caching, invalidation, and dependency tracking;
- portable ahead-of-time module support;
- a stable C API for embedding the engine into native applications.

The engine is deliberately structured as a professional systems project rather than a monolithic source drop. Public headers, runtime internals, compiler tiers, machine-code backends, tests, tools, generated assets, and platform support each have explicit ownership boundaries.

## Execution pipeline

TurboJS uses a staged execution pipeline that keeps cold code inexpensive while allowing hot code to become progressively more specialized.

| Component | Purpose |
|---|---|
| **Rotor** | Parses JavaScript and emits validated TurboJS bytecode. |
| **Pulse** | Executes cold code and preserves canonical language semantics. |
| **Spool** | Quickly compiles hot bytecode with Pulse-compatible frame state. |
| **Telemetry** | Records value kinds, call targets, object shapes, and execution behavior. |
| **Relay** | Hosts property, element, and call-site inline caches. |
| **Clutch** | Connects guarded call sites to generation-checked native entries. |
| **Redline** | Builds specialized SSA and optimized native regions. |
| **Slipstream** | Transfers active execution into optimized code through OSR. |
| **Rewind** | Reconstructs lower-tier state when speculation fails. |
| **Gearbox** | Lowers IR, allocates registers, and emits machine code. |
| **Vault** | Owns executable memory, native entries, aging, and invalidation. |
| **Forge** | Produces portable or native AOT modules. |

```text
JavaScript source
      │
      ▼
    Rotor ───────────────────────────────────────────► Forge
      │
      ▼
    Pulse ── hot ──► Spool ── stable feedback ──► Redline
      ▲                 │              │               │
      │                 └── Relay + Clutch ────────────┤
      │                                                │
      └──────── Rewind ◄────── Slipstream ─────────────┘

                Gearbox emits native code into Vault
```

The result is an engine that can start with a small runtime footprint, avoid unnecessary compilation work, and concentrate its most expensive optimizations on the code paths that matter most.

## Shared graph regions

The current engine introduces shared application-region graphs for larger real-world execution patterns. Redline can reason across grouped accumulator loops, callback routing, record processing, AST-style visitors, and coupled Float64 workloads instead of treating each hot operation as an isolated fragment.

The shared-region runtime also includes:

- polymorphic Clutch publication for observed call targets;
- dense-array fast paths integrated with OSR;
- shared graph ownership and region reuse;
- native continuation and dependency tracking;
- runtime-safe AVX2/FMA dispatch on supported x86-64 processors;
- cross-platform monotonic timing and aligned memory support.

These capabilities move TurboJS beyond isolated micro-optimizations and toward sustained optimization of larger application-level regions.

## Build

### Windows

Requirements:

- CMake
- Ninja
- Python 3
- LLVM/Clang

The included PowerShell launcher discovers both `clang.exe` and `llvm-rc.exe` automatically:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build-windows.ps1 -Preset full-release -Jobs 8
```

Run the engine:

```powershell
.\build\full-release\turbojs.exe --version
.\build\full-release\turbojs.exe -e "console.log('TurboJS:', 6 * 7)"
```

### Linux and macOS

```bash
python3 scripts/build.py --preset full-release --fresh --jobs 8
./build/full-release/turbojs --version
./build/full-release/turbojs -e "console.log('TurboJS:', 6 * 7)"
```

### Direct CMake workflow

```bash
cmake --preset full-release
cmake --build --preset full-release --parallel 8
ctest --preset full-release
```

## Embed TurboJS

```c
#include <stdio.h>
#include <string.h>
#include <turbojs/turbojs.h>

int main(void)
{
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *context = runtime ? JS_NewContext(runtime) : NULL;
    const char source[] = "21 * 2";
    JSValue value;
    int32_t result = 0;

    if (!context)
        return 1;

    value = JS_Eval(context, source, strlen(source), "embed.js",
                    JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(value) || JS_ToInt32(context, &result, value)) {
        JS_FreeValue(context, value);
        JS_FreeContext(context);
        JS_FreeRuntime(runtime);
        return 1;
    }

    printf("%d\n", result);
    JS_FreeValue(context, value);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
    return 0;
}
```

See [`docs/embedding/README.md`](docs/embedding/README.md) for ownership rules, error handling, runtime configuration, and integration examples.

## Testing

Run the complete native suite:

```bash
python3 scripts/test.py --preset full-release --no-build
```

Or directly:

```bash
ctest --test-dir build/full-release --output-on-failure
```

The current source passes all **97 native tests**, covering the parser/runtime boundary, JIT IR, SSA optimization, OSR, deoptimization, inline caches, compiled calls, Float64 lowering, SIMD kernels, application regions, embedding, lifecycle stress, and native-code ownership.

### Test262

Fetch the official suite:

```bash
python3 scripts/fetch_test262.py
```

Run the full profile:

```bash
python3 scripts/test262.py \
  --engine build/full-release/turbojs \
  --suite third_party/test262 \
  --profile full \
  --workers 8 \
  --timeout 5 \
  --resume \
  --allow-failures \
  --report build/test262-full-report.json
```

On Windows, use `build\full-release\turbojs.exe` for the engine path.

## Repository structure

```text
apps/                 CLI and product entry points
benchmarks/           Focused engine and JIT benchmarks
cmake/                Build policy, targets, and source manifests
docs/                 Durable architecture and contributor documentation
examples/             Native embedding examples
generated/            Reproducible generated engine sources
include/turbojs/      Public embedding SDK
scripts/              Build, test, benchmark, and packaging workflows
src/                  Engine implementation
tests/                Native unit, VM, JIT, and integration tests
third_party/          Optional external suites and dependencies
tools/                AOT, generation, validation, and maintenance tools
```

## Design principles

**Performance without browser-scale weight.** TurboJS is built to pursue high sustained throughput without inheriting the full startup, memory, size, and integration costs of a browser runtime.

**Compact by default.** Cold code should not pay the full cost of an optimizing compiler.

**Optimize from evidence.** Telemetry drives specialization, tiering, and call-target decisions.

**Invest progressively.** Compilation effort increases only after runtime behavior demonstrates that the investment is worthwhile.

**Fail safely.** Speculative code must deoptimize into a valid lower-tier frame rather than corrupt execution.

**Embed cleanly.** The engine exposes a direct C API and does not require a browser, event loop, or application framework.

**Keep architecture visible.** Runtime ownership and compiler boundaries are represented directly in the repository layout.

## Project status

TurboJS is an active release candidate and not yet a drop-in replacement for every V8 or Node.js workload. Its strongest current areas are compact embedding, startup-sensitive applications, native runtime integration, tiered execution, array and numeric specialization, application-region optimization, and controlled long-running workloads.

The long-term direction is clear: continue raising sustained JavaScript performance toward V8-class execution while protecting the properties that make TurboJS distinct—fast startup, low memory use, small binaries, direct embedding, and a clean systems-oriented architecture.

Ongoing work includes broader JavaScript compatibility, wider optimizing-compiler coverage, production ARM64 code generation, more aggressive inlining and escape analysis, expanded GC/JIT stress validation, and reproducible cross-platform release packaging.

See the [`roadmap`](docs/project/ROADMAP.md), [`release status`](docs/project/RELEASE_STATUS.md), and [`architecture overview`](docs/architecture/overview.md).

## Contributing

Contributions should preserve the engine’s ownership boundaries, test new behavior at the narrowest useful layer, and include benchmark evidence for performance-sensitive changes.

- [Contributing guide](CONTRIBUTING.md)
- [Security policy](SECURITY.md)
- [Support](SUPPORT.md)
- [Code of conduct](CODE_OF_CONDUCT.md)

## License

See [`LICENSE`](LICENSE) and [`NOTICE.md`](NOTICE.md).
