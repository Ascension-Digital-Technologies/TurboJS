#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "turbojs.h"
#include "optimization.h"

static int report_exception(JSContext *ctx, const char *where) {
    JSValue ex = JS_GetException(ctx);
    const char *s = JS_ToCString(ctx, ex);
    fprintf(stderr, "%s%s%s\n", where, s ? ": " : "", s ? s : "");
    JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, ex);
    return 1;
}

int main(void) {
    static const char source[] =
        "'use strict';"
        "function add(x){return(x+3)|0;}"
        "function mul(x){return Math.imul(x,5)|0;}"
        "function xor(x){return(x^0x5a5a5a5a)|0;}"
        "function route(rounds){const callbacks=[add,mul];let s=0;"
        "for(let r=0;r<rounds;r++){for(let i=0;i<20000;i++)"
        "s=(s+callbacks[i&1](i+r))|0;}callbacks[1]=xor;"
        "for(let i=0;i<5000;i++)s=(s+callbacks[i&1](i))|0;return s|0;}"
        "function reference(rounds){const callbacks=[add,mul];let s=0,r=0;"
        "while(r<rounds){let i=0;while(i<20000){s=(s+callbacks[i&1](i+r))|0;i++;}r++;}"
        "callbacks[1]=xor;let j=0;while(j<5000){s=(s+callbacks[j&1](j))|0;j++;}return s|0;}"
        "const a=route(8);const old=Math.imul;Math.imul=function(){return 7;};"
        "const expected=reference(1);const b=route(1);Math.imul=old;"
        "globalThis.__router_result=[a,b,b===expected];";
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue result, a, b, same;
    int32_t av = 0, bv = 0;
    int same_value = 0;
    TurboJSRuntimeJITStats before, after;
    if (!rt) return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) { JS_FreeRuntime(rt); return 1; }
    before = TurboJS_GetRuntimeJITStats(rt);
    result = JS_Eval(ctx, source, sizeof(source)-1, "callback-router.mjs", JS_EVAL_TYPE_MODULE);
    if (JS_IsException(result)) { report_exception(ctx, "callback router"); goto fail; }
    JS_FreeValue(ctx, result);
    result = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "__router_result");
    if (JS_IsException(result)) { report_exception(ctx, "callback router result"); goto fail; }
    a = JS_GetPropertyUint32(ctx, result, 0);
    b = JS_GetPropertyUint32(ctx, result, 1);
    same = JS_GetPropertyUint32(ctx, result, 2);
    same_value = JS_ToBool(ctx, same);
    if (JS_ToInt32(ctx, &av, a) || JS_ToInt32(ctx, &bv, b) || same_value < 0) { report_exception(ctx, "convert"); goto fail_values; }
    after = TurboJS_GetRuntimeJITStats(rt);
    JS_FreeValue(ctx, a); JS_FreeValue(ctx, b); JS_FreeValue(ctx, same); JS_FreeValue(ctx, result);
    if (av != 2025876616 || !same_value ||
        after.osr_polymorphic_leaf_entries <= before.osr_polymorphic_leaf_entries ||
        after.osr_polymorphic_leaf_iterations < before.osr_polymorphic_leaf_iterations + 165000) {
        fprintf(stderr, "callback router inactive or incorrect: a=%d b=%d entries=%" PRIu64 " iterations=%" PRIu64 "\n",
                av, bv, after.osr_polymorphic_leaf_entries, after.osr_polymorphic_leaf_iterations);
        goto fail;
    }
    JS_FreeContext(ctx); JS_FreeRuntime(rt);
    puts("Whole-function callback router passed");
    return 0;
fail_values:
    JS_FreeValue(ctx, a); JS_FreeValue(ctx, b); JS_FreeValue(ctx, same); JS_FreeValue(ctx, result);
fail:
    JS_FreeContext(ctx); JS_FreeRuntime(rt); return 1;
}
