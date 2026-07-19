#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "jit.h"
#include "internal/monotonic_clock.h"

typedef struct Context { uint64_t calls; } Context;

static uint64_t now_ns(void)
{
    return turbojs_monotonic_now_ns();
}

static TurboJSIRStatus helper(void *opaque, const TurboJSBoxedDeoptFrame *frame,
                              const TurboJSIRInstruction *instruction,
                              TurboJSBoxedValue *result)
{
    Context *ctx = (Context *)opaque;
    ctx->calls++;
    result->tag = TURBOJS_BOXED_INT64;
    result->as.integer = frame->registers[instruction->left].as.integer + 1;
    return TURBOJS_IR_OK;
}

static TurboJSIRInstruction op(TurboJSIROpcode opcode, uint16_t dst,
                               uint16_t left, int64_t imm)
{
    TurboJSIRInstruction in;
    memset(&in, 0, sizeof(in));
    in.opcode = opcode;
    in.destination = dst;
    in.left = left;
    in.immediate = imm;
    return in;
}

int main(void)
{
    TurboJSIRFunction fn;
    TurboJSNativeFunction *native = NULL;
    TurboJSIRDiagnostic diagnostic;
    TurboJSRuntimeHelperTable helpers;
    TurboJSCodeCache *vault = NULL;
    Context ctx = {0};
    int64_t args[1] = {41}, result = 0;
    const size_t cached_iterations = 100000;
    const size_t cold_iterations = 100;
    uint64_t start, cached_ns, cold_ns;

    TurboJS_IRFunctionInit(&fn, 1);
    for (int i = 0; i < 3; ++i)
        if (TurboJS_IRAllocateRegister(&fn) != (uint16_t)i) return 2;
    if (TurboJS_IREmit(&fn, op(TURBOJS_IR_ARGUMENT, 0, 0, 0)) != TURBOJS_IR_OK ||
        TurboJS_IREmit(&fn, op(TURBOJS_IR_RUNTIME_HELPER, 1, 0, 9)) != TURBOJS_IR_OK ||
        TurboJS_IREmit(&fn, op(TURBOJS_IR_CONSTANT_I64, 2, 0, 1)) != TURBOJS_IR_OK ||
        TurboJS_IREmit(&fn, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_ADD_I64, .destination=1, .left=1, .right=2 }) != TURBOJS_IR_OK ||
        TurboJS_IREmit(&fn, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_RETURN_I64, .left=1 }) != TURBOJS_IR_OK ||
        TurboJS_BaselineCompile(&fn, &native, &diagnostic) != TURBOJS_IR_OK)
        return 3;

    vault = TurboJS_CodeCacheCreate(32, 1024 * 1024);
    if (!vault) return 4;
    TurboJS_RuntimeHelperTableInit(&helpers, NULL);
    TurboJS_RuntimeHelperAttachContinuationVault(&helpers, vault);
    if (TurboJS_RuntimeHelperRegister(&helpers, 9, helper, &ctx) != TURBOJS_IR_OK)
        return 4;
    if (TurboJS_NativeInvokeWithRuntime(native, &fn, &helpers, args, 1, &result) != TURBOJS_IR_OK || result != 43)
        return 5;

    start = now_ns();
    for (size_t i = 0; i < cached_iterations; ++i) {
        result = 0;
        if (TurboJS_NativeInvokeWithRuntime(native, &fn, &helpers, args, 1, &result) != TURBOJS_IR_OK || result != 43)
            return 6;
    }
    cached_ns = now_ns() - start;

    start = now_ns();
    for (size_t i = 0; i < cold_iterations; ++i) {
        TurboJS_CodeCacheClear(vault);
        result = 0;
        if (TurboJS_NativeInvokeWithRuntime(native, &fn, &helpers, args, 1, &result) != TURBOJS_IR_OK || result != 43)
            return 7;
    }
    cold_ns = now_ns() - start;

    printf("cached_ns_per_call=%.2f\n", (double)cached_ns / cached_iterations);
    printf("recompile_ns_per_call=%.2f\n", (double)cold_ns / cold_iterations);
    printf("cache_hits=%" PRIu64 " cache_misses=%" PRIu64 " compiles=%" PRIu64 "\n",
           helpers.native_continuation_cache_hits,
           helpers.native_continuation_cache_misses,
           helpers.native_continuation_compiles);

    {
        TurboJSCodeCacheStats stats = TurboJS_CodeCacheGetStats(vault);
        printf("vault_continuation_entries=%zu vault_continuation_bytes=%zu vault_evictions=%" PRIu64 "\n",
               stats.continuation_entry_count, stats.continuation_code_bytes,
               stats.continuation_evictions);
    }
    TurboJS_RuntimeHelperTableDestroy(&helpers);
    TurboJS_CodeCacheDestroy(vault);
    TurboJS_NativeFunctionDestroy(native);
    TurboJS_IRFunctionDestroy(&fn);
    return 0;
}
