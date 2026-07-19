#include <math.h>
#include <stdio.h>
#include "turbojs.h"
#include "optimization.h"

int main(void)
{
    static const char source[] = "(function f(a,b){ return (a+b)*a/b; })";
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue fn;
    TurboJSRuntimeJITStats stats;
    int i;
    if (!rt) return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) { JS_FreeRuntime(rt); return 1; }
    TurboJS_SetRuntimeJITThreshold(rt, 3);
    fn = JS_Eval(ctx, source, sizeof(source)-1, "float-vm.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(fn)) return 1;
    for (i=0;i<1000;i++) {
        JSValue args[2] = { JS_NewFloat64(ctx, 1.5 + i * 0.001), JS_NewFloat64(ctx, 2.25) };
        JSValue r = JS_Call(ctx, fn, JS_UNDEFINED, 2, args);
        double got = 0.0, expected = ((1.5 + i * 0.001) + 2.25) * (1.5 + i * 0.001) / 2.25;
        if (JS_IsException(r) || JS_ToFloat64(ctx,&got,r) || fabs(got-expected) > 1e-12) {
            fprintf(stderr,"float mismatch at %d: %.17g vs %.17g\n",i,got,expected);
            return 1;
        }
        JS_FreeValue(ctx,r);
    }
    stats=TurboJS_GetRuntimeJITStats(rt);
    printf("VM Float64 JIT: native=%llu interpreted=%llu compilations=%llu bytes=%zu\n",
      (unsigned long long)stats.native_calls,(unsigned long long)stats.interpreted_calls,
      (unsigned long long)stats.compilations,stats.native_code_bytes);
    if (stats.native_calls < 900 || stats.compilations < 1) return 1;
    JS_FreeValue(ctx,fn); JS_FreeContext(ctx); JS_FreeRuntime(rt);
    puts("JavaScript Float64 baseline JIT integration passed");
    return 0;
}
