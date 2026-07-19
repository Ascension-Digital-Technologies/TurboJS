#ifndef TURBOJS_INTERNAL_MONOTONIC_CLOCK_H
#define TURBOJS_INTERNAL_MONOTONIC_CLOCK_H

#include <stdint.h>
#include <time.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

/*
 * Cross-platform monotonic clock used by JIT calibration and benchmarks.
 * Returns zero only when the platform timing API fails.
 */
static inline uint64_t turbojs_monotonic_now_ns(void)
{
#if defined(_WIN32)
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    uint64_t seconds;
    uint64_t remainder;

    if (frequency.QuadPart <= 0) {
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
            return 0;
    }
    if (!QueryPerformanceCounter(&counter) || counter.QuadPart < 0)
        return 0;

    /* Split the conversion so counter * 1e9 cannot overflow. */
    seconds = (uint64_t)(counter.QuadPart / frequency.QuadPart);
    remainder = (uint64_t)(counter.QuadPart % frequency.QuadPart);
    return seconds * UINT64_C(1000000000) +
           (remainder * UINT64_C(1000000000)) / (uint64_t)frequency.QuadPart;
#elif defined(CLOCK_MONOTONIC)
    struct timespec timestamp;
    if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0)
        return 0;
    return (uint64_t)timestamp.tv_sec * UINT64_C(1000000000) +
           (uint64_t)timestamp.tv_nsec;
#elif defined(TIME_UTC)
    struct timespec timestamp;
    if (timespec_get(&timestamp, TIME_UTC) != TIME_UTC)
        return 0;
    return (uint64_t)timestamp.tv_sec * UINT64_C(1000000000) +
           (uint64_t)timestamp.tv_nsec;
#else
    return (uint64_t)clock() * UINT64_C(1000000000) /
           (uint64_t)CLOCKS_PER_SEC;
#endif
}

static inline double turbojs_monotonic_now_ms(void)
{
    return (double)turbojs_monotonic_now_ns() / 1000000.0;
}

#endif /* TURBOJS_INTERNAL_MONOTONIC_CLOCK_H */
