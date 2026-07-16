# Test suite

Focused unit and differential tests for the interpreter, baseline JIT, optimizing JIT, deoptimization, GC cooperation, runtime helpers, code caches, and AOT formats.

## Rules

Avoid side effects inside `assert`; Release builds may disable assertions. Prefer explicit checks and useful failure messages.

## Related documentation

Start with the [repository README](../README.md) and `docs/architecture/` for detailed design notes.
