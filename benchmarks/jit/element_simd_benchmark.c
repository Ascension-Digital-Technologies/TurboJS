#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../src/jit/backend/x64/simd_kernels.h"
#include "internal/aligned_memory.h"
#include "internal/monotonic_clock.h"

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

static uint64_t now_ns(void)
{
    return turbojs_monotonic_now_ns();
}

TURBOJS_NOINLINE TURBOJS_GCC_NO_VECTORIZE
static double scalar_sum(const double *values, size_t count)
{
    double sum_a = 0.0;
    double sum_b = 0.0;
    size_t index = 0;

#if defined(__clang__)
#pragma clang loop vectorize(disable)
#pragma clang loop interleave(disable)
#endif
    for (; index + 1 < count; index += 2) {
        sum_a += values[index];
        sum_b += values[index + 1];
    }
    if (index < count)
        sum_a += values[index];
    return sum_a + sum_b;
}

TURBOJS_NOINLINE TURBOJS_GCC_NO_VECTORIZE
static void scalar_transform(const double *source, double *destination,
                             size_t count, double scale, double bias)
{
    size_t index;

#if defined(__clang__)
#pragma clang loop vectorize(disable)
#pragma clang loop interleave(disable)
#endif
    for (index = 0; index < count; ++index)
        destination[index] = source[index] * scale + bias;
}

int main(void)
{
    const size_t count = 1024;
    const size_t repetitions = 200000;
    double *source = turbojs_aligned_alloc(32, count * sizeof(*source));
    double *destination = turbojs_aligned_alloc(32, count * sizeof(*destination));
    TurboJSX64SIMDLevel level;
    uint64_t start;
    uint64_t end;
    double checksum = 0.0;
    double scalar_sum_ns;
    double simd_sum_ns;
    double scalar_transform_ns;
    double simd_transform_ns;
    double avx2_transform_ns;
    double fma_transform_ns = 0.0;
    size_t repetition;

    if (!source || !destination) {
        turbojs_aligned_free(source);
        turbojs_aligned_free(destination);
        return 2;
    }

    for (repetition = 0; repetition < count; ++repetition)
        source[repetition] = (double)(repetition % 97) * 0.25;

    level = turbojs_x64_simd_level();

    start = now_ns();
    for (repetition = 0; repetition < repetitions; ++repetition)
        checksum += scalar_sum(source, count - (repetition & 7));
    end = now_ns();
    scalar_sum_ns = (double)(end - start) / repetitions;

    start = now_ns();
    for (repetition = 0; repetition < repetitions; ++repetition)
        checksum += turbojs_x64_f64_sum(source, count - (repetition & 7), level);
    end = now_ns();
    simd_sum_ns = (double)(end - start) / repetitions;

    start = now_ns();
    for (repetition = 0; repetition < repetitions; ++repetition) {
        scalar_transform(source, destination, count, 1.75, 0.5);
        checksum += destination[repetition & (count - 1)];
    }
    end = now_ns();
    scalar_transform_ns = (double)(end - start) / repetitions;

    start = now_ns();
    for (repetition = 0; repetition < repetitions; ++repetition) {
        turbojs_x64_f64_transform(source, destination, count, 1.75, 0.5, level);
        checksum += destination[repetition & (count - 1)];
    }
    end = now_ns();
    simd_transform_ns = (double)(end - start) / repetitions;
    avx2_transform_ns = simd_transform_ns;

    if (level >= TURBOJS_X64_SIMD_FMA3) {
        start = now_ns();
        for (repetition = 0; repetition < repetitions; ++repetition) {
            turbojs_x64_f64_transform(source, destination, count, 1.75, 0.5,
                                      TURBOJS_X64_SIMD_AVX2);
            checksum += destination[repetition & (count - 1)];
        }
        end = now_ns();
        avx2_transform_ns = (double)(end - start) / repetitions;

        start = now_ns();
        for (repetition = 0; repetition < repetitions; ++repetition) {
            turbojs_x64_f64_transform(source, destination, count, 1.75, 0.5,
                                      TURBOJS_X64_SIMD_FMA3);
            checksum += destination[repetition & (count - 1)];
        }
        end = now_ns();
        fma_transform_ns = (double)(end - start) / repetitions;
    }

    printf("simd_level=%d\n", (int)level);
    printf("scalar_sum_ns=%.3f\n", scalar_sum_ns);
    printf("simd_sum_ns=%.3f\n", simd_sum_ns);
    printf("sum_speedup=%.3f\n", scalar_sum_ns / simd_sum_ns);
    printf("scalar_transform_ns=%.3f\n", scalar_transform_ns);
    printf("simd_transform_ns=%.3f\n", simd_transform_ns);
    printf("transform_speedup=%.3f\n", scalar_transform_ns / simd_transform_ns);
    printf("avx2_transform_ns=%.3f\n", avx2_transform_ns);
    if (fma_transform_ns > 0.0) {
        printf("fma3_transform_ns=%.3f\n", fma_transform_ns);
        printf("fma3_vs_avx2=%.3f\n", avx2_transform_ns / fma_transform_ns);
    }
    printf("checksum=%.6f\n", checksum);

    turbojs_aligned_free(source);
    turbojs_aligned_free(destination);
    return !isfinite(checksum);
}
