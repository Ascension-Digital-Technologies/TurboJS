# Runtime safepoints and moving-GC relocation

TurboJS baseline native functions accept a `TurboJSSafepointController` on entry. The x86-64 backend emits a lightweight poll before every backward jump or backward conditional branch. A clear controller adds one memory comparison to the loop backedge. A requested controller exits through the normal deoptimization snapshot path with `TURBOJS_BAILOUT_SAFEPOINT_REQUESTED`.

The bailout records the exact IR instruction and original bytecode offset and materializes virtual registers and locals. The runtime may trace the frame with `TurboJS_TraceDeoptFrame`, relocate live heap references with `TurboJS_RelocateDeoptFrame`, perform collection or interruption work, clear the controller, and resume through the interpreter or invoke native code again.

## Controller lifecycle

```c
TurboJSSafepointController controller;
TurboJS_SafepointControllerInit(&controller);
TurboJS_NativeSetSafepointController(native, &controller);

TurboJS_SafepointRequest(&controller);
/* Native loop exits with TURBOJS_IR_BAILOUT. */
TurboJS_SafepointClear(&controller);
```

Each native function owns a default controller. A runtime-wide controller can be attached to multiple compiled functions so a single request stops them cooperatively at loop backedges.

## Relocation

`TurboJS_RelocateDeoptFrame` visits only values that are materialized, live, and marked `TURBOJS_VALUE_HEAP_REFERENCE`. The callback returns the relocated address, which is written back into the captured frame. Primitive and dead values are ignored.

The current controller is a cooperative polling contract. The embedding runtime is responsible for coordinating request/clear operations with its threading model. Native-call safepoints and direct collector integration remain future work.
