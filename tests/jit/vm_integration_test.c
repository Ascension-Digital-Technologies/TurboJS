#include <stdio.h>
#include <stdint.h>
#include "turbojs.h"
#include "optimization.h"

static int fail(JSContext *ctx, const char *message)
{
    JSValue exception = JS_GetException(ctx);
    const char *text = JS_ToCString(ctx, exception);
    fprintf(stderr, "%s%s%s\n", message, text ? ": " : "", text ? text : "");
    JS_FreeCString(ctx, text);
    JS_FreeValue(ctx, exception);
    return 1;
}

int main(void)
{
    static const char source[] = "(function add(a, b) { return a + b; })";
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue function;
    TurboJSRuntimeJITStats stats;
    int i;

    if (!rt) {
        fprintf(stderr, "unable to create runtime\n");
        return 1;
    }
    ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return 1;
    }
    TurboJS_SetRuntimeJITThreshold(rt, 3);
    function = JS_Eval(ctx, source, sizeof(source) - 1, "jit-test.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(function)) {
        int rc = fail(ctx, "evaluation failed");
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return rc;
    }

    for (i = 0; i < 1000; ++i) {
        JSValue args[2] = { JS_NewInt32(ctx, i), JS_NewInt32(ctx, 7) };
        JSValue result = JS_Call(ctx, function, JS_UNDEFINED, 2, args);
        int32_t value = 0;
        if (JS_IsException(result)) {
            JS_FreeValue(ctx, function);
            fail(ctx, "call failed");
            JS_FreeContext(ctx);
            JS_FreeRuntime(rt);
            return 1;
        }
        if (JS_ToInt32(ctx, &value, result) || value != i + 7) {
            fprintf(stderr, "incorrect result at %d: %d\n", i, value);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, function);
            JS_FreeContext(ctx);
            JS_FreeRuntime(rt);
            return 1;
        }
        JS_FreeValue(ctx, result);
    }

    /* Float64 arguments now use a separately cached numeric specialization. */
    {
        JSValue args[2] = { JS_NewFloat64(ctx, 1.5), JS_NewInt32(ctx, 2) };
        JSValue result = JS_Call(ctx, function, JS_UNDEFINED, 2, args);
        double value = 0;
        if (JS_IsException(result) || JS_ToFloat64(ctx, &value, result) || value != 3.5) {
            fprintf(stderr, "guard fallback produced an incorrect result\n");
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, function);
            JS_FreeContext(ctx);
            JS_FreeRuntime(rt);
            return 1;
        }
        JS_FreeValue(ctx, result);
    }

    stats = TurboJS_GetRuntimeJITStats(rt);
    printf("vm integration: native=%llu interpreted=%llu guards=%llu compilations=%llu bytes=%zu\n",
           (unsigned long long)stats.native_calls,
           (unsigned long long)stats.interpreted_calls,
           (unsigned long long)stats.guard_failures,
           (unsigned long long)stats.compilations,
           stats.native_code_bytes);
    if (stats.compilations < 1 || stats.native_calls < 1) {
        fprintf(stderr, "expected tiering activity was not observed\n");
        JS_FreeValue(ctx, function);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }

    JS_FreeValue(ctx, function);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    puts("JavaScript source to baseline JIT integration passed");
    return 0;
}
