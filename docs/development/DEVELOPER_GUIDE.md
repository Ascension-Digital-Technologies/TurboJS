# Developer Guide

This guide describes the normal engineering loop for TurboJS.

## Configure

Use a CMake preset for consistent options. `full-release` is intended for optimized validation; development presets should retain assertions and tests. Meson support exists for downstream and alternate build workflows.

## Build in layers

Start with the smallest affected target. Build the engine and focused test, then the CLI/compiler, then the full test suite. Regenerate checked-in outputs only when their source or generator changes.

## Validate in layers

1. Format and repository validation.
2. Focused unit test.
3. Interpreter-only semantic test.
4. JIT differential and deoptimization tests when native execution is affected.
5. Embedding lifecycle tests when public or memory behavior changes.
6. Test262 subset, then broader compatibility run.
7. Sanitizer or stress configuration for ownership-sensitive changes.
8. Benchmarks only after correctness is established.

## Patch design

Keep semantic changes separate from mechanical layout changes when possible. State the invariant being changed, the fallback behavior, and the tests proving both optimized and generic paths. Avoid broad source rewrites that obscure review of performance-sensitive code.

## Generated files

Do not manually edit `generated/turbojs_engine_unit.c`, generated built-in data, Unicode tables, or generated runtime artifacts. Run the owning generator and review both generator and output diffs.

## Performance work

Record a baseline before changing code. Preserve identical JavaScript input and correctness checks. Report median or another declared statistic, raw run counts, machine/compiler configuration, startup inclusion, and code-size/memory effects. A faster wrong result is a regression.
