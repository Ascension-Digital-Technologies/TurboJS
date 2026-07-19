# SIMD Float64 kernels

TurboJS Gearbox may select a runtime-dispatched x64 SIMD kernel for proven typed
Float64 reduction and transform loops. CPU capabilities are detected once. AVX2
hosts use four-lane vectors in eight-element batches; other hosts retain the
existing scalar SSE2 generated loop.

The SIMD path preserves the same entry guards as the scalar loop: typed element
kind, storage generation, current length, and requested limit. Transform kernels
allow exact in-place operation or non-overlapping source/destination ranges.
Partial overlap bails out before any write.

Telemetry is exposed through `TurboJSRegionNativeStats`:

- `inline_element_simd_loops`
- `inline_element_simd_width`
- `inline_element_simd_level`

The current x64 levels are scalar, SSE2, and AVX2. ARM64 NEON dispatch remains a
separate backend milestone.
