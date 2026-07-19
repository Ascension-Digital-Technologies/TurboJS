# TurboJS engine source

`src/` contains handwritten production implementation only. Public SDK headers live in `include/turbojs/`; generated compilation units live in `generated/`; applications, tests, and repository tooling live outside the engine tree.

## Ownership map

| Directory | Responsibility |
| --- | --- |
| `api/` | Implementations of the stable embedding facade and optional libc integration |
| `builtins/` | JavaScript built-ins and their authored JavaScript sources |
| `compiler/` | Lexer, parser, and bytecode emission |
| `core/` | Runtime/context lifecycle, atoms, allocation, configuration, and value primitives |
| `gc/` | Collection, exception state, prototypes, and property access machinery |
| `internal/` | Private cross-subsystem contracts and engine data model |
| `jit/` | Spool, Redline, Slipstream, Rewind, Vault, Relay, and Forge pipeline implementation |
| `modules/` | Module records, linking, evaluation, and dynamic compilation |
| `numeric/`, `regexp/`, `unicode/` | Focused low-level support libraries |
| `objects/` | Objects, strings, classes, shapes, and lifetime rules |
| `serialization/` | Bytecode reader and writer |
| `vm/` | Interpreter, calls, jobs, generators, feedback, and execution state |

## Source rules

- New code belongs to the subsystem that owns its lifetime and invariants.
- Do not introduce catch-all `common`, `misc`, `support`, or `utils` directories.
- Public declarations belong in `include/turbojs/`; private declarations belong in `src/internal/` or the owning subsystem.
- Generated files must never be authored in `src/`.
- A subsystem may depend only on predecessors declared in `cmake/TurboJSSubsystems.json`.

Run `python tools/validation/check_architecture.py` after changing subsystem ownership or the generated engine unit.
