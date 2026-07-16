# Checked Division and Deoptimization Metadata

TurboJS baseline code may specialize JavaScript numeric operations to signed int32 arithmetic only when the VM has already guarded the operands. Division and remainder require additional checks because native `idiv` can trap while JavaScript must remain recoverable.

## Supported IR operations

- `div.i32.checked`
- `rem.i32.checked`

Both operations reject a zero divisor through a structured JIT bailout. Division also rejects `INT32_MIN / -1`, whose mathematical result cannot be represented by the int32 specialization. Remainder handles `INT32_MIN % -1` as zero without executing the trapping native instruction.

## Bailout contract

A native bailout records:

- The bailout reason
- The exact IR instruction index
- The originating engine bytecode offset

The current runtime performs whole-call interpreter fallback. The bytecode offset is retained now so a later deoptimization frame can resume at the exact VM instruction after reconstructing live values.

## Portable AOT format

TJIR format version 2 stores the bytecode offset for every instruction. Round-tripping a function through AOT serialization therefore preserves deoptimization source positions.

## Safety properties

Generated division code explicitly guards:

1. Divisor equals zero
2. Signed division overflow
3. The special remainder overflow pair

No native divide fault is allowed to escape into the host process.
