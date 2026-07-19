#include <stdio.h>
#include "optimization.h"

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "optimization test failed: %s (%s:%d)\n",        \
                    #condition, __FILE__, __LINE__);                           \
            return 1;                                                          \
        }                                                                      \
    } while (0)

int main(void)
{
    TurboJSOptimizationConfig config = TurboJS_DefaultOptimizationConfig();
    TurboJSOptimizationCapabilities caps = TurboJS_GetOptimizationCapabilities();
    TurboJSHotFunction hot;
    TurboJSProfiler *profiler;
    unsigned i;

    CHECK(caps.profiling == 1);
    CHECK(caps.portable_bytecode_aot == 1);
    config.enable_jit = 1;
    config.enable_optimizing_jit = caps.optimizing_jit;
    config.baseline_threshold = 3;
    config.optimizing_threshold = 5;
    profiler = TurboJS_ProfilerCreate(&config);
    CHECK(profiler != NULL);

    for (i = 0; i < 2; ++i)
        CHECK(TurboJS_ProfilerRecordCall(profiler, 42) == TURBOJS_TIER_INTERPRETER);
    CHECK(TurboJS_ProfilerRecordCall(profiler, 42) == TURBOJS_TIER_BASELINE);
    TurboJS_ProfilerRecordCall(profiler, 42);
    CHECK(TurboJS_ProfilerRecordCall(profiler, 42) ==
          (caps.optimizing_jit ? TURBOJS_TIER_OPTIMIZING : TURBOJS_TIER_BASELINE));
    CHECK(TurboJS_ProfilerQuery(profiler, 42, &hot));
    CHECK(hot.call_count == 5);
    CHECK(hot.requested_tier ==
          (caps.optimizing_jit ? TURBOJS_TIER_OPTIMIZING : TURBOJS_TIER_BASELINE));
    CHECK(TurboJS_ProfilerFunctionCount(profiler) == 1);

    TurboJS_ProfilerDestroy(profiler);
    config.enable_optimizing_jit = 0;
    profiler = TurboJS_ProfilerCreate(&config);
    CHECK(profiler != NULL);
    for (i = 0; i < 8; ++i)
        CHECK(TurboJS_ProfilerRecordCall(profiler, 7) != TURBOJS_TIER_OPTIMIZING);

    TurboJS_ProfilerDestroy(profiler);
    puts("optimization policy tests passed");
    return 0;
}
