#include <stdint.h>
#include <stdio.h>
#include <string.h>
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

static int warm_binary(JSContext *ctx, JSValueConst fn)
{
    JSValue args[2] = { JS_NewInt32(ctx, 20), JS_NewInt32(ctx, 22) };
    int i, ok = 1;
    for (i = 0; i < 4; ++i) {
        JSValue v = JS_Call(ctx, fn, JS_UNDEFINED, 2, args);
        if (JS_IsException(v)) { JS_FreeValue(ctx, v); ok = 0; break; }
        JS_FreeValue(ctx, v);
    }
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    return ok ? 0 : report_exception(ctx, "warm-up failed");
}

static int call_i32(JSContext *ctx, JSValueConst fn, int32_t n, int32_t *out)
{
    JSValue arg = JS_NewInt32(ctx, n);
    JSValue v = JS_Call(ctx, fn, JS_UNDEFINED, 1, &arg);
    JS_FreeValue(ctx, arg);
    if (JS_IsException(v)) return report_exception(ctx, "runner failed");
    if (JS_ToInt32(ctx, out, v)) { JS_FreeValue(ctx, v); return report_exception(ctx, "conversion failed"); }
    JS_FreeValue(ctx, v);
    return 0;
}

int main(void)
{
    static const char less1_source[] = "(function(a,b){return a<b;})";
    static const char less2_source[] = "(function(a,b){return a<=b;})";
    static const char mono_source[] =
        "(function(n){var s=0,i;for(i=0;i<n;i++)s+=spoolLess(i&255,128);return s;})";
    static const char poly_source[] =
        "(function(n){var s=0,i,f;for(i=0;i<n;i++){f=(i&1)?spoolLess:spoolLess2;s+=f(i&255,128);}return s;})";
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue global, less1, less2, mono, poly;
    TurboJSRuntimeJITStats stats;
    int32_t result;
    if (!rt) return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) { JS_FreeRuntime(rt); return 1; }
    TurboJS_SetRuntimeJITThreshold(rt, 2);
    less1 = JS_Eval(ctx, less1_source, sizeof(less1_source)-1, "less1.js", JS_EVAL_TYPE_GLOBAL);
    less2 = JS_Eval(ctx, less2_source, sizeof(less2_source)-1, "less2.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(less1) || JS_IsException(less2)) return report_exception(ctx, "callee setup failed");
    global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "spoolLess", JS_DupValue(ctx, less1));
    JS_SetPropertyStr(ctx, global, "spoolLess2", JS_DupValue(ctx, less2));
    mono = JS_Eval(ctx, mono_source, sizeof(mono_source)-1, "mono.js", JS_EVAL_TYPE_GLOBAL);
    poly = JS_Eval(ctx, poly_source, sizeof(poly_source)-1, "poly.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(mono) || JS_IsException(poly)) return report_exception(ctx, "runner setup failed");
    if (warm_binary(ctx, less1) || warm_binary(ctx, less2)) return 1;
    if (call_i32(ctx, mono, 12000, &result) || call_i32(ctx, poly, 12000, &result)) return 1;
    stats = TurboJS_GetRuntimeJITStats(rt);
    printf("Telemetry Clutch: installs=%llu rejections=%llu hits=%llu mono=%llu poly=%llu\n",
           (unsigned long long)stats.relay_spool_feedback_installs,
           (unsigned long long)stats.relay_spool_feedback_rejections,
           (unsigned long long)stats.relay_spool_hits,
           (unsigned long long)stats.call_feedback_monomorphic,
           (unsigned long long)stats.call_feedback_polymorphic);
    /* The runtime can publish both observed polymorphic targets directly. A
       rejection is therefore no longer required for this workload; the
       durable contract is that both feedback shapes are observed and the
       installed entries receive substantial traffic. */
    if (stats.relay_spool_feedback_installs < 2 ||
        stats.relay_spool_hits < 5000 ||
        stats.call_feedback_monomorphic < 1 ||
        stats.call_feedback_polymorphic < 1) {
        fprintf(stderr, "Telemetry did not install and exercise Clutch entries as expected\n");
        return 1;
    }
    JS_FreeValue(ctx, less1); JS_FreeValue(ctx, less2);
    JS_FreeValue(ctx, mono); JS_FreeValue(ctx, poly); JS_FreeValue(ctx, global);
    JS_FreeContext(ctx); JS_FreeRuntime(rt);
    puts("Telemetry-guided Clutch integration passed");
    return 0;
}
