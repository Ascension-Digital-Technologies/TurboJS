#include "simd_kernels.h"
#include "internal/monotonic_clock.h"
#include "internal/x64_cpu_features.h"
#include <stdint.h>
#include <time.h>


#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

static double sum_scalar(const double *v, size_t n)
{
    double a = 0.0, b = 0.0;
    size_t i = 0;
    for (; i + 1 < n; i += 2) { a += v[i]; b += v[i + 1]; }
    if (i < n) a += v[i];
    return a + b;
}

static void transform_scalar(const double *s, double *d, size_t n,
                             double scale, double bias)
{
    size_t i;
    for (i = 0; i < n; ++i) d[i] = s[i] * scale + bias;
}


static void bound_scalar(const double *source, double *destination, size_t count,
                         double lower, double upper, int mode)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        double x = source[i];
        if (mode == 0) destination[i] = x > upper ? upper : x;
        else if (mode == 1) destination[i] = x < lower ? lower : x;
        else { if (x < lower) x = lower; if (x > upper) x = upper; destination[i] = x; }
    }
}

static void binary_scalar(const double *left, const double *right, double *destination,
                          size_t count, int subtract)
{
    size_t i;
    if (subtract) for (i = 0; i < count; ++i) destination[i] = left[i] - right[i];
    else for (i = 0; i < count; ++i) destination[i] = left[i] + right[i];
}

#if (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
__attribute__((target("avx2")))
static double sum_avx2(const double *v, size_t n)
{
    __m256d a = _mm256_setzero_pd();
    __m256d b = _mm256_setzero_pd();
    double lane[4];
    size_t i = 0;
    for (; i + 7 < n; i += 8) {
        a = _mm256_add_pd(a, _mm256_loadu_pd(v + i));
        b = _mm256_add_pd(b, _mm256_loadu_pd(v + i + 4));
    }
    a = _mm256_add_pd(a, b);
    _mm256_storeu_pd(lane, a);
    {
        double total = lane[0] + lane[1] + lane[2] + lane[3];
        for (; i < n; ++i) total += v[i];
        return total;
    }
}

__attribute__((target("avx2")))
static void transform_avx2(const double *s, double *d, size_t n,
                           double scale, double bias)
{
    const __m256d vs = _mm256_set1_pd(scale);
    const __m256d vb = _mm256_set1_pd(bias);
    size_t i = 0;
    for (; i + 7 < n; i += 8) {
        __m256d x0 = _mm256_loadu_pd(s + i);
        __m256d x1 = _mm256_loadu_pd(s + i + 4);
        x0 = _mm256_add_pd(_mm256_mul_pd(x0, vs), vb);
        x1 = _mm256_add_pd(_mm256_mul_pd(x1, vs), vb);
        _mm256_storeu_pd(d + i, x0);
        _mm256_storeu_pd(d + i + 4, x1);
    }
    for (; i < n; ++i) d[i] = s[i] * scale + bias;
}


__attribute__((target("avx2")))
static void binary_avx2(const double *left, const double *right, double *destination,
                        size_t count, int subtract)
{
    size_t i = 0;
    for (; i + 7 < count; i += 8) {
        __m256d l0 = _mm256_loadu_pd(left + i);
        __m256d l1 = _mm256_loadu_pd(left + i + 4);
        __m256d r0 = _mm256_loadu_pd(right + i);
        __m256d r1 = _mm256_loadu_pd(right + i + 4);
        __m256d o0 = subtract ? _mm256_sub_pd(l0, r0) : _mm256_add_pd(l0, r0);
        __m256d o1 = subtract ? _mm256_sub_pd(l1, r1) : _mm256_add_pd(l1, r1);
        _mm256_storeu_pd(destination + i, o0);
        _mm256_storeu_pd(destination + i + 4, o1);
    }
    for (; i < count; ++i) destination[i] = subtract ? left[i] - right[i] : left[i] + right[i];
}


__attribute__((target("avx2")))
static void bound_avx2(const double *source, double *destination, size_t count,
                       double lower, double upper, int mode)
{
    const __m256d lo = _mm256_set1_pd(lower), hi = _mm256_set1_pd(upper);
    size_t i = 0;
    for (; i + 7 < count; i += 8) {
        __m256d x0 = _mm256_loadu_pd(source + i);
        __m256d x1 = _mm256_loadu_pd(source + i + 4);
        if (mode == 0) {
            __m256d m0 = _mm256_cmp_pd(x0, hi, _CMP_GT_OQ), m1 = _mm256_cmp_pd(x1, hi, _CMP_GT_OQ);
            x0 = _mm256_blendv_pd(x0, hi, m0); x1 = _mm256_blendv_pd(x1, hi, m1);
        } else if (mode == 1) {
            __m256d m0 = _mm256_cmp_pd(x0, lo, _CMP_LT_OQ), m1 = _mm256_cmp_pd(x1, lo, _CMP_LT_OQ);
            x0 = _mm256_blendv_pd(x0, lo, m0); x1 = _mm256_blendv_pd(x1, lo, m1);
        } else {
            __m256d ml0 = _mm256_cmp_pd(x0, lo, _CMP_LT_OQ), ml1 = _mm256_cmp_pd(x1, lo, _CMP_LT_OQ);
            x0 = _mm256_blendv_pd(x0, lo, ml0); x1 = _mm256_blendv_pd(x1, lo, ml1);
            { __m256d mh0 = _mm256_cmp_pd(x0, hi, _CMP_GT_OQ), mh1 = _mm256_cmp_pd(x1, hi, _CMP_GT_OQ);
              x0 = _mm256_blendv_pd(x0, hi, mh0); x1 = _mm256_blendv_pd(x1, hi, mh1); }
        }
        _mm256_storeu_pd(destination + i, x0); _mm256_storeu_pd(destination + i + 4, x1);
    }
    bound_scalar(source + i, destination + i, count - i, lower, upper, mode);
}

