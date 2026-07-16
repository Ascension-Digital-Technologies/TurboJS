# Boxed deoptimization bridge

TurboJS baseline code operates on unboxed integer values. The JavaScript VM,
however, operates on tagged `JSValue` values. Phase 16 introduces the boundary
object used to cross between those representations without exposing native
stack storage to the VM.

## Flow

1. Native code exits through a checked-operation bailout block.
2. `TurboJSDeoptFrame` records the instruction, bytecode offset, live masks,
   value kinds, registers, and locals.
3. `TurboJS_BoxDeoptFrame` materializes a separately owned typed snapshot.
4. A VM slow-path callback evaluates the operation with generic semantics.
5. `TurboJS_IRResumeWithSlowPath` installs the result in the failed
   instruction's destination and resumes at the next IR instruction.

The boxed snapshot currently supports undefined, int32, int64, and boolean
values. Heap references and doubles require GC rooting and are deliberately not
advertised yet.

## Lifetime

`TurboJSDeoptFrame` aliases storage owned by the compiled function and remains
valid only until the next invocation. `TurboJSBoxedDeoptFrame` owns its arrays
and remains valid until `TurboJS_BoxedDeoptFrameDestroy` is called.

## VM integration boundary

The callback API is intentionally independent of the large interpreter unit.
The VM can adapt `TurboJSBoxedValue` to `JSValue`, perform coercion or generic
arithmetic, and return a typed result. Direct interpreter program-counter
re-entry remains the next integration step.
