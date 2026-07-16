# JIT and AOT subsystem

Contains the baseline IR, bytecode frontend, x86-64 backend, executable memory, code caches, deoptimization, stack maps, runtime helper dispatch, type feedback, SSA optimizer, and portable AOT formats.

## Rules

Every new operation needs verifier coverage, interpreter semantics, native lowering or a defined fallback, differential tests, and deoptimization/GC metadata when values can survive a safepoint.

## Related documentation

Start with the [repository README](../../README.md) and `docs/architecture/` for detailed design notes.
