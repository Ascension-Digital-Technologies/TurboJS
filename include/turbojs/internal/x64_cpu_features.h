#ifndef TURBOJS_INTERNAL_X64_CPU_FEATURES_H
#define TURBOJS_INTERNAL_X64_CPU_FEATURES_H

/*
 * Small, dependency-free x64 CPU feature probe shared by the VM and JIT.
 *
 * Clang targeting the Microsoft runtime must not use
 * __builtin_cpu_supports(): that builtin references compiler-rt/GCC globals
 * (__cpu_model and __cpu_indicator_init) which are not part of a normal
 * Windows MSVC-runtime link.  Windows therefore uses CPUID/XGETBV directly.
 */

typedef struct TurboJSX64CPUFeatures {
    int avx2;
    int fma3;
} TurboJSX64CPUFeatures;

#if (defined(__x86_64__) || defined(_M_X64)) && defined(_WIN32)
#include <intrin.h>
#endif

static inline TurboJSX64CPUFeatures turbojs_x64_cpu_features(void)
{
    TurboJSX64CPUFeatures features = { 0, 0 };

#if (defined(__x86_64__) || defined(_M_X64)) && defined(_WIN32)
    int cpu_info[4] = { 0, 0, 0, 0 };
    int maximum_leaf;
    int has_osxsave;
    int has_avx;
    int has_fma;
    unsigned __int64 xcr0;

    __cpuid(cpu_info, 0);
    maximum_leaf = cpu_info[0];
    if (maximum_leaf < 1)
        return features;

    __cpuidex(cpu_info, 1, 0);
    has_osxsave = (cpu_info[2] & (1 << 27)) != 0;
    has_avx = (cpu_info[2] & (1 << 28)) != 0;
    has_fma = (cpu_info[2] & (1 << 12)) != 0;
    if (!has_osxsave || !has_avx)
        return features;

    xcr0 = _xgetbv(0);
    if ((xcr0 & 0x6u) != 0x6u)
        return features;

    if (maximum_leaf >= 7) {
        __cpuidex(cpu_info, 7, 0);
        features.avx2 = (cpu_info[1] & (1 << 5)) != 0;
        features.fma3 = features.avx2 && has_fma;
    }
#elif defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    __builtin_cpu_init();
    features.avx2 = __builtin_cpu_supports("avx2") != 0;
    features.fma3 = features.avx2 && (__builtin_cpu_supports("fma") != 0);
#endif

    return features;
}

#endif /* TURBOJS_INTERNAL_X64_CPU_FEATURES_H */
