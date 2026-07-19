#include <stdio.h>
#include <string.h>
#include "turbojs.h"

static int fail_exception(JSContext *ctx, const char *where) {
    JSValue ex = JS_GetException(ctx);
    const char *text = JS_ToCString(ctx, ex);
    fprintf(stderr, "%s%s%s\n", where, text ? ": " : "", text ? text : "");
    JS_FreeCString(ctx, text);
    JS_FreeValue(ctx, ex);
    return 1;
}

int main(void) {
    static const char source[] =
        "function hashString(s){let h=2166136261|0;for(let i=0;i<s.length;i++)h=Math.imul(h^s.charCodeAt(i),16777619);return h|0;}"
        "function transformRecords(count){const rows=[];for(let i=0;i<count;i++)rows.push({id:i,group:i%23,active:(i&3)!==0,score:(i*17)%997,name:'user-'+i});const totals={};const output=[];for(let i=0;i<rows.length;i++){const row=rows[i];if(!row.active)continue;const score=(row.score+row.group*7)|0;totals[row.group]=(totals[row.group]||0)+score;output.push({id:row.id,label:row.name+':'+score,score});}const json=JSON.stringify({totals,output});const copy=JSON.parse(json);return(copy.output.length+json.length+copy.totals[7])|0;}"
        "function mergeConfigs(rounds){const base={mode:'prod',retry:3,flags:{jit:true,osr:true,trace:false},limits:{heap:64,stack:2}};let h=0;for(let i=0;i<rounds;i++){const override={retry:(i%7)+1,flags:{jit:true,osr:(i&1)===0,trace:(i%11)===0},limits:{heap:64+(i&15),stack:2}};const merged={mode:base.mode,retry:override.retry,flags:{jit:override.flags.jit,osr:override.flags.osr,trace:override.flags.trace},limits:{heap:override.limits.heap,stack:override.limits.stack}};h=(h+hashString(JSON.stringify(merged)))|0;}return h|0;}"
        "globalThis.__app_region=[transformRecords(5000),mergeConfigs(400),transformRecords(17),mergeConfigs(9)];";
    const int32_t expected[] = {270836, -711458902, 809, -869119824};
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue value, item;
    int i;
    if (!rt) return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) { JS_FreeRuntime(rt); return 1; }
    value = JS_Eval(ctx, source, sizeof(source) - 1, "application-region.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(value)) { fail_exception(ctx, "eval"); goto fail; }
    JS_FreeValue(ctx, value);
    value = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "__app_region");
    if (JS_IsException(value)) { fail_exception(ctx, "result"); goto fail; }
    for (i = 0; i < 4; ++i) {
        int32_t actual = 0;
        item = JS_GetPropertyUint32(ctx, value, (uint32_t)i);
        if (JS_ToInt32(ctx, &actual, item) || actual != expected[i]) {
            fprintf(stderr, "application region mismatch at %d: got %d expected %d\n", i, actual, expected[i]);
            JS_FreeValue(ctx, item); JS_FreeValue(ctx, value); goto fail;
        }
        JS_FreeValue(ctx, item);
    }
    JS_FreeValue(ctx, value);
    JS_FreeContext(ctx); JS_FreeRuntime(rt);
    puts("Application region reductions passed");
    return 0;
fail:
    JS_FreeContext(ctx); JS_FreeRuntime(rt); return 1;
}
