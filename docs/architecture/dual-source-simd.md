# Dual-source Float64 SIMD

TurboJS recognizes canonical typed Float64 loops of the forms `dst[i] = left[i] + right[i]` and `dst[i] = left[i] - right[i]`. Gearbox validates both sources and the destination once, checks all lengths and generations, rejects unsafe partial overlap, and dispatches to an eight-element AVX2 loop with a scalar tail. Exact in-place output with either source is permitted.