__attribute__((target("avx2,fma")))
static void transform_fma3(const double *s, double *d, size_t n,
                           double scale, double bias)
{
    const __m256d vs = _mm256_set1_pd(scale);
    const __m256d vb = _mm256_set1_pd(bias);
    size_t i = 0;
    for (; i + 7 < n; i += 8) {
        __m256d x0 = _mm256_loadu_pd(s + i);
        __m256d x1 = _mm256_loadu_pd(s + i + 4);
        x0 = _mm256_fmadd_pd(x0, vs, vb);
        x1 = _mm256_fmadd_pd(x1, vs, vb);
        _mm256_storeu_pd(d + i, x0);
        _mm256_storeu_pd(d + i + 4, x1);
    }
    for (; i < n; ++i) d[i] = s[i] * scale + bias;
}
#endif

TurboJSX64SIMDLevel turbojs_x64_simd_level(void)
{
#if defined(__x86_64__) || defined(_M_X64)
    static int initialized = 0;
    static TurboJSX64SIMDLevel level = TURBOJS_X64_SIMD_SSE2;
    if (!initialized) {
        TurboJSX64CPUFeatures features = turbojs_x64_cpu_features();
        if (features.avx2)
            level = features.fma3 ? TURBOJS_X64_SIMD_FMA3 : TURBOJS_X64_SIMD_AVX2;
        initialized = 1;
    }
    return level;
#else
    return TURBOJS_X64_SIMD_SCALAR;
#endif
}

static uint64_t simd_now_ns(void)
{
    return turbojs_monotonic_now_ns();
}

TurboJSX64SIMDLevel turbojs_x64_f64_transform_level(int allow_fma)
{
#if (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
    static int calibrated = 0;
    static TurboJSX64SIMDLevel selected = TURBOJS_X64_SIMD_AVX2;
    TurboJSX64SIMDLevel available = turbojs_x64_simd_level();
    if (!allow_fma || available < TURBOJS_X64_SIMD_FMA3)
        return available >= TURBOJS_X64_SIMD_AVX2 ? TURBOJS_X64_SIMD_AVX2 : available;
    if (!calibrated) {
        static double source[1024], destination[1024];
        volatile double sink = 0.0;
        uint64_t avx_begin, avx_end, fma_begin, fma_end;
        size_t i, r;
        for (i = 0; i < 1024; ++i) source[i] = (double)(i % 97) * 0.25;
        avx_begin = simd_now_ns();
        for (r = 0; r < 256; ++r) {
            transform_avx2(source, destination, 1024, 1.75, 0.5);
            sink += destination[r & 1023];
        }
        avx_end = simd_now_ns();
        fma_begin = simd_now_ns();
        for (r = 0; r < 256; ++r) {
            transform_fma3(source, destination, 1024, 1.75, 0.5);
            sink += destination[r & 1023];
        }
        fma_end = simd_now_ns();
        (void)sink;
        if (avx_begin && avx_end > avx_begin && fma_begin && fma_end > fma_begin &&
            (fma_end - fma_begin) < (avx_end - avx_begin))
            selected = TURBOJS_X64_SIMD_FMA3;
        calibrated = 1;
    }
    return selected;
#else
    (void)allow_fma;
    return turbojs_x64_simd_level();
#endif
}

double turbojs_x64_f64_sum(const double *values, size_t count,
                           TurboJSX64SIMDLevel level)
{
#if (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
    if (level >= TURBOJS_X64_SIMD_AVX2) return sum_avx2(values, count);
#else
    (void)level;
#endif
    return sum_scalar(values, count);
}

void turbojs_x64_f64_transform(const double *source, double *destination,
                               size_t count, double scale, double bias,
                               TurboJSX64SIMDLevel level)
{
#if (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
    if (level >= TURBOJS_X64_SIMD_FMA3) {
        transform_fma3(source, destination, count, scale, bias);
        return;
    }
    if (level >= TURBOJS_X64_SIMD_AVX2) {
        transform_avx2(source, destination, count, scale, bias);
        return;
    }
#else
    (void)level;
#endif
    transform_scalar(source, destination, count, scale, bias);
}

void turbojs_x64_f64_binary(const double *left, const double *right,
                            double *destination, size_t count, int subtract,
                            TurboJSX64SIMDLevel level)
{
#if (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
    if (level >= TURBOJS_X64_SIMD_AVX2) {
        binary_avx2(left, right, destination, count, subtract);
        return;
    }
#else
    (void)level;
#endif
    binary_scalar(left, right, destination, count, subtract);
}

void turbojs_x64_f64_bound(const double *source, double *destination,
                           size_t count, double lower, double upper, int mode,
                           TurboJSX64SIMDLevel level)
{
#if (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
    if (level >= TURBOJS_X64_SIMD_AVX2) { bound_avx2(source, destination, count, lower, upper, mode); return; }
#else
    (void)level;
#endif
    bound_scalar(source, destination, count, lower, upper, mode);
}
