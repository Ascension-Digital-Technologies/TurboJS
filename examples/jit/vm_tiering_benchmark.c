#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include "turbojs.h"
#include "optimization.h"

static double elapsed_ms(clock_t begin, clock_t end)
{
    return (double)(end - begin) * 1000.0 / (double)CLOCKS_PER_SEC;
}

int main(void)
{
    static const char source[] = "(function add(a, b) { return a + b; })";
    const int iterations = 100000;
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue function;
    TurboJSRuntimeJITStats stats;
    clock_t begin, end;
    int i;

    if (!rt || !(ctx = JS_NewContext(rt)))
        return 1;
    TurboJS_SetRuntimeJITThreshold(rt, 10);
    function = JS_Eval(ctx, source, sizeof(source) - 1, "tiering-benchmark.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(function))
        return 1;

    begin = clock();
    for (i = 0; i < iterations; ++i) {
        JSValue args[2] = { JS_NewInt32(ctx, i & 1023), JS_NewInt32(ctx, 7) };
        JSValue result = JS_Call(ctx, function, JS_UNDEFINED, 2, args);
        if (JS_IsException(result))
            return 1;
        JS_FreeValue(ctx, result);
    }
    end = clock();
    stats = TurboJS_GetRuntimeJITStats(rt);

    printf("calls=%d elapsed=%.3f ms native=%llu interpreted=%llu compilations=%llu code=%zu bytes\n",
           iterations, elapsed_ms(begin, end),
           (unsigned long long)stats.native_calls,
           (unsigned long long)stats.interpreted_calls,
           (unsigned long long)stats.compilations,
           stats.native_code_bytes);

    JS_FreeValue(ctx, function);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return 0;
}
