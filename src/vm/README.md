# Virtual machine

Owns bytecode interpretation, calls, coercion, generator/async execution, and the bridge into tiered execution. The interpreter is the semantic reference for compiled tiers.

## Rules

Native fast paths must return here for unsupported or dynamic behavior rather than duplicating full JavaScript semantics in backend code.

## Related documentation

Start with the [repository README](../../README.md) and `docs/architecture/` for detailed design notes.
