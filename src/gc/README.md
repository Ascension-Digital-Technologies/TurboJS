# Garbage collection

Owns allocation, tracing, property storage helpers, and collection. JIT code cooperates through stack maps, safepoints, rooting hooks, and relocation callbacks.

## Rules

A native value that may reference managed memory must be visible to tracing or rooted before any allocation or runtime call.

## Related documentation

Start with the [repository README](../../README.md) and `docs/architecture/` for detailed design notes.
