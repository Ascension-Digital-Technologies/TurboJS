#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "turbojs.h"
#include "optimization.h"

static int report_exception(JSContext *ctx, const char *where)
{
    JSValue ex = JS_GetException(ctx);
    const char *text = JS_ToCString(ctx, ex);
    fprintf(stderr, "%s%s%s\n", where, text ? ": " : "", text ? text : "");
    JS_FreeCString(ctx, text);
    JS_FreeValue(ctx, ex);
    return 1;
}

static int eval_i32(JSContext *ctx, const char *source, int32_t expected)
{
    JSValue value = JS_Eval(ctx, source, strlen(source), "osr-dense-array.js",
                            JS_EVAL_TYPE_GLOBAL);
    int32_t result = 0;
    if (JS_IsException(value))
        return report_exception(ctx, "evaluation failed");
    if (JS_ToInt32(ctx, &result, value) || result != expected) {
        fprintf(stderr, "unexpected result: got=%d expected=%d\n", result, expected);
        JS_FreeValue(ctx, value);
        return 1;
    }
    JS_FreeValue(ctx, value);
    return 0;
}

int main(void)
{
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    TurboJSRuntimeJITStats stats;
    if (!rt)
        return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return 1;
    }
    TurboJS_SetRuntimeJITThreshold(rt, 1);

    if (eval_i32(ctx,
        "(function(){"
        " const a=[]; for(let i=0;i<2048;i++) a[i]=i;"
        " let s=0; for(let i=0;i<2048;i++) s+=a[i];"
        " return s; })()", 2096128))
        goto fail;

    stats = TurboJS_GetRuntimeJITStats(rt);
    printf("dense arrays: loads=%llu stores=%llu slow=%llu\n",
           (unsigned long long)stats.dense_array_load_hits,
           (unsigned long long)stats.dense_array_store_hits,
           (unsigned long long)stats.dense_array_slow_paths);
    printf("osr: backedges=%llu requests=%llu captures=%llu\n",
           (unsigned long long)stats.osr_backedges,
           (unsigned long long)stats.osr_compile_requests,
           (unsigned long long)stats.osr_frame_captures);
    /* Once OSR succeeds, the native remainder no longer increments Pulse's
       interpreter backedge/capture counters. Validate activation and fast-path
       coverage instead of requiring the pre-OSR interpreter totals. */
    if (stats.dense_array_load_hits < 2000 || stats.dense_array_store_hits < 2000 ||
        stats.dense_array_slow_paths > 64 || stats.osr_backedges < 1 ||
        stats.osr_compile_requests < 2) {
        fprintf(stderr, "expected dense-array fast paths and OSR activation\n");
        goto fail;
    }

    /* A hole must not leak the internal uninitialized sentinel. Prototype
       lookup remains observable and therefore must take the canonical path. */
    if (eval_i32(ctx,
        "(function(){ Array.prototype[1]=42; const a=[1,,3];"
        " const r=a[1]; delete Array.prototype[1]; return r; })()", 42))
        goto fail;

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    puts("VM OSR backedge capture and dense-array fast paths passed");
    return 0;
fail:
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return 1;
}
