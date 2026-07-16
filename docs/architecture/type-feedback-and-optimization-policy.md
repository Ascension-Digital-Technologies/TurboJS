# Type Feedback and Optimization Policy

TurboJS records compact runtime evidence before admitting a function to a future optimizing tier. The feedback vector is deliberately independent of the machine-code backend and portable AOT format.

## Recorded evidence

Each function records:

- Execution count
- Argument type masks and transitions
- Return type mask and transitions
- Native bailout count
- Exception count

The initial integer-only tier classifies values as `int32` or `int64`. The public representation already reserves categories for booleans, doubles, heap references, and undefined values so the VM bridge can expand observations without replacing the format.

## Stability

A slot is stable when it has observations and exactly one observed type bit. A transition occurs when a new type expands an existing slot from one type to a mixed set.

## Eligibility

`TurboJS_EvaluateOptimization` returns an explicit decision:

- `eligible`
- `too-cold`
- `unstable-arguments`
- `unstable-result`
- `too-many-bailouts`
- `too-many-exceptions`
- `argument-limit`

This report is diagnostic input to the future optimizing compiler. It does not yet generate SSA or optimized native code.

## Integration

`TurboJSTieredFunction` owns its feedback vector. `TurboJS_TieredInvoke` records calls, successful results, native bailouts, and exceptions automatically, keeping policy data synchronized with real tiering behavior.
