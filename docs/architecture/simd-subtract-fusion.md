# SIMD subtract-only fusion

The typed Float64 transform recognizer supports the typed Float64 transform recognizer with the canonical form:

```js
destination[i] = source[i] - bias;
```

The frontend records transform kind `subtract-only`. The runtime SIMD dispatcher
normalizes the operation to the established affine kernel with `scale = 1.0` and
`bias = -bias`. This reuses AVX2/SSE2 dispatch, overlap checks, generation guards,
length validation, and scalar remainder handling without creating a duplicate
machine-kernel family.

The optimization is legal under ordinary JavaScript Float64 semantics because
subtraction by `b` and addition of `-b` use the same IEEE-754 operation after the
unary sign change. FMA contraction remains disabled unless fast-math is explicitly
permitted.
