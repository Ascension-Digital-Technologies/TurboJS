# Clutch direct compiled-call edge

TurboJS provides `TURBOJS_IR_CALL_NATIVE_I64`, the first Gearbox instruction that transfers control directly from one Spool-compiled function to another.

## Safety sequence

Before the machine call, generated x64 code validates:

1. The call site still references a Vault entry handle.
2. The published generation matches the generation captured by Relay.
3. The native entry kind still matches the call site.
4. The handle still owns a live compiled function.

Only after these guards does Gearbox load the Spool code entry and issue an indirect machine `call`. Vault invalidation changes the generation before executable storage is released, so stale edges bail out rather than entering reclaimed code.

## ABI

The current native entry ABI receives:

- a contiguous argument vector,
- a destination slot,
- the callee-owned deoptimization buffer, and
- the callee-owned safepoint controller.

Gearbox preserves TurboJS runtime registers around the call and records a `TURBOJS_SAFEPOINT_CLUTCH_CALL` stack map. The first implementation supports two Int64/Int32-compatible arguments and an Int64-compatible result on x64.

## Current limitations

- Float64 compiled calls are not lowered yet.
- More than two arguments are not lowered yet.
- ARM64 does not yet emit the direct edge.
- Callee bailout metadata is propagated as a caller runtime-helper bailout; inlining and nested deoptimization reconstruction remain future work.
- The edge is exposed in JIT IR but is not yet selected automatically from ordinary JavaScript bytecode.
