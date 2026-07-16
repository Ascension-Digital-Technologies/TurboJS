# VM JIT Integration

TurboJS now has a guarded baseline-JIT entry in the bytecode function call path.

## Ownership

- `JSRuntime` owns one bounded native-code cache.
- `JSFunctionBytecode` stores call count and compilation-attempt metadata.
- Cache entries are keyed by the bytecode object and invalidated when that object is freed.
- Runtime shutdown destroys executable memory before GC releases bytecode objects.

## Guarded fast path

The first VM-integrated tier accepts normal bytecode functions with integer arguments and no captured variable references. The engine translates supported stack bytecode into verified IR and compiles only a side-effect-free integer subset. Unsupported opcodes, constructors, generators, closures, non-integer arguments, and results outside the int32 representation return to the canonical interpreter.

The VM-safe subset currently includes arguments, integer constants, addition, subtraction, integer comparison, and return. Multiplication remains available in the standalone IR JIT but is intentionally excluded from VM tiering until checked-overflow or deoptimization support exists.

## Public controls

`TurboJS_SetRuntimeJITThreshold` changes the call threshold. `TurboJS_GetRuntimeJITStats` reports interpreter calls, native calls, guard failures, cache activity, compilations, evictions, entry count, and native-code bytes. `TurboJS_ClearRuntimeJITCache` releases compiled entries while preserving the runtime.

## Correctness strategy

Native entry is optional. Any failed guard or unsupported translation continues through the existing interpreter. The integration test executes JavaScript source repeatedly, verifies results, checks tier promotion, and then sends a floating-point argument to prove semantic fallback.
