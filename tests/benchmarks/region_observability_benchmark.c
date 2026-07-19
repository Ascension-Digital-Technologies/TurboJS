#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "turbojs.h"
#include "optimization.h"
#include "internal/monotonic_clock.h"

static uint64_t now_ns(void) {
    return turbojs_monotonic_now_ns();
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv) {
    static const char source[] =
        "(function benchmark(repetitions){let checksum=0;"
        "for(let r=0;r<repetitions;r++){let sum=0;"
        "for(let i=0;i<1000000;i++)sum+=i;checksum+=sum;}return checksum;})";
    int runs = argc > 1 ? atoi(argv[1]) : 15;
    int repetitions = argc > 2 ? atoi(argv[2]) : 5;
    int warmups = argc > 3 ? atoi(argv[3]) : 3;
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue fn, arg, result;
    uint64_t *samples;
    int i;
    int64_t checksum = 0;
    TurboJSRuntimeJITStats before, after;
    if (!rt || runs < 1 || repetitions < 1 || warmups < 0) return 1;
    {
        TurboJSOptimizationConfig config = TurboJS_GetRuntimeOptimizationConfig(rt);
        config.baseline_threshold = 1;
        config.optimizing_threshold = 1;
        TurboJS_SetRuntimeOptimizationConfig(rt, &config);
        TurboJS_ResetRuntimeJITStats(rt);
    }
    ctx = JS_NewContext(rt);
    if (!ctx) return 1;
    fn = JS_Eval(ctx, source, sizeof(source)-1, "phase69-region.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(fn)) return 1;
    arg = JS_NewInt32(ctx, repetitions);
    for (i = 0; i < warmups; ++i) {
        result = JS_Call(ctx, fn, JS_UNDEFINED, 1, &arg);
        if (JS_IsException(result)) return 1;
        JS_FreeValue(ctx, result);
    }
    before = TurboJS_GetRuntimeJITStats(rt);
    samples = (uint64_t *)calloc((size_t)runs, sizeof(*samples));
    if (!samples) return 1;
    for (i = 0; i < runs; ++i) {
        uint64_t t0 = now_ns();
        result = JS_Call(ctx, fn, JS_UNDEFINED, 1, &arg);
        samples[i] = now_ns() - t0;
        if (JS_IsException(result) || JS_ToInt64(ctx, &checksum, result)) return 1;
        JS_FreeValue(ctx, result);
    }
    after = TurboJS_GetRuntimeJITStats(rt);
    qsort(samples, (size_t)runs, sizeof(*samples), cmp_u64);
    printf("{\n");
    printf("  \"engine\": \"TurboJS\",\n");
    printf("  \"clock\": \"monotonic_ns\",\n");
    printf("  \"runs\": %d, \"warmups\": %d, \"repetitions\": %d,\n", runs, warmups, repetitions);
    printf("  \"median_ns\": %" PRIu64 ", \"min_ns\": %" PRIu64 ", \"max_ns\": %" PRIu64 ",\n", samples[runs/2], samples[0], samples[runs-1]);
    printf("  \"checksum\": %" PRId64 ",\n", checksum);
    printf("  \"jit\": {\"total_region_compilations\": %" PRIu64 ", \"measured_region_native_calls\": %" PRIu64 ", \"total_region_compile_failures\": %" PRIu64 "}\n",
           after.region_compilations,
           after.region_native_calls - before.region_native_calls,
           after.region_compile_failures);
    printf("}\n");
    free(samples);
    JS_FreeValue(ctx, arg);
    JS_FreeValue(ctx, fn);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return checksum == 2499997500000LL ? 0 : 1;
}
