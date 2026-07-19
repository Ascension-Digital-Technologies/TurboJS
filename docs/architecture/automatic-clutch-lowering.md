# Automatic Clutch lowering

`TurboJS_EngineBytecodeToIR` can accept an optional call resolver. For supported call bytecodes, the frontend asks the resolver for a stable generation-checked Clutch target. A successful resolution emits a native call IR instruction and stores its call-site metadata under the IR function's ownership.

Call-site objects are individually allocated. This keeps pointers embedded in IR instructions stable even when the ownership table expands.

When no resolver is present, the target is unstable, or the call uses more than four arguments, lowering returns `TURBOJS_IR_UNSUPPORTED` so execution remains on the canonical dynamic JavaScript path.
