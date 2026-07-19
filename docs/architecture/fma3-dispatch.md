# FMA3 transform dispatch

TurboJS detects AVX2 and FMA3 separately. Fused multiply-add is not enabled for ordinary JavaScript arithmetic because contraction can change IEEE-754 rounding relative to separate multiply and add operations.

A typed transform may opt in with `TURBOJS_ELEMENT_FLAG_FAST_MATH`. On the first eligible compilation, Gearbox performs a small lazy calibration between AVX2 multiply-plus-add and AVX2/FMA3. The faster legal kernel is cached for subsequent transforms. This avoids assuming that FMA3 is faster on every microarchitecture.

The scalar and AVX2 paths remain available on hosts without FMA3 or when strict arithmetic is required.
