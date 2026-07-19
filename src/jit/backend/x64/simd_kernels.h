#ifndef TURBOJS_JIT_BACKEND_X64_SIMD_KERNELS_H
#define TURBOJS_JIT_BACKEND_X64_SIMD_KERNELS_H

#include <stddef.h>

typedef enum TurboJSX64SIMDLevel {
    TURBOJS_X64_SIMD_SCALAR = 0,
    TURBOJS_X64_SIMD_SSE2 = 1,
    TURBOJS_X64_SIMD_AVX2 = 2,
    TURBOJS_X64_SIMD_FMA3 = 3
} TurboJSX64SIMDLevel;

TurboJSX64SIMDLevel turbojs_x64_simd_level(void);
TurboJSX64SIMDLevel turbojs_x64_f64_transform_level(int allow_fma);
double turbojs_x64_f64_sum(const double *values, size_t count,
                           TurboJSX64SIMDLevel level);
void turbojs_x64_f64_transform(const double *source, double *destination,
                               size_t count, double scale, double bias,
                               TurboJSX64SIMDLevel level);
void turbojs_x64_f64_binary(const double *left, const double *right,
                            double *destination, size_t count, int subtract,
                            TurboJSX64SIMDLevel level);
void turbojs_x64_f64_bound(const double *source, double *destination,
                           size_t count, double lower, double upper, int mode,
                           TurboJSX64SIMDLevel level);

#endif
