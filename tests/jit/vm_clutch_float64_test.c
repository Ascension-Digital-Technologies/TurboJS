#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "turbojs.h"
#include "optimization.h"

static int report_exception(JSContext *ctx, const char *message)
{
    JSValue exception = JS_GetException(ctx);
    const char *text = JS_ToCString(ctx, exception);
    fprintf(stderr, "%s%s%s\n", message, text ? ": " : "", text ? text : "");
    JS_FreeCString(ctx, text);
    JS_FreeValue(ctx, exception);
    return 1;
}

static double expected_sum(int32_t count)
{
    return ((double)count * (double)(count - 1)) / 4.0 +
           0.75 * (double)count;
}

static int call_f64(JSContext *ctx, JSValueConst function,
                    int32_t argument, double *out)
{
    JSValue arg = JS_NewInt32(ctx, argument);
    JSValue result = JS_Call(ctx, function, JS_UNDEFINED, 1, &arg);
    JS_FreeValue(ctx, arg);
    if (JS_IsException(result))
        return report_exception(ctx, "Clutch Float64 runner failed");
    if (JS_ToFloat64(ctx, out, result)) {
        JS_FreeValue(ctx, result);
        return report_exception(ctx, "Clutch Float64 conversion failed");
    }
    JS_FreeValue(ctx, result);
    return 0;
}

int main(void)
{
    static const char target_source[] =
        "(function(a,b){return a/b;})";
    static const char runner_source[] =
        "(function(n){var s=0,i;for(i=0;i<n;i++)"
        "s+=spoolDivide(i+1.5,2.0);return s;})";
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue global, target, runner, args[2];
    TurboJSRuntimeJITStats before_clear, after_clear;
    double result = 0.0;
    int i;

    if (!rt)
        return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return 1;
    }
    TurboJS_SetRuntimeJITThreshold(rt, 2);
    target = JS_Eval(ctx, target_source, sizeof(target_source) - 1,
                     "clutch-float-target.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(target))
        return report_exception(ctx, "Float64 target setup failed");
    global = JS_GetGlobalObject(ctx);
    if (JS_SetPropertyStr(ctx, global, "spoolDivide",
                          JS_DupValue(ctx, target)) < 0)
        return report_exception(ctx, "Float64 target installation failed");
    runner = JS_Eval(ctx, runner_source, sizeof(runner_source) - 1,
                     "clutch-float-runner.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(runner))
        return report_exception(ctx, "Float64 runner setup failed");

    args[0] = JS_NewFloat64(ctx, 9.0);
    args[1] = JS_NewFloat64(ctx, 2.0);
    for (i = 0; i < 3; ++i) {
        JSValue value = JS_Call(ctx, target, JS_UNDEFINED, 2, args);
        double warm = 0.0;
        if (JS_IsException(value) || JS_ToFloat64(ctx, &warm, value) ||
            warm != 4.5) {
            JS_FreeValue(ctx, value);
            return report_exception(ctx, "Float64 target warm-up failed");
        }
        JS_FreeValue(ctx, value);
    }
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);

    if (call_f64(ctx, runner, 12000, &result) ||
        fabs(result - expected_sum(12000)) > 1e-9) {
        fprintf(stderr, "unexpected Float64 Clutch result: %.17g\n", result);
        return 1;
    }
    before_clear = TurboJS_GetRuntimeJITStats(rt);
    if (before_clear.relay_spool_hits < 11000 ||
        before_clear.relay_spool_installs < 1) {
        fprintf(stderr, "expected sustained Float64 Clutch hits\n");
        return 1;
    }

    TurboJS_ClearRuntimeJITCache(rt);
    if (call_f64(ctx, runner, 1200, &result) ||
        fabs(result - expected_sum(1200)) > 1e-9) {
        fprintf(stderr, "unexpected post-clear Float64 result: %.17g\n", result);
        return 1;
    }
    after_clear = TurboJS_GetRuntimeJITStats(rt);
    if (after_clear.relay_spool_hits <= before_clear.relay_spool_hits ||
        after_clear.relay_spool_installs < 2 ||
        after_clear.relay_spool_invalidations < 1) {
        fprintf(stderr, "expected Float64 Clutch invalidation and reinstall\n");
        return 1;
    }

    printf("Float64 Clutch: hits=%llu installs=%llu invalidations=%llu result=%.3f\n",
           (unsigned long long)after_clear.relay_spool_hits,
           (unsigned long long)after_clear.relay_spool_installs,
           (unsigned long long)after_clear.relay_spool_invalidations,
           result);
    JS_FreeValue(ctx, target);
    JS_FreeValue(ctx, runner);
    JS_FreeValue(ctx, global);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    puts("VM Clutch Float64 native entry integration passed");
    return 0;
}
