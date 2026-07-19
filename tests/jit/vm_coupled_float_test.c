#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "turbojs.h"
#include "optimization.h"

static int report_exception(JSContext *ctx, const char *name)
{
    JSValue exception = JS_GetException(ctx);
    const char *text = JS_ToCString(ctx, exception);
    fprintf(stderr, "%s%s%s\n", name, text ? ": " : "", text ? text : "");
    JS_FreeCString(ctx, text);
    JS_FreeValue(ctx, exception);
    return 1;
}

int main(void)
{
    static const char source[] =
        "(function(){"
        "function coupled(){let x=1.000001,y=0.25;"
        "for(let i=0;i<6000;i++){"
        "x=(x*1.0000003+y)/(1.0000001+(i&7)*1e-8);"
        "y=(y+x*1e-7)%3.0;}return +x;}"
        "const expected=2581.2892133309538;"
        "for(let r=0;r<16;r++){const got=coupled();"
        "if(Math.abs(got-expected)>1e-12)return false;}return true;})()";
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue result;
    TurboJSOptimizationConfig config;
    TurboJSRuntimeJITStats stats;
    int ok;
    if (!rt)
        return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return 1;
    }
    config = TurboJS_GetRuntimeOptimizationConfig(rt);
    config.enable_jit = 1;
    config.enable_optimizing_jit = 1;
    config.enable_osr = 1;
    config.osr_threshold = 8;
    TurboJS_SetRuntimeOptimizationConfig(rt, &config);
    result = JS_Eval(ctx, source, strlen(source), "coupled-float.js",
                     JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        report_exception(ctx, "coupled-float.js");
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }
    ok = JS_ToBool(ctx, result);
    JS_FreeValue(ctx, result);
    stats = TurboJS_GetRuntimeJITStats(rt);
    if (ok != 1 || stats.osr_coupled_float_entries < 8 ||
        stats.osr_coupled_float_iterations < 40000 || stats.osr_bailouts != 0) {
        fprintf(stderr,
                "coupled Float64 specialization inactive: ok=%d entries=%" PRIu64
                " iterations=%" PRIu64 " bailouts=%" PRIu64 "\n",
                ok, stats.osr_coupled_float_entries,
                stats.osr_coupled_float_iterations, stats.osr_bailouts);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    puts("Coupled Float64 OSR specialization passed");
    return 0;
}
