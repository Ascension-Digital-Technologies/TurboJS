# TurboJS

**TurboJS is a lightweight embeddable JavaScript engine with an interpreter, x86-64 baseline JIT, feedback-driven optimizing JIT, and portable AOT modules.**

TurboJS is designed for applications that need fast startup, controlled memory use, native embedding, and an execution pipeline that remains understandable. The project keeps cold code in the interpreter, promotes hot functions to a compact baseline compiler, and only invokes the optimizing tier when runtime feedback is stable.

> **97%+ targeted core JavaScript coverage:** TurboJS passed **5,833 of 5,988 non-skipped executions (97.41%)** in the published 6,000-test Test262 core baseline. In the complete single-variant run, the JavaScript `language/` category passed **96.02%**. Intl, Temporal, and unsupported host hooks are reported separately so the scope remains explicit.

> **Project status:** v1 engineering release. The numeric and runtime-helper JIT/AOT pipeline is implemented and tested. Unsupported optimized shapes safely remain in the baseline tier or interpreter. See [RELEASE_STATUS.md](RELEASE_STATUS.md) for exact scope and limitations.

## Highlights

- Compact interpreter and bytecode compiler
- x86-64 baseline JIT with checked arithmetic, branches, loops, locals, and runtime exits
- Typed SSA optimizing IR with CFGs, dominators, loops, phi nodes, type guards, folding, and DCE
- Runtime type feedback and automatic interpreter → baseline → optimizing promotion
- Precise bailout metadata, deoptimization frames, boxed value reconstruction, GC stack maps, and safepoints
- Runtime-helper dispatch ABI with exceptions and continuation
- Portable checksummed `TJIR` function images and multi-function `TJM1` AOT modules
- Cross-platform CMake presets and Python/Bash/PowerShell/Batch developer scripts
- Focused differential tests and benchmark reporting

## Execution architecture

```text
JavaScript source
      │
      ▼
Parser and bytecode compiler
      │
      ▼
TurboJS bytecode ───────────────► Portable AOT module (.tjm)
      │                                  │
      ▼                                  ▼
Interpreter ── hot ──► Baseline JIT ── stable feedback ──► Optimizing JIT
      ▲                     │                                  │
      └──── fallback ◄──────┴──────── deoptimization ◄─────────┘
```

The engine fails closed: unsupported bytecode, unstable types, failed guards, arithmetic edge cases, and dynamic JavaScript operations return to a safe runtime path instead of being guessed or miscompiled.

## Repository layout

```text
apps/                 CLI application sources
runtime/              Optional runtime and libc integration
src/
  api/                Public C API and API implementation
  internal/           Private engine contracts
  compiler/           Lexer, parser, and bytecode compilation
  vm/                 Interpreter and VM call machinery
  runtime/            Runtime state, atoms, strings, and classes
  objects/            Object representation and shapes
  gc/                 Allocation, tracing, and collection
  builtins/            JavaScript built-ins
  modules/             Module parsing, linking, and evaluation
  serialization/       Bytecode serialization
  numeric/             Numeric helpers
  regexp/              Regular-expression engine
  unicode/             Unicode tables and algorithms
  jit/                 Baseline JIT, SSA optimizer, AOT, deopt, and cache
  generated/           Reproducible generated engine sources
examples/              Embedding, JIT, and profiling examples
tests/                 Unit, differential, VM, JIT, and AOT tests
tools/                 Generators, validation, amalgamation, and AOT tools
scripts/               Cross-platform developer workflow

docs/architecture/     Design documentation
docs/development/      Build, test, and contribution guidance
docs/specifications/   Versioned artifact formats and contracts
```

## Prerequisites

- CMake 3.20 or newer
- Ninja recommended
- C11-capable compiler
  - Clang or GCC on Linux/macOS
  - Clang, clang-cl, or MSVC-compatible toolchain on Windows
- Python 3.9 or newer for repository scripts and generators

