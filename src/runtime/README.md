# Runtime subsystem

Owns runtime/context state, atoms, strings, classes, and shared lifecycle services. Runtime-wide JIT caches, helper tables, and safepoint controllers integrate at this boundary.

## Rules

Document ownership and shutdown order for any object retained beyond a single call.

## Related documentation

Start with the [repository README](../../README.md) and `docs/architecture/` for detailed design notes.
