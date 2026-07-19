# SIMD Transform Fusion

TurboJS recognizes three canonical typed Float64 transform loops in Redline/Gearbox:

- affine: `dst[i] = src[i] * scale + bias`
- scale-only: `dst[i] = src[i] * scale`
- bias-only: `dst[i] = src[i] + bias`

All forms retain the same one-time element-kind, storage-generation, length, backing-store, and overlap guards. Scale-only and bias-only loops normalize the missing operand to the neutral value (`bias = 0` or `scale = 1`) and do not require an unused JavaScript/native argument.

FMA contraction remains legal only when both element operations carry `TURBOJS_ELEMENT_FLAG_FAST_MATH`. Runtime calibration may still prefer separate AVX2 multiply/add even when FMA3 is available.

Partial source/destination overlap bails out. Distinct buffers and exact in-place transforms are supported.