The x86-64 native backend currently supports Windows x64 and System V x86-64 hosts. Other hosts retain interpreter and portable AOT functionality where supported by the base engine.

## Quick start

### Focused JIT development build

```bash
python scripts/build.py --preset jit-dev --fresh
python scripts/test.py --preset jit-dev
```

Equivalent CMake commands:

```bash
cmake --preset jit-dev
cmake --build --preset jit-dev
ctest --preset jit-dev
```

### Full release build

```bash
python scripts/build.py --preset full-release --fresh
python scripts/test.py --preset full-release
```

### Windows PowerShell

```powershell
.\scripts\run.ps1 build --preset jit-dev --fresh
.\scripts\run.ps1 test --preset jit-dev
```

### Windows Command Prompt

```bat
scripts\run.bat build --preset jit-dev --fresh
scripts\run.bat test --preset jit-dev
```

## Test262 conformance suite

Test262 is **not vendored** in source archives. Fetch or update it from the official TC39 repository when needed:

```bash
python scripts/fetch_test262.py
```

Then configure and run either profile:

```bash
cmake -S . -B build/test262 -DTURBOJS_BUILD_TEST262_RUNNER=ON
cmake --build build/test262 --target run-test262-core
cmake --build build/test262 --target run-test262
```

The fetched checkout lives at `third_party/test262/` and is ignored by Git.

## Developer workflow

```bash
python scripts/configure.py --preset jit-dev
python scripts/build.py --preset jit-dev
python scripts/test.py --preset jit-dev
python scripts/benchmark.py --preset jit-dev --runs 20
python scripts/validate.py --preset jit-dev
python scripts/sanitize.py --kind address-undefined
python scripts/package.py --name turbojs-source
python scripts/clean.py --preset jit-dev
```

See [scripts/README.md](scripts/README.md) for every command, output location, and platform-specific note.

## Embedding overview

Public headers live in `src/api/`. A minimal host creates a runtime and context, evaluates source, handles exceptions, and destroys resources in reverse order. New embedding code should use TurboJS-owned API names and must not include private headers from `src/internal/`.

```c
#include "turbojs.h"

int main(void) {
    JSRuntime *runtime = JS_NewRuntime();
    if (!runtime) return 1;

    JSContext *context = JS_NewContext(runtime);
    if (!context) {
        JS_FreeRuntime(runtime);
        return 1;
    }

    const char source[] = "40 + 2";
    JSValue result = JS_Eval(
        context,
        source,
        sizeof(source) - 1,
        "example.js",
        JS_EVAL_TYPE_GLOBAL
    );

    if (JS_IsException(result)) {
        JSValue exception = JS_GetException(context);
        JS_FreeValue(context, exception);
    }

    JS_FreeValue(context, result);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
    return 0;
}
```

See `examples/` and the architecture documents for JIT-specific APIs, runtime-helper registration, AOT loading, and tier statistics.

## JIT tiers

### Interpreter

The interpreter remains the semantic source of truth and handles all supported JavaScript behavior. It is used for cold functions, unsupported JIT instructions, unstable feedback, exceptions, and deoptimization fallback.

### Baseline JIT

The baseline compiler prioritizes low compile latency and predictable native code. Its verified IR supports arguments, constants, locals, arithmetic, comparisons, branches, loops, checked division/remainder, runtime-helper exits, and returns. Stack-backed virtual registers keep the backend compact.

### Optimizing JIT

The optimizing tier is entered only after feedback policy approval. It uses typed SSA values, explicit basic blocks, CFG edges, dominators, loop metadata, phi nodes, specialization guards, constant folding, branch folding, and dead-value elimination. Unsupported graphs stay on the baseline tier.

### Deoptimization and GC cooperation

Compiled code publishes precise bytecode positions, live-value masks, stack maps, and safepoints. Checked arithmetic, guard failures, runtime requests, and helper calls can capture a frame, root or relocate heap references, execute a slow path, and resume without replaying the function prefix where supported.

