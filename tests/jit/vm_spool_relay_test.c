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
        return report_exception(ctx, "Spool Relay call failed");
    if (JS_ToInt32(ctx, out, result)) {
        JS_FreeValue(ctx, result);
        return report_exception(ctx, "Spool Relay result conversion failed");
    }
    JS_FreeValue(ctx, result);
    return 0;
}

int main(void)
{
    static const char add_source[] =
        "(function(a,b){return a<b;})";
    static const char run_source[] =
        "(function(n){var s=0,i;for(i=0;i<n;i++)"
        "s+=spoolLess(i&255,128);return s;})";
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue global, add, run;
    JSValue add_args[2];
    TurboJSRuntimeJITStats before_clear, after_clear;
    int32_t result = 0;
    int i;

    if (!rt)
        return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return 1;
    }
    TurboJS_SetRuntimeJITThreshold(rt, 2);
    add = JS_Eval(ctx, add_source, sizeof(add_source) - 1,
                  "spool-add.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(add)) {
        int rc = report_exception(ctx, "Spool function setup failed");
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return rc;
    }
    global = JS_GetGlobalObject(ctx);
    if (JS_SetPropertyStr(ctx, global, "spoolLess", JS_DupValue(ctx, add)) < 0) {
        report_exception(ctx, "Spool global installation failed");
        JS_FreeValue(ctx, add);
        JS_FreeValue(ctx, global);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }
    run = JS_Eval(ctx, run_source, sizeof(run_source) - 1,
                  "spool-run.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(run)) {
        int rc = report_exception(ctx, "Spool runner setup failed");
        JS_FreeValue(ctx, add);
        JS_FreeValue(ctx, global);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return rc;
    }

    add_args[0] = JS_NewInt32(ctx, 20);
    add_args[1] = JS_NewInt32(ctx, 22);
    for (i = 0; i < 3; ++i) {
        JSValue value = JS_Call(ctx, add, JS_UNDEFINED, 2, add_args);
        int warm = JS_ToBool(ctx, value);
        if (JS_IsException(value) || warm != 1) {
            JS_FreeValue(ctx, value);
            report_exception(ctx, "Spool warm-up failed");
            JS_FreeValue(ctx, add_args[0]);
            JS_FreeValue(ctx, add_args[1]);
            JS_FreeValue(ctx, add);
            JS_FreeValue(ctx, run);
            JS_FreeValue(ctx, global);
            JS_FreeContext(ctx);
            JS_FreeRuntime(rt);
            return 1;
        }
        JS_FreeValue(ctx, value);
    }
    JS_FreeValue(ctx, add_args[0]);
    JS_FreeValue(ctx, add_args[1]);

    if (call_i32(ctx, run, 20000, &result) || result != expected_sum(20000)) {
        fprintf(stderr, "unexpected first Spool Relay result: %d\n", result);
        JS_FreeValue(ctx, add);
        JS_FreeValue(ctx, run);
        JS_FreeValue(ctx, global);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }
    before_clear = TurboJS_GetRuntimeJITStats(rt);
    printf("Before clear: native=%llu interpreted=%llu guards=%llu relay=%llu spool_hits=%llu spool_misses=%llu spool_installs=%llu spool_invalidations=%llu cache_entries=%zu compilations=%llu\n",
           (unsigned long long)before_clear.native_calls,
           (unsigned long long)before_clear.interpreted_calls,
           (unsigned long long)before_clear.guard_failures,
           (unsigned long long)before_clear.relay_call_hits,
           (unsigned long long)before_clear.relay_spool_hits,
           (unsigned long long)before_clear.relay_spool_misses,
           (unsigned long long)before_clear.relay_spool_installs,
           (unsigned long long)before_clear.relay_spool_invalidations,
           before_clear.cache_entries,
           (unsigned long long)before_clear.compilations);
    if (before_clear.relay_spool_hits < 19000 ||
        before_clear.relay_spool_installs < 1) {
        fprintf(stderr, "expected sustained Spool Relay hits\n");
        JS_FreeValue(ctx, add);
        JS_FreeValue(ctx, run);
        JS_FreeValue(ctx, global);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }

    TurboJS_ClearRuntimeJITCache(rt);
    if (call_i32(ctx, run, 2000, &result) || result != expected_sum(2000)) {
        fprintf(stderr, "unexpected post-invalidation result: %d\n", result);
        JS_FreeValue(ctx, add);
        JS_FreeValue(ctx, run);
        JS_FreeValue(ctx, global);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }
    after_clear = TurboJS_GetRuntimeJITStats(rt);
    printf("Spool Relay: hits=%llu misses=%llu installs=%llu invalidations=%llu result=%d\n",
           (unsigned long long)after_clear.relay_spool_hits,
           (unsigned long long)after_clear.relay_spool_misses,
           (unsigned long long)after_clear.relay_spool_installs,
           (unsigned long long)after_clear.relay_spool_invalidations,
           result);
    if (after_clear.relay_spool_hits <= before_clear.relay_spool_hits ||
        after_clear.relay_spool_installs < 2 ||
        after_clear.relay_spool_invalidations < 1) {
        fprintf(stderr, "expected safe Vault invalidation and Spool re-installation\n");
        JS_FreeValue(ctx, add);
        JS_FreeValue(ctx, run);
        JS_FreeValue(ctx, global);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }

    JS_FreeValue(ctx, add);
    JS_FreeValue(ctx, run);
    JS_FreeValue(ctx, global);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    puts("VM Spool Relay native entry integration passed");
    return 0;
}
