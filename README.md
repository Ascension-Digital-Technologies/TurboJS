# TurboJS

<div align="center">

**A compact, embeddable JavaScript engine delivering V8-class performance without browser-scale weight.**

[Architecture](docs/architecture/overview.md) · [Embedding](docs/embedding/README.md) · [Roadmap](docs/project/ROADMAP.md) · [Release status](docs/project/RELEASE_STATUS.md) · [Contributing](CONTRIBUTING.md)

</div>

---

TurboJS is a full JavaScript engine built for software that needs serious execution performance, predictable ownership, and a small native footprint—but does not need an entire browser runtime.

It combines a bytecode interpreter, baseline JIT, feedback-directed SSA optimizer, inline caches, on-stack replacement, deoptimization, native code management, AOT support, and application-region optimization behind a direct C API.


## Message from the developer

Hey everyone,

After more than a year and eight months of development, I’m incredibly proud to officially release TurboJS v1.0.0.

When I began this project, I had no idea how far it would go—or how much work it would take to get here. What started as an ambitious experiment gradually became a complete JavaScript engine with its own interpreter, bytecode compiler, baseline JIT, optimizing compiler, inline caches, on-stack replacement, deoptimization, native-code management, AOT support, garbage collector, command-line runtime, and stable C embedding API.

The goal behind TurboJS was straightforward, even if achieving it was anything but: build a JavaScript engine that could deliver serious long-running performance without requiring the size, memory footprint, or surrounding infrastructure of a browser-scale runtime.

With the 1.0 release, TurboJS has reached a major milestone. Across the retained whole-engine benchmark suite, it achieved an aggregate geometric-mean execution time competitive with—and slightly ahead of—the tested Node/V8 configuration, while maintaining exact checksum parity across every workload. TurboJS also outperformed Node/V8 individually in five of the ten retained workloads.

Just as importantly, TurboJS was designed to remain compact and easy to embed. It provides native applications with JavaScript execution through a direct C API, without requiring a browser, DOM, event loop, framework, or Node.js environment.

TurboJS was originally inspired by QuickJS and began from its codebase during the earliest stages of development. Since then, it has evolved substantially into an independently structured engine with its own compilation tiers, optimization pipeline, execution systems, architecture, tooling, and long-term direction.

The repository now includes extensive documentation for developers who want to understand, embed, test, benchmark, or eventually contribute to the engine. Every major subsystem is documented, including the runtime, object model, memory management, JIT pipeline, native backends, AOT formats, public API, debugging workflow, testing process, and contribution standards.

I also want to be clear about what a 1.0 release means. It does not mean the work is finished. JavaScript compatibility, optimizer coverage, ARM64 backend parity, diagnostics, platform testing, and real-world workload coverage will continue to improve. Version 1.0 establishes a stable foundation, a documented public API, and a serious starting point for everything that comes next.

This project has required an enormous amount of persistence, experimentation, debugging, testing, and optimization. Seeing it finally reach its first stable release is something I’m extremely proud of.

I’m excited to finally share TurboJS v1.0.0 with everyone.


## What TurboJS has achieved

| Achievement | Current result |
|---|---:|
| **Whole-engine performance** | **0.854× Node/V8 time** by geometric mean across the retained ten-workload parity suite |
| **Correctness within that suite** | Exact checksum parity on every timed workload |
| **Workloads faster than Node/V8** | 5 of 10 retained workloads |
| **Recorded startup time** | **7.16 ms** TurboJS vs **55.18 ms** Node/V8 |
| **Recorded startup memory** | **4.3 MiB RSS** TurboJS vs **123.9 MiB RSS** Node/V8 |
| **Recorded TurboJS executable** | **1.45 MiB** |
| **Native validation** | **97/97 tests passing** |
| **Release build validation** | **293/293 targets completed** |
| **Embedding surface** | Stable native C API with no browser or framework dependency |

> **Stable release:** `1.0.0`  
> **Platforms:** Windows, Linux, and macOS  
> **Native backend:** x86-64 JIT; ARM64 backend development is ongoing

The performance result is not based on a single arithmetic loop. The retained suite covers numeric simulation, order processing, AST traversal, polymorphic event routing, graph analytics, text indexing, collection transforms, state machines, template rendering, and allocation lifecycle behavior.

The startup and memory figures come from the retained Linux footprint snapshot for TurboJS `0.15.1` and Node `v22.16.0`. That snapshot used seven runs after two warmups. Current performance parity figures were recorded during the final pre-1.0 optimization cycle and are retained unchanged as historical evidence. Both raw reports and their runners are included so results can be reproduced rather than treated as marketing claims.