## AOT artifacts

TurboJS defines two portable formats:

- **TJIR** — a versioned, checksummed serialized IR function
- **TJM1** — a checksummed multi-function module with named exports and embedded TJIR images

Inspect a module with:

```bash
turbojs-aot-inspect application.tjm
```

The loader validates magic, version, declared sizes, function-table bounds, embedded images, and checksums before exposing executable content. See [docs/specifications/tjm-module-format.md](docs/specifications/tjm-module-format.md).

## Tests

The focused suite covers IR verification, interpreter/native differential execution, register pressure, bytecode translation, locals, branches, loops, checked numeric behavior, code caching, deoptimization, GC maps, runtime safepoints, helper dispatch, type feedback, SSA optimization, CFG analysis, automatic promotion, and AOT modules.

```bash
python scripts/test.py --preset jit-dev
```

Run one group:

```bash
python scripts/test.py --preset jit-dev --filter turbojs.jit
```

Every optimization should add a differential test that compares the interpreter, baseline JIT, optimizing tier, and AOT-loaded execution whenever those routes support the operation.

## Benchmarks

```bash
python scripts/benchmark.py --preset jit-dev --runs 20
```

The runner reports warmups, raw samples, median, mean, minimum, and maximum process times, and writes machine-readable JSON. Benchmarks must distinguish process-level timing from isolated generated-code throughput and must not make unsupported comparisons.

## Documentation map

- [Architecture overview](docs/architecture/overview.md)
- [JIT/AOT roadmap](docs/architecture/jit-aot-roadmap.md)
- [Intermediate representation](docs/architecture/intermediate-representation.md)
- [Optimizing SSA](docs/architecture/optimizing-ssa.md)
- [Optimizing CFG](docs/architecture/optimizing-cfg.md)
- [Automatic tiering](docs/architecture/automatic-tiering.md)
- [Deoptimization frames](docs/architecture/deoptimization-frames.md)
- [Runtime safepoints](docs/architecture/runtime-safepoints.md)
- [Runtime helper dispatch](docs/architecture/runtime-helper-dispatch.md)
- [Building](docs/development/building.md)
- [Testing](docs/development/testing.md)
- [TJM module format](docs/specifications/tjm-module-format.md)

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting changes. Important rules:

- Preserve interpreter correctness as the source of truth.
- Unsupported compiler cases must return a defined fallback status.
- Do not add recursive source globbing to build files.
- Public API changes require documentation and tests.
- Generated files must be reproducible from checked-in generators.
- Performance changes need correctness tests and benchmark evidence.

## Security

Do not report security vulnerabilities in public issues. Follow [SECURITY.md](SECURITY.md) for private reporting guidance and include a minimal reproduction, affected revision, platform, and impact assessment.

## Support and project scope

Use [GitHub Discussions](SUPPORT.md) for usage questions and design conversations; use issues for reproducible defects and scoped feature work. The exact supported v1 surface and deferred work are documented in [RELEASE_STATUS.md](RELEASE_STATUS.md).

## License

A project license has not been selected in this repository snapshot. Select and add an OSI-approved `LICENSE` before publishing a public source release. Contributors should not assume redistribution rights until that file exists.

## ECMAScript conformance with Test262

TurboJS does not vendor the large TC39 Test262 checkout. Fetch it on demand, then use the metadata-aware parallel runner:

```bash
python scripts/fetch_test262.py
cmake -S . -B build/test262 -DTURBOJS_BUILD_TEST262_RUNNER=ON
cmake --build build/test262 --target run-test262-core
```

The runner supports strict/sloppy/module variants, harness includes, negative
tests, timeouts, filtering, JSON reports, and deterministic sharding. See
[`tests/test262/README.md`](tests/test262/README.md) and the checked-in
[`BASELINE_RESULTS.md`](tests/test262/BASELINE_RESULTS.md).
