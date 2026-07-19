# Spool tagged baseline foundation

Spool provides the portable semantic foundation for broad baseline-native
JavaScript execution.

## Tagged value subset

The compact IR now defines baseline operations for full tagged values:

- `VALUE_ARGUMENT`
- `VALUE_UNDEFINED`
- `VALUE_CONSTANT_I32`
- `VALUE_MOVE`
- `VALUE_LOCAL_GET`
- `VALUE_LOCAL_SET`
- `VALUE_TO_BOOLEAN`
- `VALUE_RETURN`

`TurboJS_IRExecuteTagged()` is the semantic oracle for these operations. It
preserves integer, floating-point, Boolean, undefined, and heap-reference
values using `TurboJSBoxedValue`. Gearbox lowering will be added after the
frontend can form complete tagged regions.

## Dynamic frame state

`TurboJSSpoolFrameState` mirrors Pulse locals and operand-stack slots without
the previous fixed 64-slot frontend storage. It supports dynamic growth,
cloning, local access, push/pop, and shape compatibility checks at CFG joins.

The next compiler pass will use these states at every bytecode boundary and
merge non-empty operand stacks across branches.
