#include <stdio.h>
#include <string.h>
#include "turbojs.h"
#include "optimization.h"

int main(void)
{
    static const char source[] =
        "(function benchmark(repetitions){let checksum=0;"
        "for(let r=0;r<repetitions;r++){let sum=0;"
        "for(let i=0;i<1000000;i++)sum+=i;checksum+=sum;}return checksum;})";
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue fn, args[1], result;
    int64_t value = 0;
    TurboJSRuntimeJITStats stats;

    if (!rt)
        return 1;
    {
        TurboJSOptimizationConfig config = TurboJS_GetRuntimeOptimizationConfig(rt);
        config.baseline_threshold = 1;
        config.optimizing_threshold = 1;
        TurboJS_SetRuntimeOptimizationConfig(rt, &config);
    }
    ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return 1;
    }
    fn = JS_Eval(ctx, source, sizeof(source) - 1, "region-vm.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(fn)) {
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }
    args[0] = JS_NewInt32(ctx, 5);
    result = JS_Call(ctx, fn, JS_UNDEFINED, 1, args);
    if (JS_IsException(result) || JS_ToInt64(ctx, &value, result) ||
        value != 2499997500000LL) {
        fprintf(stderr, "region VM result mismatch: %lld\n", (long long)value);
        return 1;
    }
    stats = TurboJS_GetRuntimeJITStats(rt);
    if (stats.region_compilations < 1 || stats.region_native_calls < 1) {
        fprintf(stderr,
                "region VM tier inactive: comp=%llu calls=%llu failures=%llu\n",
                (unsigned long long)stats.region_compilations,
                (unsigned long long)stats.region_native_calls,
                (unsigned long long)stats.region_compile_failures);
        return 1;
    }
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, fn);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    puts("General region VM hot-tier integration passed");
    return 0;
}