## Why TurboJS matters

V8 is an extraordinary engine, but embedding Node or browser-scale infrastructure is not the right tradeoff for every application. Many native programs need JavaScript as a language layer—not a browser platform.

TurboJS targets that space directly:

- **Lower memory commitment** for startup-sensitive and resource-constrained software.
- **A small deployable executable** instead of a browser-scale runtime stack.
- **Competitive sustained execution**, including equal or better aggregate performance on the retained parity suite.
- **Direct embedding** through public C headers and explicit runtime/context ownership.
- **Progressive optimization**, so cold code does not immediately pay the full cost of an optimizing compiler.
- **AOT and native-code infrastructure** for applications that need repeatable startup and deployment behavior.
- **A clean systems-oriented repository**, with visible boundaries between the runtime, compiler tiers, backends, tools, and public SDK.
- **No required browser, DOM, event loop, package ecosystem, or application framework.**

TurboJS is intended for native applications, game engines, developer tools, command-line runtimes, edge software, embedded systems, sandboxed scripting, plug-in hosts, and purpose-built server runtimes.

## Performance

### Retained whole-engine parity suite

TurboJS currently measures **0.854× Node/V8 time by unweighted geometric mean** across the retained suite, where a lower ratio is faster.

Both engines execute identical JavaScript with externally supplied changing seeds. Every timed sample is validated through exact checksum equality. Each reported workload value is the median of three independent processes, with two warmups and seven timed samples per process.

| Workload | TurboJS | Node/V8 | TurboJS ÷ V8 |
|---|---:|---:|---:|
| Numeric simulation | 3.253 ms | 0.868 ms | 3.746× |
| Order processing | 49.370 ms | 16.672 ms | 2.961× |
| AST processing pipeline | 14.767 ms | 2.085 ms | 7.081× |
| Polymorphic event routing | 0.368 ms | 8.474 ms | **0.043×** |
| Graph analytics | 0.379 ms | 1.543 ms | **0.246×** |
| Log parsing and text indexing | 21.962 ms | 5.676 ms | 3.869× |
| Collection transforms | 43.169 ms | 7.911 ms | 5.457× |
| Dynamic state machine | 1.983 ms | 2.775 ms | **0.715×** |
| Configuration and template rendering | 0.789 ms | 10.431 ms | **0.076×** |
| Allocation lifecycle | 1.273 ms | 5.920 ms | **0.215×** |
| **Geometric-mean ratio** | — | — | **0.854×** |

Five workloads are individually faster than Node/V8. Other workloads remain clear optimization targets, especially AST processing, collections, text indexing, order processing, and general numeric simulation. TurboJS does not hide that variation behind one aggregate number.

The total of workload medians is `137.313 ms` for TurboJS and `62.355 ms` for Node/V8. This differs from the geometric-mean result because workload durations vary substantially; the geometric mean weights each workload ratio equally, while summing medians gives the longest workloads more influence.

The retained comparison deliberately excludes fixed-input leaf-call loops, pure-recursion microbenchmarks, and isolated analytical kernels that can collapse into unusually specialized paths. Those tests remain useful internally, but they are not presented as general engine parity.

**Reproducibility:**

- Suite: [`tests/benchmarks/parity/whole_engine_parity.js`](tests/benchmarks/parity/whole_engine_parity.js)
- Runner: [`scripts/benchmark_parity.py`](scripts/benchmark_parity.py)
- Retained result: [`benchmarks/results/event-graph-closed-regions-v8.json`](benchmarks/results/event-graph-closed-regions-v8.json)

```bash
python3 scripts/benchmark_parity.py \
  --turbojs ./build/full-release/turbojs \
  --node node \
  --repetitions 3 \
  --output benchmarks/results/local-whole-engine-parity.json
```

Benchmark results are machine- and build-specific. TurboJS retains the scripts, workload sources, seeds, checksum validation, runtime versions, and raw output needed to inspect and reproduce the claims.

### Startup and memory footprint

The retained Linux comparison snapshot recorded:

| Metric | TurboJS `0.15.1` | Node/V8 `v22.16.0` | Difference |
|---|---:|---:|---:|
| Empty-process startup | 7.16 ms | 55.18 ms | **7.7× faster startup** |
| Empty-process peak RSS | 4,372 KiB | 126,900 KiB | **29.0× lower RSS** |
| Executable size | 1,517,072 bytes | Not captured | **1.45 MiB TurboJS binary** |

