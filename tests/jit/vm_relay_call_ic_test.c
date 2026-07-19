#include <stdio.h>
#include <stdint.h>
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

int main(void)
{
    static const char source[] =
        "(function(){"
        " function leaf(x){return (x*3+7)|0;}"
        " function other(x){return (x^91)|0;}"
        " function mono(){var s=0,i;for(i=0;i<20000;i++)s=(s+leaf(i))|0;return s;}"
        " function poly(){var s=0,i,f;for(i=0;i<200;i++){f=(i&1)?leaf:other;s=(s+f(i))|0;}return s;}"
        " function imulLeaf(x){return Math.imul(x,5)|0;}"
        " var before=0,after=0,j,oldImul=Math.imul;"
        " for(j=0;j<20000;j++)before=(before+imulLeaf(j))|0;"
        " Math.imul=function(){return 7;};"
        " for(j=0;j<1000;j++)after=(after+imulLeaf(j))|0;"
        " Math.imul=oldImul;"
        " return [mono(),poly(),before,after];"
        "})()";
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue result, mono_value, poly_value, imul_before_value, imul_after_value;
    TurboJSRuntimeJITStats stats;
    int32_t mono = 0, poly = 0, imul_before = 0, imul_after = 0;

    if (!rt)
        return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return 1;
    }
    result = JS_Eval(ctx, source, sizeof(source) - 1,
                     "relay-call-ic.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        int rc = report_exception(ctx, "Relay call IC evaluation failed");
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return rc;
    }
    mono_value = JS_GetPropertyUint32(ctx, result, 0);
    poly_value = JS_GetPropertyUint32(ctx, result, 1);
    imul_before_value = JS_GetPropertyUint32(ctx, result, 2);
    imul_after_value = JS_GetPropertyUint32(ctx, result, 3);
    if (JS_IsException(mono_value) || JS_IsException(poly_value) ||
        JS_IsException(imul_before_value) || JS_IsException(imul_after_value) ||
        JS_ToInt32(ctx, &mono, mono_value) || JS_ToInt32(ctx, &poly, poly_value) ||
        JS_ToInt32(ctx, &imul_before, imul_before_value) ||
        JS_ToInt32(ctx, &imul_after, imul_after_value)) {
        JS_FreeValue(ctx, mono_value);
        JS_FreeValue(ctx, poly_value);
        JS_FreeValue(ctx, imul_before_value);
        JS_FreeValue(ctx, imul_after_value);
        JS_FreeValue(ctx, result);
        report_exception(ctx, "Relay result conversion failed");
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }
    JS_FreeValue(ctx, mono_value);
    JS_FreeValue(ctx, poly_value);
    JS_FreeValue(ctx, imul_before_value);
    JS_FreeValue(ctx, imul_after_value);
    JS_FreeValue(ctx, result);

    stats = TurboJS_GetRuntimeJITStats(rt);
    printf("Relay calls: hits=%llu misses=%llu installs=%llu invalidations=%llu mono=%d poly=%d\n",
           (unsigned long long)stats.relay_call_hits,
           (unsigned long long)stats.relay_call_misses,
           (unsigned long long)stats.relay_call_installs,
           (unsigned long long)stats.relay_call_invalidations,
           mono, poly);
    if (mono != 600110000 || poly != 42588 ||
        imul_before != 999950000 || imul_after != 7000 ||
        stats.relay_call_hits < 39000 || stats.relay_call_installs < 4) {
        fprintf(stderr, "expected sustained two-target Relay hits and guarded Math.imul fallback\n");
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    puts("VM Relay call IC integration passed");
    return 0;
}
