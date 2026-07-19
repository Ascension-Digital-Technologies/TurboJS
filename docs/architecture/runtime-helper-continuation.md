# Runtime-helper continuation

Spool may compile a native prefix and exit at `TURBOJS_IR_RUNTIME_HELPER` when an operation still requires canonical JavaScript semantics.

`TurboJS_NativeInvokeWithRuntime` invokes the native function first. A runtime-helper bailout captures the materialized register and local state, boxes rooted values for the callback, executes the registered helper, writes the helper result back into the destination register, and resumes the verified IR continuation. The continuation loop may cross multiple helper exits before returning.

The helper table does not own VM values. Embedders provide rooting hooks whenever boxed frames may contain heap references. Missing helpers, exceptions, execution limits, stale Clutch calls, and unsupported continuation opcodes terminate continuation with an explicit status.
