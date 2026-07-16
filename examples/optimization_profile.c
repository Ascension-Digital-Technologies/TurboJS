#include <stdio.h>
#include "optimization.h"

int main(void)
{
    TurboJSOptimizationConfig config = TurboJS_DefaultOptimizationConfig();
    TurboJSProfiler *profiler;
    unsigned i;
    config.enable_jit = 1;
    config.baseline_threshold = 10;
    config.optimizing_threshold = 100;
    profiler = TurboJS_ProfilerCreate(&config);
    if (!profiler)
        return 1;
    for (i = 1; i <= 100; ++i) {
        TurboJSOptimizationTier tier = TurboJS_ProfilerRecordCall(profiler, 7);
        if (i == 1 || i == 10 || i == 100)
            printf("call=%u requested-tier=%s\n", i, TurboJS_OptimizationTierName(tier));
    }
    TurboJS_ProfilerDestroy(profiler);
    return 0;
}