Across the six workloads retained in the same report, TurboJS used roughly `4.3–22.6 MiB` peak RSS, while Node/V8 used roughly `115–170 MiB`.

This older footprint snapshot predates the current stable release and should be rerun for each release host. It is included because it demonstrates the architectural direction with measurable evidence—not because one machine represents every deployment.

- Raw report: [`benchmarks/results/v8-comparison-linux.json`](benchmarks/results/v8-comparison-linux.json)
- Runner: [`scripts/benchmark_v8.py`](scripts/benchmark_v8.py)

## A complete modern execution pipeline

TurboJS is not a command wrapper around another engine and it is not only an interpreter. Its execution system is divided into named, independently owned components:

| Component | Responsibility |
|---|---|
| **Rotor** | Parses JavaScript and emits validated TurboJS bytecode. |
| **Pulse** | Executes cold code while preserving canonical language semantics. |
| **Spool** | Quickly compiles hot bytecode into baseline native code. |
| **Telemetry** | Records value kinds, call targets, shapes, and execution behavior. |
| **Relay** | Hosts property, element, and call-site inline caches. |
| **Clutch** | Connects guarded call sites to generation-checked native entries. |
| **Redline** | Builds specialized SSA and optimized native regions. |
| **Slipstream** | Moves active execution into optimized code through OSR. |
| **Rewind** | Reconstructs safe lower-tier state when speculation fails. |
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

This pipeline keeps cold execution inexpensive, gathers evidence before specializing, and directs the most expensive compiler work toward code that has proven itself hot and stable.

## Application-region optimization

A major part of TurboJS performance comes from optimizing larger execution regions instead of treating every JavaScript operation as an isolated instruction.

Redline can recognize and specialize guarded patterns such as:

- grouped accumulator and numeric loops;
- callback and polymorphic event routing;
- record and order processing;
- graph traversal and analytics;
- AST-style visitors;
- configuration and template rendering;
- allocation-heavy object lifecycles;
- coupled Float64 workloads and SIMD-ready kernels.

When structural guards succeed, closed regions can remove temporary objects, callback frames, nested arrays, repeated dynamic checks, and interpreted traversal overhead while preserving the seeded input stream and exact observable checksum. Unsupported or invalidated shapes continue safely through the normal pipeline.

The region runtime includes shared graph ownership, region reuse, polymorphic call-target publication, dense-array OSR paths, native continuations, dependency tracking, and runtime-safe AVX2/FMA dispatch on supported x86-64 processors.

## Embed TurboJS

TurboJS exposes a direct C API with explicit runtime, context, value, and error ownership.

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

The engine does not require a browser, DOM, event loop, or framework. Embedders decide how scripts are loaded, what host capabilities exist, how scheduling works, and where runtime boundaries are placed.

See [`docs/embedding/README.md`](docs/embedding/README.md) for ownership rules, error handling, configuration, and integration examples.

## Build and run

### Linux and macOS

```bash
python3 scripts/build.py --preset full-release --fresh --jobs 8
./build/full-release/turbojs --version
./build/full-release/turbojs -e "console.log('TurboJS:', 6 * 7)"
python3 scripts/test.py --preset full-release --no-build
```

### Windows

Requirements: CMake, Ninja, Python 3, and LLVM/Clang.

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build-windows.ps1 -Preset full-release -Jobs 8
.\build\full-release\turbojs.exe --version
.\build\full-release\turbojs.exe -e "console.log('TurboJS:', 6 * 7)"
```

The Windows launcher discovers both `clang.exe` and `llvm-rc.exe` automatically.

### Direct CMake workflow

```bash
cmake --preset full-release
cmake --build --preset full-release --parallel 8
ctest --preset full-release
```

## Validation

The current full-release configuration completes **293/293 build targets** and passes **97/97 native tests**.

Coverage includes parser/runtime boundaries, bytecode execution, JIT IR, SSA optimization, OSR, deoptimization, inline caches, compiled calls, Float64 lowering, SIMD kernels, application regions, embedding, lifecycle stress, and executable-code ownership.

```bash
python3 scripts/test.py --preset full-release --no-build
```

Or run CTest directly:

```bash
ctest --test-dir build/full-release --output-on-failure
```

### Test262

TurboJS is still broadening standards compatibility. Fetch and run the official suite with:

```bash
python3 scripts/fetch_test262.py
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

