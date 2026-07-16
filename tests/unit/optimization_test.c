#include <assert.h>
#include <stdio.h>
#include "optimization.h"

int main(void)
{
    TurboJSOptimizationConfig config = TurboJS_DefaultOptimizationConfig();
    TurboJSOptimizationCapabilities caps = TurboJS_GetOptimizationCapabilities();
    TurboJSHotFunction hot;
    TurboJSProfiler *profiler;
    unsigned i;

    assert(caps.profiling == 1);
    assert(caps.portable_bytecode_aot == 1);
    config.enable_jit = 1;
    config.baseline_threshold = 3;
    config.optimizing_threshold = 5;
    profiler = TurboJS_ProfilerCreate(&config);
    assert(profiler != NULL);

    for (i = 0; i < 2; ++i)
        assert(TurboJS_ProfilerRecordCall(profiler, 42) == TURBOJS_TIER_INTERPRETER);
    assert(TurboJS_ProfilerRecordCall(profiler, 42) == TURBOJS_TIER_BASELINE);
    TurboJS_ProfilerRecordCall(profiler, 42);
    assert(TurboJS_ProfilerRecordCall(profiler, 42) == TURBOJS_TIER_OPTIMIZING);
    assert(TurboJS_ProfilerQuery(profiler, 42, &hot));
    assert(hot.call_count == 5);
    assert(hot.requested_tier == TURBOJS_TIER_OPTIMIZING);
    assert(TurboJS_ProfilerFunctionCount(profiler) == 1);

    TurboJS_ProfilerDestroy(profiler);
    puts("optimization policy tests passed");
    return 0;
}
