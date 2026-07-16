# JIT stack maps and safepoints

TurboJS baseline native functions now publish immutable stack maps for locations where the runtime may need to inspect compiled state. Maps are emitted for checked-operation bailout sites, loop backedges, and returns.

Each `TurboJSStackMap` records the IR instruction, native code offset, source bytecode offset, live register/local masks, and the subset known to contain collector-managed references. Embedders can enumerate maps through `TurboJS_NativeStackMapCount` and `TurboJS_NativeStackMapAt`.

`TurboJS_TraceDeoptFrame` visits live, materialized heap references in a captured deoptimization frame. It deliberately ignores dead, unavailable, and primitive values. This gives the collector a stable tracing hook while future phases add runtime polling and moving-GC relocation support.

The current baseline IR does not yet create heap-reference-producing instructions, so reference masks are normally empty unless the VM annotates kinds during integration. The format and tracing contract are complete and tested.
