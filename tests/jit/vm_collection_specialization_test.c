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

static int eval_int64(JSContext *ctx, const char *name, const char *source,
                      int64_t expected)
{
    JSValue value = JS_Eval(ctx, source, strlen(source), name, JS_EVAL_TYPE_GLOBAL);
    int64_t result = 0;
    if (JS_IsException(value))
        return report_exception(ctx, name);
    if (JS_ToInt64(ctx, &result, value) || result != expected) {
        fprintf(stderr, "%s result mismatch: got=%" PRId64 " expected=%" PRId64 "\n",
                name, result, expected);
        JS_FreeValue(ctx, value);
        return 1;
    }
    JS_FreeValue(ctx, value);
    return 0;
}

static int test_object_arrays(JSContext *ctx, JSRuntime *rt)
{
    const char *update_source =
        "(function(){const objs=[];for(let i=0;i<6000;i++)"
        "objs.push({x:i,y:i+1,z:i+2});let s=0;for(let r=0;r<8;r++)"
        "for(let i=0;i<objs.length;i++){const o=objs[i];"
        "o.x=(o.x+o.y-r)|0;s=(s+o.x+o.z)|0;}return s;})()";
    const char *poly_source =
        "(function(){const a=[];for(let i=0;i<6000;i++){"
        "if(i%3===0)a.push({x:i,y:1});else if(i%3===1)"
        "a.push({y:2,x:i,z:3});else a.push({x:i,w:4});}"
        "let s=0;for(let r=0;r<5;r++)for(let i=0;i<a.length;i++)"
        "s=(s+a[i].x)|0;return s;})()";
    TurboJSRuntimeJITStats stats;
    if (eval_int64(ctx, "object-update-specialization.js", update_source, 935652000) ||
        eval_int64(ctx, "object-polymorphic-specialization.js", poly_source, 89985000))
        return 1;
    stats = TurboJS_GetRuntimeJITStats(rt);
    if (stats.object_array_update_osr_entries < 8 ||
        stats.object_array_polymorphic_osr_entries < 5 ||
        stats.object_array_osr_entries < 13 ||
        stats.object_array_osr_elements < 70000 || stats.osr_bailouts != 0) {
        fprintf(stderr,
                "object-array specialization inactive: entries=%" PRIu64
                " elements=%" PRIu64 " update=%" PRIu64 " poly=%" PRIu64
                " bailouts=%" PRIu64 "\n",
                stats.object_array_osr_entries, stats.object_array_osr_elements,
                stats.object_array_update_osr_entries,
                stats.object_array_polymorphic_osr_entries, stats.osr_bailouts);
        return 1;
    }
    return 0;
}

static int test_holey_prototype_guard(JSContext *ctx, JSRuntime *rt)
{
    const char *source =
        "(function(){function sum(a){let s=0;for(let i=0;i<a.length;i++)"
        "if(a[i]!==undefined)s=(s+a[i])|0;return s;}"
        "const a=new Array(300);let expected=0;for(let i=0;i<a.length;i+=3)"
        "{a[i]=i;expected=(expected+i)|0;}sum(a);Array.prototype[1]=7;"
        "const got=sum(a);delete Array.prototype[1];return got===((expected+7)|0);})()";
    TurboJSRuntimeJITStats before = TurboJS_GetRuntimeJITStats(rt), after;
    if (eval_int64(ctx, "holey-prototype-guard.js", source, 1))
        return 1;
    after = TurboJS_GetRuntimeJITStats(rt);
    if (after.holey_array_osr_entries <= before.holey_array_osr_entries ||
        after.osr_bailouts <= before.osr_bailouts) {
        fprintf(stderr,
                "holey prototype guard inactive: entries=%" PRIu64 "->%" PRIu64
                " bailouts=%" PRIu64 "->%" PRIu64 "\n",
                before.holey_array_osr_entries, after.holey_array_osr_entries,
                before.osr_bailouts, after.osr_bailouts);
        return 1;
    }
    return 0;
}

