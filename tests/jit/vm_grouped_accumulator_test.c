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
        "function grouped(){const users=[];"
        "for(let i=0;i<1200;i++)users.push({group:i%17,score:(i*13)%1000});"
        "const totals={};for(let r=0;r<8;r++){for(let i=0;i<users.length;i++){"
        "const u=users[i];u.score=(u.score+u.group+r)%1000;"
        "totals[u.group]=(totals[u.group]||0)+u.score;}}"
        "let sum=0;for(const k in totals)sum=(sum+totals[k])|0;return sum;}"
        "const a=grouped(),b=grouped();return a===b&&a===4742740;})()";
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue result;
    TurboJSOptimizationConfig config;
    TurboJSRuntimeJITStats stats;
    int ok;
    if (!rt) return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) { JS_FreeRuntime(rt); return 1; }
    config = TurboJS_GetRuntimeOptimizationConfig(rt);
    config.enable_jit = 1;
    config.enable_optimizing_jit = 1;
    config.enable_osr = 1;
    config.osr_threshold = 8;
    TurboJS_SetRuntimeOptimizationConfig(rt, &config);
    result = JS_Eval(ctx, source, strlen(source), "grouped-accumulator.js",
                     JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        report_exception(ctx, "grouped-accumulator.js");
        JS_FreeContext(ctx); JS_FreeRuntime(rt); return 1;
    }
    ok = JS_ToBool(ctx, result);
    JS_FreeValue(ctx, result);
    stats = TurboJS_GetRuntimeJITStats(rt);
    if (ok != 1 || stats.object_array_grouped_osr_entries < 8 ||
        stats.object_array_grouped_osr_elements < 9000 || stats.osr_bailouts != 0) {
        fprintf(stderr,
                "grouped accumulator inactive: ok=%d entries=%" PRIu64
                " elements=%" PRIu64 " bailouts=%" PRIu64 "\n",
                ok, stats.object_array_grouped_osr_entries,
                stats.object_array_grouped_osr_elements, stats.osr_bailouts);
        JS_FreeContext(ctx); JS_FreeRuntime(rt); return 1;
    }
    JS_FreeContext(ctx); JS_FreeRuntime(rt);
    puts("Grouped accumulator OSR specialization passed");
    return 0;
}
