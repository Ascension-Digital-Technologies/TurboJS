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
    static const char source[] =
        "(function(){"
        " function add1(x){return x+1;}"
        " function add2(x){return x+2;}"
        " function add3(x){return x+3;}"
        " function add4(x){return x+4;}"
        " function add5(x){return x+5;}"
        " function invoke(fn,x){return fn(x);}"
        " var sum=0,i;"
        " for(i=0;i<1000;i++) sum+=invoke(add1,i);"
        " for(i=0;i<1000;i++) sum+=invoke((i&1)?add1:add2,i);"
        " var f=[add1,add2,add3,add4,add5];"
        " for(i=0;i<1000;i++) sum+=invoke(f[i%5],i);"
        " return sum;"
        "})()";
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue result;
    TurboJSRuntimeJITStats stats;
    int64_t value = 0;

    if (!rt)
        return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return 1;
    }
    result = JS_Eval(ctx, source, sizeof(source) - 1,
                     "call-feedback.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        int rc = fail(ctx, "call feedback evaluation failed");
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return rc;
    }
    if (JS_ToInt64(ctx, &value, result)) {
        JS_FreeValue(ctx, result);
        fail(ctx, "call feedback result conversion failed");
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }
    JS_FreeValue(ctx, result);

    stats = TurboJS_GetRuntimeJITStats(rt);
    printf("Telemetry calls: observations=%llu mono=%llu poly=%llu mega=%llu transitions=%llu result=%lld\n",
           (unsigned long long)stats.call_feedback_observations,
           (unsigned long long)stats.call_feedback_monomorphic,
           (unsigned long long)stats.call_feedback_polymorphic,
           (unsigned long long)stats.call_feedback_megamorphic,
           (unsigned long long)stats.call_feedback_transitions,
           (long long)value);
    if (stats.call_feedback_observations < 3000 ||
        stats.call_feedback_monomorphic < 1 ||
        stats.call_feedback_polymorphic < 1 ||
        stats.call_feedback_megamorphic < 1 ||
        stats.call_feedback_transitions < 3) {
        fprintf(stderr, "expected mono/poly/megamorphic call-site transitions\n");
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    puts("VM Telemetry call feedback integration passed");
    return 0;
}
