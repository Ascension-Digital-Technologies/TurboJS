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
        "(function read(o) { return o.x + o.x + o.x + o.x; })";
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue fn, write_fn, object;
    TurboJSRuntimeJITStats stats;
    int i;

    if (!rt)
        return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return 1;
    }
    fn = JS_Eval(ctx, source, sizeof(source) - 1, "property-ic.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(fn)) {
        int rc = fail(ctx, "evaluation failed");
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return rc;
    }

    write_fn = JS_Eval(ctx, "(function write(o,v){ o.x=v; return o.x; })",
                      sizeof("(function write(o,v){ o.x=v; return o.x; })") - 1,
                      "property-write-ic.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(write_fn)) {
        JS_FreeValue(ctx, fn);
        fail(ctx, "write function evaluation failed");
        JS_FreeContext(ctx); JS_FreeRuntime(rt); return 1;
    }
    object = JS_NewObject(ctx);
    if (JS_IsException(object) || JS_SetPropertyStr(ctx, object, "x", JS_NewInt32(ctx, 7)) < 0) {
        JS_FreeValue(ctx, fn);
        JS_FreeValue(ctx, object);
        fail(ctx, "object setup failed");
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }

    for (i = 0; i < 2000; ++i) {
        JSValue result = JS_Call(ctx, fn, JS_UNDEFINED, 1, &object);
        int32_t value = 0;
        if (JS_IsException(result) || JS_ToInt32(ctx, &value, result) || value != 28) {
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, object);
            JS_FreeValue(ctx, fn);
            fail(ctx, "property read failed");
            JS_FreeContext(ctx);
            JS_FreeRuntime(rt);
            return 1;
        }
        JS_FreeValue(ctx, result);
    }

    stats = TurboJS_GetRuntimeJITStats(rt);
    printf("property IC: hits=%llu misses=%llu fills=%llu\n",
           (unsigned long long)stats.property_ic_hits,
           (unsigned long long)stats.property_ic_misses,
           (unsigned long long)stats.property_ic_fills);
    if (stats.property_ic_hits < 7000 || stats.property_ic_fills < 1) {
        fprintf(stderr, "expected monomorphic property cache activity\n");
        JS_FreeValue(ctx, object);
        JS_FreeValue(ctx, fn);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }

    /* A shape transition must miss safely and refill rather than using a stale slot. */
    if (JS_SetPropertyStr(ctx, object, "y", JS_NewInt32(ctx, 1)) < 0) {
        JS_FreeValue(ctx, object);
        JS_FreeValue(ctx, fn);
        fail(ctx, "shape transition failed");
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }
    {
        JSValue result = JS_Call(ctx, fn, JS_UNDEFINED, 1, &object);
        int32_t value = 0;
        if (JS_IsException(result) || JS_ToInt32(ctx, &value, result) || value != 28) {
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, object);
            JS_FreeValue(ctx, fn);
            fail(ctx, "post-transition read failed");
            JS_FreeContext(ctx);
            JS_FreeRuntime(rt);
            return 1;
        }
        JS_FreeValue(ctx, result);
    }

    /* A single property site should retain a small family of shapes instead of
       thrashing when objects have different layouts. */
    {
        JSValue objects[4];
        TurboJSRuntimeJITStats before = TurboJS_GetRuntimeJITStats(rt), after;
        int j;
        for (j = 0; j < 4; ++j) {
            objects[j] = JS_NewObject(ctx);
            if (j > 0) JS_SetPropertyStr(ctx, objects[j], "pad0", JS_NewInt32(ctx, j));
            if (j > 1) JS_SetPropertyStr(ctx, objects[j], "pad1", JS_NewInt32(ctx, j));
            if (j > 2) JS_SetPropertyStr(ctx, objects[j], "pad2", JS_NewInt32(ctx, j));
            if (JS_SetPropertyStr(ctx, objects[j], "x", JS_NewInt32(ctx, 10 + j)) < 0) return 1;
        }
        for (i = 0; i < 4000; ++i) {
            JSValue arg = objects[i & 3];
            JSValue result = JS_Call(ctx, fn, JS_UNDEFINED, 1, &arg);
            int32_t value = 0;
            if (JS_IsException(result) || JS_ToInt32(ctx, &value, result) || value != 4 * (10 + (i & 3))) return 1;
            JS_FreeValue(ctx, result);
        }
        after = TurboJS_GetRuntimeJITStats(rt);
        if (after.property_ic_hits - before.property_ic_hits < 15000) {
            fprintf(stderr, "polymorphic property cache did not retain four shapes\n");
            return 1;
        }
        for (j = 0; j < 4; ++j) JS_FreeValue(ctx, objects[j]);
    }

    {
        TurboJSRuntimeJITStats before = TurboJS_GetRuntimeJITStats(rt), after;
        for (i = 0; i < 3000; ++i) {
            JSValue argv[2] = { object, JS_NewInt32(ctx, i) };
            JSValue result = JS_Call(ctx, write_fn, JS_UNDEFINED, 2, argv);
            int32_t value = -1;
            JS_FreeValue(ctx, argv[1]);
            if (JS_IsException(result) || JS_ToInt32(ctx, &value, result) || value != i) return 1;
            JS_FreeValue(ctx, result);
        }
        after = TurboJS_GetRuntimeJITStats(rt);
        if (after.property_ic_hits - before.property_ic_hits < 2500) {
            fprintf(stderr, "polymorphic property write cache did not warm\n");
            return 1;
        }
    }

    JS_FreeValue(ctx, object);
    JS_FreeValue(ctx, write_fn);
    JS_FreeValue(ctx, fn);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    puts("VM property inline cache integration passed");
    return 0;
}
