# Checked Arithmetic and Native Bailouts

TurboJS baseline-compiled JavaScript integer arithmetic uses explicit checked IR operations:

- `add.i32.checked`
- `sub.i32.checked`
- `mul.i32.checked`

The x86-64 backend performs the operation in 64 bits, sign-extends the low 32-bit result, and compares it with the full result. A mismatch branches to a shared bailout epilogue. Native entry points return a status code and write successful values through an output pointer, so every signed 64-bit value remains representable without sentinel collisions.

`TurboJS_NativeInvoke` returns `TURBOJS_IR_BAILOUT` for overflow. The VM bridge then executes the original bytecode through the interpreter, preserving JavaScript number semantics. `TurboJS_NativeLastBailout` exposes the bailout reason for diagnostics and future deoptimization work.

This phase uses whole-call fallback. Future phases can attach bytecode positions and live-value maps for mid-function interpreter re-entry.
