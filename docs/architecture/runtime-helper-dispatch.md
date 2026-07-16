# Runtime Helper Dispatch ABI

TurboJS baseline code represents dynamic VM operations with `runtime.helper` IR instructions. A helper instruction is a stable numeric ABI boundary rather than a process-specific C function address.

`TurboJSRuntimeHelperTable` binds helper IDs to runtime callbacks. `TurboJS_NativeInvokeWithRuntime` executes native code, handles runtime-call exits, boxes and roots the live deoptimization frame, dispatches the registered callback, and resumes immediately after the helper instruction.

This design keeps compiled code reusable across runtime instances and portable AOT images. Missing helpers fail with `TURBOJS_IR_UNSUPPORTED`; helper exceptions propagate as `TURBOJS_IR_EXCEPTION` and never resume the native continuation.

Call-site stack maps remain the source of truth for live and collector-managed values. Embedders can provide retain/release hooks when initializing the helper table so heap references remain rooted for the duration of the helper callback.
