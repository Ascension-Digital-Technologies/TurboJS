# SIMD Bounds Fusion

Redline and Gearbox support typed Float64 min, max, and clamp loop recognition to Redline/Gearbox.
Canonical SSA forms use `TURBOJS_SSA_MIN_F64` and `TURBOJS_SSA_MAX_F64`; a clamp is represented as `min(max(value, lower), upper)`.

The x64 runtime kernel processes eight values per AVX2 batch and retains a scalar tail. Ordered comparisons plus blends preserve source NaNs and signed zero. All object-kind, generation, length, storage, overlap, and bound-order checks complete before destination writes begin.

The backend currently dispatches these kernels through the proven typed-element region path. Direct discovery from ordinary engine `get_array_el` / `put_array_el` bytecodes and native ARM64 NEON execution remain follow-on work.
