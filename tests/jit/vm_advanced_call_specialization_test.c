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

static int eval_true(JSContext *ctx, const char *name, const char *source)
{
    JSValue value = JS_Eval(ctx, source, strlen(source), name,
                            JS_EVAL_TYPE_GLOBAL);
    int result = 0;
    if (JS_IsException(value))
        return report_exception(ctx, name);
    result = JS_ToBool(ctx, value);
    JS_FreeValue(ctx, value);
    if (result != 1) {
        fprintf(stderr, "%s returned false\n", name);
        return 1;
    }
    return 0;
}

static int test_polymorphic_leaf_calls(JSContext *ctx, JSRuntime *rt)
{
    const char *source =
        "(function(){function leafA(x){return (x*3+7)|0;}"
        "let leafB=function(x){return (x^0x5a5a5a5a)|0;};"
        "function run(n){let s=0;for(let i=0;i<n;i++){"
        "const f=(i&1)?leafA:leafB;s=(s+f(i))|0;}return s;}"
        "function ref(n){let s=0,i=0;while(i<n){const f=(i&1)?leafA:leafB;"
        "s=(s+f(i))|0;i++;}return s;}"
        "if(run(6000)!==ref(6000))return false;"
        "leafB=function(x){return (x+101)|0;};"
        "return run(6000)===ref(6000);})()";
    TurboJSRuntimeJITStats before = TurboJS_GetRuntimeJITStats(rt), after;
    if (eval_true(ctx, "polymorphic-leaf.js", source))
        return 1;
    after = TurboJS_GetRuntimeJITStats(rt);
    if (after.osr_polymorphic_leaf_entries <=
            before.osr_polymorphic_leaf_entries ||
        after.osr_polymorphic_leaf_iterations <
            before.osr_polymorphic_leaf_iterations + 5000 ||
        after.osr_bailouts <= before.osr_bailouts) {
        fprintf(stderr,
                "polymorphic leaf specialization inactive: entries=%" PRIu64
                "->%" PRIu64 " iterations=%" PRIu64 "->%" PRIu64
                " bailouts=%" PRIu64 "->%" PRIu64 "\n",
                before.osr_polymorphic_leaf_entries,
                after.osr_polymorphic_leaf_entries,
                before.osr_polymorphic_leaf_iterations,
                after.osr_polymorphic_leaf_iterations,
                before.osr_bailouts, after.osr_bailouts);
        return 1;
    }
    return 0;
}

static int test_captured_closure_calls(JSContext *ctx, JSRuntime *rt)
{
    const char *source =
        "function specializedClosures(){let total=0;"
        "function make(k){return function(x){return (x+k)|0;};}"
        "const fs=[make(1),make(2),make(3),make(4)];"
        "for(let i=0;i<6000;i++)total=(total+fs[i&3](i))|0;return total;}"
        "function specializedClosuresRef(){let total=0;const captures=[1,2,3,4];"
        "for(let i=0;i<6000;i++)total=(total+i+captures[i&3])|0;return total;}"
        "(()=>{const expected=specializedClosuresRef();for(let r=0;r<12;r++)"
        "if(specializedClosures()!==expected)return false;return true;})()";
    TurboJSRuntimeJITStats before = TurboJS_GetRuntimeJITStats(rt), after;
    if (eval_true(ctx, "captured-closure.js", source))
        return 1;
    after = TurboJS_GetRuntimeJITStats(rt);
    if (after.osr_closure_call_entries <= before.osr_closure_call_entries ||
        after.osr_closure_call_iterations <
            before.osr_closure_call_iterations + 5000) {
        fprintf(stderr,
                "closure specialization inactive: entries=%" PRIu64 "->%" PRIu64
                " iterations=%" PRIu64 "->%" PRIu64 "\n",
                before.osr_closure_call_entries,
                after.osr_closure_call_entries,
                before.osr_closure_call_iterations,
                after.osr_closure_call_iterations);
        return 1;
    }
    return 0;
}

static int test_recursive_call_lowering(JSContext *ctx, JSRuntime *rt)
{
    const char *source =
        "function specializedRecursion(){function fib(n){return n<2?n:fib(n-1)+fib(n-2);}"
        "let s=0;for(let i=0;i<300;i++)s+=fib(18+(i&1));return s;}"
        "(()=>{for(let r=0;r<12;r++)if(specializedRecursion()!==1014750)return false;"
        "return true;})()";
    TurboJSRuntimeJITStats before = TurboJS_GetRuntimeJITStats(rt), after;
    if (eval_true(ctx, "recursive-call.js", source))
        return 1;
    after = TurboJS_GetRuntimeJITStats(rt);
    if (after.osr_recursive_call_entries <= before.osr_recursive_call_entries ||
        after.osr_recursive_call_iterations <
            before.osr_recursive_call_iterations + 200) {
        fprintf(stderr,
                "recursive specialization inactive: entries=%" PRIu64 "->%" PRIu64
                " iterations=%" PRIu64 "->%" PRIu64 "\n",
                before.osr_recursive_call_entries,
                after.osr_recursive_call_entries,
                before.osr_recursive_call_iterations,
                after.osr_recursive_call_iterations);
        return 1;
    }
    return 0;
}

int main(void)
{
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    TurboJSOptimizationConfig config;
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
    config.osr_threshold = 16;
    TurboJS_SetRuntimeOptimizationConfig(rt, &config);

    if (test_polymorphic_leaf_calls(ctx, rt) ||
        test_captured_closure_calls(ctx, rt) ||
        test_recursive_call_lowering(ctx, rt)) {
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    puts("Guarded call, closure, and recursion specialization passed");
    return 0;
}
