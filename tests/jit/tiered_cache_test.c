#include <stdio.h>
#include "jit.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static int build_add(TurboJSIRFunction *ir)
{
    TurboJSIRInstruction ins = {0};
    uint16_t a, b, sum;
    TurboJS_IRFunctionInit(ir, 2);
    a = TurboJS_IRAllocateRegister(ir);
    b = TurboJS_IRAllocateRegister(ir);
    sum = TurboJS_IRAllocateRegister(ir);
    ins.opcode = TURBOJS_IR_ARGUMENT; ins.destination = a; ins.immediate = 0;
    CHECK(TurboJS_IREmit(ir, ins) == TURBOJS_IR_OK);
    ins.opcode = TURBOJS_IR_ARGUMENT; ins.destination = b; ins.immediate = 1;
    CHECK(TurboJS_IREmit(ir, ins) == TURBOJS_IR_OK);
    ins.opcode = TURBOJS_IR_ADD_I64; ins.destination = sum; ins.left = a; ins.right = b;
    CHECK(TurboJS_IREmit(ir, ins) == TURBOJS_IR_OK);
    ins.opcode = TURBOJS_IR_RETURN_I64; ins.left = sum;
    CHECK(TurboJS_IREmit(ir, ins) == TURBOJS_IR_OK);
    return 0;
}

int main(void)
{
    TurboJSIRFunction ir;
    TurboJSIRDiagnostic diagnostic = {0};
    TurboJSCodeCache *cache;
    TurboJSTieredFunction tiered;
    TurboJSTieredResult route;
    TurboJSCodeCacheStats stats;
    int key;
    int64_t args[2] = {20, 22};
    int64_t result;

    CHECK(build_add(&ir) == 0);
    cache = TurboJS_CodeCacheCreate(2, 4096);
    CHECK(cache != NULL);
    TurboJS_TieredFunctionInit(&tiered, &key, 3);

    CHECK(TurboJS_TieredInvoke(&tiered, cache, &ir, args, 2, &result, &route, &diagnostic) == TURBOJS_IR_OK);
    CHECK(route == TURBOJS_TIERED_INTERPRETED && result == 42);
    CHECK(TurboJS_TieredInvoke(&tiered, cache, &ir, args, 2, &result, &route, &diagnostic) == TURBOJS_IR_OK);
    CHECK(route == TURBOJS_TIERED_INTERPRETED);
    CHECK(TurboJS_TieredInvoke(&tiered, cache, &ir, args, 2, &result, &route, &diagnostic) == TURBOJS_IR_OK);
    CHECK(route == TURBOJS_TIERED_COMPILED);
    CHECK(TurboJS_TieredInvoke(&tiered, cache, &ir, args, 2, &result, &route, &diagnostic) == TURBOJS_IR_OK);
    CHECK(route == TURBOJS_TIERED_NATIVE);

    {
        int keys[96];
        size_t i;
        const TurboJSNativeFunction *compiled = NULL;
        for (i = 0; i < 96; ++i) {
            keys[i] = (int)i;
            CHECK(TurboJS_CodeCacheCompile(cache, &keys[i], &ir, &compiled, &diagnostic) == TURBOJS_IR_OK);
            CHECK(compiled != NULL);
        }
        CHECK(TurboJS_CodeCacheLookup(cache, &keys[95]) != NULL);
        CHECK(TurboJS_CodeCacheLookup(cache, &keys[0]) == NULL);
        TurboJS_CodeCacheInvalidate(cache, &keys[95]);
        CHECK(TurboJS_CodeCacheLookup(cache, &keys[95]) == NULL);
    }

    stats = TurboJS_CodeCacheGetStats(cache);
    CHECK(stats.entry_count <= 2);
    CHECK(stats.evictions > 0);
    CHECK(stats.compilations == 97);
    CHECK(stats.hits >= 1);
    CHECK(stats.code_bytes > 0);
    printf("tiered cache passed: calls=%u hits=%llu misses=%llu code=%zu bytes\n",
           tiered.call_count, (unsigned long long)stats.hits,
           (unsigned long long)stats.misses, stats.code_bytes);

    TurboJS_CodeCacheDestroy(cache);
    TurboJS_IRFunctionDestroy(&ir);
    return 0;
}
