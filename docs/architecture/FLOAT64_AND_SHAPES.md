# Float64 execution and object shapes

TurboJS now carries IEEE-754 values through the compact IR as exact 64-bit payloads. The x86-64 baseline backend lowers `constant.f64`, `add.f64`, `sub.f64`, `mul.f64`, `div.f64`, and `return.f64` through SSE2 scalar instructions. The public `TurboJS_IRExecuteF64` and `TurboJS_NativeInvokeF64` adapters preserve NaN payloads, infinities, and negative zero because values cross the generic ABI as raw bits.

The object-shape subsystem provides immutable canonical shapes. Adding the same property to the same parent shape returns the same child shape. Each property has a stable slot offset, allowing a monomorphic inline cache to replace repeated name lookup with a shape-id comparison and direct offset reuse.

This pass deliberately keeps shapes independent from the inherited object implementation. The next integration step attaches a shape pointer to ordinary objects and routes property bytecodes through these caches. That separation keeps compatibility changes reviewable and lets the cache contract be stress-tested before changing observable object semantics.
