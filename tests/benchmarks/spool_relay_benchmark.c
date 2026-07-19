#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "turbojs.h"
#include "optimization.h"
#include "internal/monotonic_clock.h"

#define REPETITIONS 7
#define ITERATIONS 1000000

static double monotonic_ms(void)
{
    return turbojs_monotonic_now_ms();
}

static int compare_double(const void *left, const void *right)
{
    const double a = *(const double *)left;
    const double b = *(const double *)right;
    return (a > b) - (a < b);
}

static int report_exception(JSContext *ctx, const char *message)
{
    JSValue exception = JS_GetException(ctx);
    const char *text = JS_ToCString(ctx, exception);
    fprintf(stderr, "%s%s%s\n", message, text ? ": " : "", text ? text : "");
    JS_FreeCString(ctx, text);
    JS_FreeValue(ctx, exception);
    return 1;
}

static int32_t expected_sum(int32_t count)
{
    int32_t i;
    int32_t sum = 0;
    for (i = 0; i < count; ++i)
        sum += (i & 255) < 128;
    return sum;
}

static int call_i32(JSContext *ctx, JSValueConst function,
                    int32_t argument, int32_t *out)
{
    JSValue arg = JS_NewInt32(ctx, argument);
    JSValue result = JS_Call(ctx, function, JS_UNDEFINED, 1, &arg);
    JS_FreeValue(ctx, arg);
    if (JS_IsException(result))
        return report_exception(ctx, "Spool Relay benchmark call failed");
    if (JS_ToInt32(ctx, out, result)) {
        JS_FreeValue(ctx, result);
        return report_exception(ctx, "Spool Relay benchmark conversion failed");
    }
    JS_FreeValue(ctx, result);
    return 0;
}

int main(void)
{
    static const char target_source[] =
        "(function(a,b){return a<b;})";
    static const char runner_source[] =
        "(function(n){var s=0,i;for(i=0;i<n;i++)"
        "s+=spoolLess(i&255,128);return s;})";
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue global, target, runner, args[2];
    TurboJSRuntimeJITStats stats;
    double samples[REPETITIONS];
    int32_t result = 0;
    int repetition;

    if (!rt)
        return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return 1;
    }
    TurboJS_SetRuntimeJITThreshold(rt, 2);
    target = JS_Eval(ctx, target_source, sizeof(target_source) - 1,
                     "spool-relay-target.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(target))
        return report_exception(ctx, "target setup failed");
    global = JS_GetGlobalObject(ctx);
    if (JS_SetPropertyStr(ctx, global, "spoolLess", JS_DupValue(ctx, target)) < 0)
        return report_exception(ctx, "target installation failed");
    runner = JS_Eval(ctx, runner_source, sizeof(runner_source) - 1,
                     "spool-relay-runner.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(runner))
        return report_exception(ctx, "runner setup failed");

    args[0] = JS_NewInt32(ctx, 1);
    args[1] = JS_NewInt32(ctx, 2);
    for (repetition = 0; repetition < 4; ++repetition) {
        JSValue value = JS_Call(ctx, target, JS_UNDEFINED, 2, args);
        if (JS_IsException(value) || JS_ToBool(ctx, value) != 1) {
            JS_FreeValue(ctx, value);
            return report_exception(ctx, "target warm-up failed");
        }
        JS_FreeValue(ctx, value);
    }
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);

    /* Prime the call site separately from the timed repetitions. */
    if (call_i32(ctx, runner, 2000, &result) || result != expected_sum(2000))
        return 1;

    for (repetition = 0; repetition < REPETITIONS; ++repetition) {
        const double start = monotonic_ms();
        if (call_i32(ctx, runner, ITERATIONS, &result) ||
            result != expected_sum(ITERATIONS))
            return 1;
        samples[repetition] = monotonic_ms() - start;
    }
    qsort(samples, REPETITIONS, sizeof(samples[0]), compare_double);
    stats = TurboJS_GetRuntimeJITStats(rt);
    printf("spool_relay_median_ms=%.3f iterations=%d repetitions=%d checksum=%d native_calls=%llu compilations=%llu\n",
           samples[REPETITIONS / 2], ITERATIONS, REPETITIONS, result,
           (unsigned long long)stats.native_calls,
           (unsigned long long)stats.compilations);

    JS_FreeValue(ctx, target);
    JS_FreeValue(ctx, runner);
    JS_FreeValue(ctx, global);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return 0;
}
