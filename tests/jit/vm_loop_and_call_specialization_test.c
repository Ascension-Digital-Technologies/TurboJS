#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "turbojs.h"
#include "optimization.h"

static int eval_int64(const char *name, const char *source, int64_t expected,
                      TurboJSRuntimeJITStats *stats_out)
{
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue value;
    int64_t result = 0;
    TurboJSOptimizationConfig config;
    TurboJSRuntimeJITStats stats;
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
    value = JS_Eval(ctx, source, strlen(source), name, JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(value) || JS_ToInt64(ctx, &result, value) || result != expected) {
        fprintf(stderr, "%s result mismatch: got=%" PRId64 " expected=%" PRId64 "\n",
                name, result, expected);
        JS_FreeValue(ctx, value);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }
    JS_FreeValue(ctx, value);
    stats = TurboJS_GetRuntimeJITStats(rt);
    if (stats_out)
        *stats_out = stats;
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return 0;
}

static int test_leaf_call_specialization(void)
{
    const char *source =
        "(function(){"
        "function leaf(x){return ((x*3)+1)|0;}"
        "function mono(n){let s=0;for(let i=0;i<n;i++)s=(s+leaf(i))|0;return s;}"
        "let total=0;for(let k=0;k<8;k++)total+=mono(1000+k);return total;})()";
    TurboJSRuntimeJITStats stats;
    if (eval_int64("leaf-call-specialization.js", source, 12080196, &stats))
        return 1;
    if (stats.osr_leaf_call_entries < 8 || stats.osr_leaf_call_iterations < 8000 ||
        stats.osr_bailouts != 0) {
        fprintf(stderr,
                "leaf-call specialization inactive: entries=%" PRIu64
                " iterations=%" PRIu64 " bailouts=%" PRIu64 "\n",
                stats.osr_leaf_call_entries, stats.osr_leaf_call_iterations,
                stats.osr_bailouts);
        return 1;
    }
    return 0;
}

static int test_int32_mix_specialization(void)
{
    const char *source =
        "(function(){"
        "function mix(n){let x=0;for(let i=0;i<n;i++){"
        "x=(Math.imul(x^i,1664525)+1013904223)|0;"
        "x=(x^(x>>>13))|0;}return x;}"
        "let total=0;for(let k=0;k<8;k++)total+=mix(1000+k);return total;})()";
    TurboJSRuntimeJITStats stats;
    if (eval_int64("int32-mix-specialization.js", source, -4697552797LL, &stats))
        return 1;
    if (stats.osr_int32_mix_entries < 8 || stats.osr_int32_mix_iterations < 8000 ||
        stats.osr_bailouts != 0) {
        fprintf(stderr,
                "Int32 mix specialization inactive: entries=%" PRIu64
                " iterations=%" PRIu64 " bailouts=%" PRIu64 "\n",
                stats.osr_int32_mix_entries, stats.osr_int32_mix_iterations,
                stats.osr_bailouts);
        return 1;
    }
    return 0;
}

static int test_math_imul_identity_guard(void)
{
    const char *source =
        "(function(){"
        "function mix(n){let x=0;for(let i=0;i<n;i++){"
        "x=(Math.imul(x^i,1664525)+1013904223)|0;"
        "x=(x^(x>>13))|0;}return x;}"
        "let native=Math.imul;mix(1000);Math.imul=function(a,b){return 17;};"
        "let result=mix(100);Math.imul=native;return result;})()";
    TurboJSRuntimeJITStats stats;
    if (eval_int64("imul-guard-specialization.js", source, 1013911559, &stats))
        return 1;
    if (stats.osr_int32_mix_entries < 1 || stats.osr_bailouts < 1) {
        fprintf(stderr,
                "Math.imul identity guard inactive: mix_entries=%" PRIu64
                " bailouts=%" PRIu64 "\n",
                stats.osr_int32_mix_entries, stats.osr_bailouts);
        return 1;
    }
    return 0;
}

static int test_negative_cache(void)
{
    const char *source =
        "(function(){"
        "function unsupported(n){let s=0;for(let i=0;i<n;i++){s=(s+(i%7))|0;}return s;}"
        "let total=0;for(let k=0;k<8;k++)total+=unsupported(1000+k);return total;})()";
    TurboJSRuntimeJITStats stats;
    if (eval_int64("negative-cache-specialization.js", source, 24053, &stats))
        return 1;
    if (stats.osr_rejections_unsupported < 1 || stats.osr_negative_cache_hits < 100) {
        fprintf(stderr,
                "OSR negative cache inactive: rejected=%" PRIu64 " hits=%" PRIu64 "\n",
                stats.osr_rejections_unsupported, stats.osr_negative_cache_hits);
        return 1;
    }
    return 0;
}

int main(void)
{
    if (test_leaf_call_specialization())
        return 1;
    if (test_int32_mix_specialization())
        return 1;
    if (test_math_imul_identity_guard())
        return 1;
    if (test_negative_cache())
        return 1;
    puts("Counted-loop, call, guard, and negative-cache specialization passed");
    return 0;
}
