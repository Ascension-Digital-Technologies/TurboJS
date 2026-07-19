# Typed Float64 loops

Gearbox provides two x64 Gearbox kernels for `TURBOJS_ELEMENT_KIND_TYPED_F64`.

## Accumulation

The sum kernel validates the receiver, element kind, storage generation, requested
limit, and array length once. It hoists the backing pointer and processes two
values per iteration using two independent SSE2 accumulators, followed by a scalar
tail.

## Transform and store

The transform kernel recognizes `dst[i] = src[i] * scale + bias`. It validates
both arrays, checks both lengths against the limit, hoists both backing pointers,
loads scale and bias once, and processes two values per iteration. Source and
destination may be distinct or identical because each element is loaded before
its corresponding store.

## Bailouts

Both kernels return `TURBOJS_IR_BAILOUT` before storage access when argument count,
object tagging, element kind, storage generation, limit range, or bounds checks
fail.

## Current boundary

The kernels use scalar SSE2 instructions. SIMD vectorization, runtime AVX2 feature
selection, explicit overlap policy, and ARM64 NEON parity remain future work.
