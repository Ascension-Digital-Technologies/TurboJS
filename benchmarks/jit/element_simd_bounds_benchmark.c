#include <stdint.h>
#include <stdio.h>

#include "simd_kernels.h"
#include "internal/monotonic_clock.h"

#define N 1024
#define R 200000

#if defined(_MSC_VER)
#define TURBOJS_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define TURBOJS_NOINLINE __attribute__((noinline))
#else
#define TURBOJS_NOINLINE
#endif

#if defined(__GNUC__) && !defined(__clang__)
#define TURBOJS_GCC_NO_VECTORIZE __attribute__((optimize("no-tree-vectorize")))
#else
#define TURBOJS_GCC_NO_VECTORIZE
#endif

static uint64_t ns(void)
{
    return turbojs_monotonic_now_ns();
}

TURBOJS_NOINLINE TURBOJS_GCC_NO_VECTORIZE
static void scalar(const double *source, double *destination, size_t count,
                   double lower, double upper)
{
    size_t index;

#if defined(__clang__)
#pragma clang loop vectorize(disable)
#pragma clang loop interleave(disable)
#endif
    for (index = 0; index < count; ++index) {
        double value = source[index];
        if (value < lower)
            value = lower;
        if (value > upper)
            value = upper;
        destination[index] = value;
    }
}

int main(void)
{
    static double source[N];
    static double destination[N];
    volatile double sink = 0.0;
    TurboJSX64SIMDLevel level = turbojs_x64_simd_level();
    uint64_t scalar_start;
    uint64_t simd_start;
    uint64_t end;
    size_t index;
    size_t repetition;

    for (index = 0; index < N; ++index)
        source[index] = (double)((int)(index % 257) - 128) * 0.25;

    scalar_start = ns();
    for (repetition = 0; repetition < R; ++repetition) {
        scalar(source, destination, N, -10.0, 12.0);
        sink += destination[repetition & (N - 1)];
    }

    simd_start = ns();
    for (repetition = 0; repetition < R; ++repetition) {
        turbojs_x64_f64_bound(source, destination, N, -10.0, 12.0, 2, level);
        sink += destination[repetition & (N - 1)];
    }
    end = ns();

    printf("scalar_clamp_ns_per_loop=%.3f\n", (double)(simd_start - scalar_start) / R);
    printf("simd_clamp_ns_per_loop=%.3f\n", (double)(end - simd_start) / R);
    printf("speedup=%.2fx\n", (double)(simd_start - scalar_start) /
                                 (double)(end - simd_start));
    printf("simd_level=%d sink=%f\n", (int)level, (double)sink);
    return 0;
}