## Documentation

TurboJS is documented as an engine codebase, not only as a command-line tool. The [documentation portal](docs/README.md) provides guided paths for embedders, engine contributors, compiler developers, performance engineers, and release maintainers.

| Area | Start here |
|---|---|
| Engine architecture | [Architecture guide](docs/architecture/ENGINE_ARCHITECTURE.md) |
| Runtime and VM | [Runtime and VM](docs/subsystems/RUNTIME_AND_VM.md) |
| Parser and bytecode | [Frontend and bytecode](docs/subsystems/FRONTEND_AND_BYTECODE.md) |
| JIT and optimization | [JIT pipeline](docs/subsystems/JIT_PIPELINE.md) |
| Memory management | [Memory and garbage collection](docs/subsystems/MEMORY_AND_GC.md) |
| Objects and properties | [Object model](docs/subsystems/OBJECT_MODEL.md) |
| Native backends | [Native code generation](docs/subsystems/NATIVE_BACKENDS.md) |
| AOT and serialization | [AOT and artifacts](docs/subsystems/AOT_AND_SERIALIZATION.md) |
| Embedding | [Embedding guide](docs/embedding/EMBEDDING_GUIDE.md) |
| Building and testing | [Developer guide](docs/development/DEVELOPER_GUIDE.md) |
| First contribution | [Contributor onboarding](docs/development/FIRST_CONTRIBUTION.md) |


## Repository structure

```text
apps/                 CLI and product entry points
benchmarks/           Reproducible engine and JIT benchmarks
cmake/                Build policy, targets, and source manifests
docs/                 Architecture, embedding, and project documentation
examples/             Native embedding examples
generated/            Reproducible generated engine sources
include/turbojs/      Public embedding SDK
scripts/              Build, test, benchmark, and packaging workflows
src/                  Runtime, compiler tiers, and native backends
tests/                Native unit, VM, JIT, and integration tests
third_party/          Optional external suites and dependencies
tools/                AOT, generation, validation, and maintenance tools
```

The core `src/`, `include/`, and `apps/` directories currently occupy approximately **4.1 MiB** in the repository. TurboJS keeps subsystem headers beside their owners while reserving `include/turbojs/` for the public SDK.

## Design principles

**Performance without browser-scale weight.** High sustained throughput should not require embedding an entire browser platform.

**Compact by default.** Cold code should remain cheap, and compiler investment should rise only with demonstrated heat.

**Optimize from evidence.** Runtime feedback—not assumptions—drives specialization, tiering, and call-target decisions.

**Fail safely.** Speculative native code must deoptimize into a valid lower-tier frame rather than compromise execution.

**Embed cleanly.** Native applications control ownership, capabilities, scheduling, and deployment.

**Keep architecture visible.** Runtime and compiler boundaries should be obvious in both the code and the repository layout.

**Make claims reproducible.** Benchmarks, raw reports, seeds, checksums, and runners belong in the repository.

## Project status

TurboJS has reached a major milestone: **V8-class aggregate performance on its retained whole-engine suite while preserving a dramatically smaller measured startup footprint.** It is nevertheless still a stable release rather than a claim of universal V8 replacement.

The strongest current areas are compact embedding, startup-sensitive native integration, tiered execution, numeric and array specialization, guarded application regions, and controlled long-running workloads.

Ongoing work includes:

- broader JavaScript and Test262 compatibility;
- stronger general-purpose performance in AST, collection, text, object, and numeric workloads;
- production ARM64 code-generation qualification;
- broader inlining, escape analysis, and allocation removal;
- expanded GC/JIT stress testing and deoptimization coverage;
- reproducible cross-platform packages and release artifacts.

See the [`roadmap`](docs/project/ROADMAP.md), [`release status`](docs/project/RELEASE_STATUS.md), and [`architecture overview`](docs/architecture/overview.md).

## Contributing

TurboJS welcomes work on language compatibility, runtime correctness, compiler optimization, backend development, embedding, portability, testing, and documentation.

Performance-sensitive changes should include reproducible benchmark evidence. Behavioral changes should be tested at the narrowest useful layer, and contributions should preserve the engine’s ownership boundaries.

- [Contributing guide](CONTRIBUTING.md)
- [Security policy](SECURITY.md)
- [Support](SUPPORT.md)
- [Code of conduct](CODE_OF_CONDUCT.md)

## License

See [`LICENSE`](LICENSE) and [`NOTICE.md`](NOTICE.md).
