# Subsystem Map

This map answers three questions for every major directory: what it owns, what it may depend on, and where a developer should begin reading.

| Path | Responsibility | Primary entry points |
|---|---|---|
| `include/turbojs` | Installed public API and ABI declarations | `turbojs.h`, `turbojs_embed.h`, `jit.h` |
| `src/api` | Stable embedding implementation and C runtime integration | `turbojs_embed.c`, `turbojs-libc.c` |
| `src/core` | Values, runtime/context lifecycle, atoms, strings, functions, exceptions | `value_tags.c`, `version_api.c` |
| `src/compiler` | Parser-facing compiler and bytecode production | compiler source group |
| `src/vm` | Bytecode interpreter, execution frames, dispatch, feedback hooks | VM source group |
| `src/objects` | Object, array, property, shape, and callable semantics | object source group |
| `src/gc` | Tracing, cycle collection, roots, allocation accounting | GC source group |
| `src/builtins` | Standard built-in objects and functions | built-in source group |
| `src/modules` | Modules, eval, loaders, and source execution APIs | `eval_api.c` |
| `src/numeric` | Numeric parsing, conversion, formatting, and arithmetic helpers | `dtoa.c` |
| `src/regexp` | Regular-expression parser and executor | `regexp.c` |
| `src/unicode` | Unicode tables and character operations | `unicode.c` |
| `src/serialization` | Bytecode and persistent artifact readers/writers | bytecode reader/writer |
| `src/jit/frontend` | Bytecode analysis, CFG construction, frame-state lowering | frontend documents and tests |
| `src/jit/ir` | Portable IR, type model, verification, deopt metadata | `include/turbojs/jit.h` |
| `src/jit/optimizing` | SSA, specialization, range analysis, CSE, allocation removal | optimizer tests |
| `src/jit/backend` | x64 and ARM64 lowering and machine-code emission | backend tests |
| `src/jit/runtime` | Tiering, code ownership, helpers, OSR, deopt, stack maps | runtime tests |
| `src/jit/aot` | Portable IR and TJM module persistence | `portable_ir.c`, `module.c` |
| `apps` | User-facing executables | interpreter, compiler, WASI reactor |
| `tools` | Generators, validators, AOT inspection, benchmark automation | tool-specific entry points |
| `tests` | Unit, differential, integration, embedding, benchmark, Test262 | CTest registration |

## Dependency direction

Public API calls inward. Semantic runtime code must not depend on command-line applications. Backend-neutral JIT layers must not depend on a particular machine backend. Target backends may depend on IR and runtime ABI definitions, but language semantics must remain outside backend code. Tools may consume public or explicitly exported internal formats; production targets must not depend on test code.

## Finding the right test

- Language semantics: `tests/unit`, `tests/test262`.
- Public embedding behavior: `tests/embedding`.
- IR and optimizer contracts: `tests/jit`.
- VM-to-JIT integration: tests prefixed with `vm_`.
- Performance behavior: `tests/benchmarks` and `benchmarks`.
- Serialization and AOT: AOT, bytecode, and module-format tests.
