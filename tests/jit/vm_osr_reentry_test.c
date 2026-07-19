#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "turbojs.h"
#include "optimization.h"

static int32_t expected_wrapped_sum(int32_t n)
{
    uint64_t value = ((uint64_t)(uint32_t)n * (uint64_t)(uint32_t)(n - 1)) / 2u;
    return (int32_t)(uint32_t)value;
}

int main(void)
{
    static const char source[] =
        "(function f(n){let s=0;for(let i=0;i<n;i++)s=(s+i)|0;return s;})";
    static const int32_t inputs[] = {
        1000000, 1100000, 900000, 1234567,
        777777, 2000000, 1, 1000
    };
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue function;
    TurboJSOptimizationConfig config;
    TurboJSRuntimeJITStats stats;
    size_t i;

    if (!rt)
        return 1;
    config = TurboJS_GetRuntimeOptimizationConfig(rt);
    config.baseline_threshold = 1;
    config.optimizing_threshold = 1;
    config.osr_threshold = 1;
    config.enable_jit = 1;
    config.enable_optimizing_jit = 1;
    config.enable_osr = 1;
    TurboJS_SetRuntimeOptimizationConfig(rt, &config);

    ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return 1;
    }
    function = JS_Eval(ctx, source, sizeof(source) - 1,
                       "osr-reentry.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(function)) {
        fprintf(stderr, "failed to create OSR re-entry function\n");
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }

    TurboJS_ResetRuntimeJITStats(rt);
    for (i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        JSValue argument = JS_NewInt32(ctx, inputs[i]);
        JSValue result = JS_Call(ctx, function, JS_UNDEFINED, 1, &argument);
        int32_t actual = 0;
        int32_t expected = expected_wrapped_sum(inputs[i]);
        JS_FreeValue(ctx, argument);
        if (JS_IsException(result) || JS_ToInt32(ctx, &actual, result) || actual != expected) {
            fprintf(stderr,
                    "OSR re-entry mismatch at call %zu: n=%d got=%d expected=%d\n",
                    i, inputs[i], actual, expected);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, function);
            JS_FreeContext(ctx);
            JS_FreeRuntime(rt);
            return 1;
        }
        JS_FreeValue(ctx, result);
    }

    stats = TurboJS_GetRuntimeJITStats(rt);
    if (stats.osr_entries < sizeof(inputs) / sizeof(inputs[0]) ||
        stats.osr_bailouts != 0 || stats.osr_compilations < 1) {
        fprintf(stderr,
                "OSR re-entry coverage incomplete: compilations=%llu entries=%llu bailouts=%llu\n",
                (unsigned long long)stats.osr_compilations,
                (unsigned long long)stats.osr_entries,
                (unsigned long long)stats.osr_bailouts);
        JS_FreeValue(ctx, function);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }

    JS_FreeValue(ctx, function);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    puts("OSR repeated entry with changing arguments passed");
    return 0;
}
