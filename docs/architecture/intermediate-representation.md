# TurboJS Intermediate Representation

TurboJS IR is a compact register-based representation placed between engine
bytecode and execution backends. It provides one verified contract for the
reference interpreter, baseline JIT, future optimizing JIT, and AOT emitters.

## Current instruction set

- `argument`: load a signed 64-bit function argument.
- `constant.i64`: materialize a signed 64-bit constant.
- `add.i64`, `sub.i64`, `mul.i64`: integer arithmetic.
- `less-than.i64`: produce `0` or `1`.
- `jump`, `branch-true`: explicit control flow.
- `return.i64`: finish execution with a signed 64-bit result.

The verifier checks opcodes, register references, argument indexes, jump
locations, the register limit, and the presence of a return instruction.
Execution also uses a step budget to prevent malformed cyclic IR from hanging
the host.

## Baseline compilation contract

`TurboJS_BaselineCompile` either returns a callable native function or a precise
status. `TURBOJS_IR_UNSUPPORTED` is not a fatal error; it means the function
must continue through the reference interpreter. The first x86-64 backend uses
six physical registers and therefore rejects larger functions until spilling is
implemented.

## Security properties

Native pages are never writable and executable simultaneously. TurboJS emits
into read/write pages, seals them read/execute, flushes the instruction cache,
and releases them through the platform-specific memory API.