static int test_typed_array_affine_sum(JSContext *ctx, JSRuntime *rt)
{
    const char *source =
        "(function(){function fast(n){const a=new Float64Array(n);"
        "for(let i=0;i<a.length;i++)a[i]=i*0.25;let s=0;"
        "for(let i=0;i<a.length;i++){a[i]=a[i]*1.000001+0.5;s+=a[i];}return s;}"
        "function reference(n){const a=new Float64Array(n);let i=0,s=0;"
        "while(i<a.length){a[i]=i*0.25;i++;}i=0;while(i<a.length){"
        "a[i]=a[i]*1.000001+0.5;s+=a[i];i++;}return s;}"
        "return fast(1000)===reference(1000)&&fast(1237)===reference(1237);})()";
    TurboJSRuntimeJITStats before = TurboJS_GetRuntimeJITStats(rt), after;
    if (eval_int64(ctx, "typed-affine-sum.js", source, 1))
        return 1;
    after = TurboJS_GetRuntimeJITStats(rt);
    if (after.typed_array_affine_sum_osr_entries <
            before.typed_array_affine_sum_osr_entries + 2 ||
        after.typed_array_affine_sum_osr_elements <=
            before.typed_array_affine_sum_osr_elements) {
        fprintf(stderr,
                "typed affine sum inactive: entries=%" PRIu64 "->%" PRIu64
                " elements=%" PRIu64 "->%" PRIu64 "\n",
                before.typed_array_affine_sum_osr_entries,
                after.typed_array_affine_sum_osr_entries,
                before.typed_array_affine_sum_osr_elements,
                after.typed_array_affine_sum_osr_elements);
        return 1;
    }
    return 0;
}

static int test_object_accessor_guard(JSContext *ctx, JSRuntime *rt)
{
    const char *source =
        "(function(){function optimized(a,outer){let s=0;for(let i=0;i<a.length;i++){"
        "const o=a[i];o.x=(o.x+o.y-outer)|0;s=(s+o.x+o.z)|0;}return s;}"
        "function reference(a,outer){let s=0,i=0;while(i<a.length){const o=a[i];"
        "o.x=(o.x+o.y-outer)|0;s=(s+o.x+o.z)|0;i++;}return s;}"
        "function make(){const a=[];let state=150,gets=0,sets=0;"
        "for(let i=0;i<300;i++)a.push({x:i,y:i+1,z:i+2});"
        "Object.defineProperty(a[150],'x',{configurable:true,enumerable:true,"
        "get(){gets++;return state;},set(v){sets++;state=v;}});"
        "return {a,state:()=>state,gets:()=>gets,sets:()=>sets};}"
        "const warm=[];for(let i=0;i<300;i++)warm.push({x:i,y:i+1,z:i+2});"
        "optimized(warm,1);const x=make(),y=make();const got=optimized(x.a,2);"
        "const expected=reference(y.a,2);return got===expected&&x.state()===y.state()"
        "&&x.gets()===y.gets()&&x.sets()===y.sets();})()";
    TurboJSRuntimeJITStats before = TurboJS_GetRuntimeJITStats(rt), after;
    if (eval_int64(ctx, "object-accessor-guard.js", source, 1))
        return 1;
    after = TurboJS_GetRuntimeJITStats(rt);
    if (after.osr_bailouts <= before.osr_bailouts) {
        fprintf(stderr, "object accessor guard did not bail out\n");
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
    config.osr_threshold = 1;
    TurboJS_SetRuntimeOptimizationConfig(rt, &config);

    if (test_object_arrays(ctx, rt) ||
        test_holey_prototype_guard(ctx, rt) ||
        test_typed_array_affine_sum(ctx, rt) ||
        test_object_accessor_guard(ctx, rt)) {
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    puts("Object, holey-array, typed-array, and guard specialization passed");
    return 0;
}
