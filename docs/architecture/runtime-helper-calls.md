# Runtime Helper Calls and Exception Propagation

TurboJS baseline native code uses an explicit runtime-call exit for operations that require the full JavaScript runtime. The IR opcode `runtime.helper` identifies a helper id and two input registers plus a destination register.

## Execution contract

1. Native code reaches the helper instruction.
2. It records the IR instruction and bytecode offset.
3. It snapshots materialized registers and locals.
4. It returns `TURBOJS_IR_BAILOUT` with reason `TURBOJS_BAILOUT_RUNTIME_HELPER`.
5. The VM invokes a `TurboJSSlowPathCallback`.
6. A successful boxed result is installed into the helper destination.
7. IR execution resumes at the instruction following the helper call.

A callback may return `TURBOJS_IR_EXCEPTION`. That status propagates unchanged and continuation does not run.

## Safepoints

Every runtime helper instruction publishes a `TURBOJS_SAFEPOINT_RUNTIME_CALL` stack map containing its native offset, IR index, bytecode offset, liveness masks, and reference masks. This makes runtime calls suitable collection and interruption boundaries.

## Current scope

The baseline backend currently exits to the VM rather than invoking arbitrary C helpers directly from generated code. This preserves a stable ABI and correct GC/exception behavior while runtime helper signatures are still evolving. A later direct-call lowering can reuse the same stack-map and continuation contracts.
