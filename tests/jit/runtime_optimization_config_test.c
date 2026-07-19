#include <assert.h>
#include <stdio.h>
#include <turbojs.h>

int main(void)
{
    JSRuntime *rt = JS_NewRuntime();
    TurboJSOptimizationConfig config;
    TurboJSRuntimeJITStats stats;
    assert(rt != NULL);
    config = TurboJS_GetRuntimeOptimizationConfig(rt);
    assert(config.baseline_threshold > 0);
    assert(config.optimizing_threshold >= config.baseline_threshold);
    assert(config.osr_threshold > 0);
    config.baseline_threshold = 7;
    config.optimizing_threshold = 19;
    config.osr_threshold = 5;
    config.enable_jit = 0;
    config.enable_optimizing_jit = 0;
    config.enable_osr = 0;
    TurboJS_SetRuntimeOptimizationConfig(rt, &config);
    config = TurboJS_GetRuntimeOptimizationConfig(rt);
    assert(config.baseline_threshold == 7);
    assert(config.optimizing_threshold == 19);
    assert(config.osr_threshold == 5);
    assert(config.enable_jit == 0);
    assert(config.enable_optimizing_jit == 0);
    assert(config.enable_osr == 0);
    config.enable_jit = 0;
    config.enable_optimizing_jit = 1;
    config.enable_osr = 1;
    TurboJS_SetRuntimeOptimizationConfig(rt, &config);
    config = TurboJS_GetRuntimeOptimizationConfig(rt);
    assert(config.enable_jit == 0);
    assert(config.enable_optimizing_jit == 0);
    assert(config.enable_osr == 0);
    TurboJS_SetRuntimeJITThreshold(rt, 23);
    config = TurboJS_GetRuntimeOptimizationConfig(rt);
    assert(config.baseline_threshold == 23);
    assert(config.optimizing_threshold >= 23);
    assert(config.osr_threshold >= 23);
    TurboJS_ResetRuntimeJITStats(rt);
    stats = TurboJS_GetRuntimeJITStats(rt);
    assert(stats.interpreted_calls == 0);
    assert(stats.native_calls == 0);
    assert(stats.baseline_compile_requests == 0);
    assert(stats.optimizing_compile_requests == 0);
    assert(stats.osr_compile_requests == 0);
    JS_FreeRuntime(rt);
    puts("runtime optimization config: ok");
    return 0;
}
