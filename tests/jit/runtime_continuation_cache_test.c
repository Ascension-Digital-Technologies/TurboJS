#include <stdio.h>
#include <string.h>
#include "jit.h"

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)

static TurboJSIRStatus helper(void *opaque, const TurboJSBoxedDeoptFrame *frame,
                              const TurboJSIRInstruction *instruction,
                              TurboJSBoxedValue *result)
{
    (void)opaque;
    result->tag = TURBOJS_BOXED_INT64;
    result->as.integer = frame->registers[instruction->left].as.integer + instruction->immediate;
    return TURBOJS_IR_OK;
}

static TurboJSIRInstruction op(TurboJSIROpcode opcode, uint16_t dst,
                               uint16_t left, int64_t imm)
{
    TurboJSIRInstruction in;
    memset(&in, 0, sizeof(in));
    in.opcode = opcode; in.destination = dst; in.left = left; in.immediate = imm;
    return in;
}

int main(void)
{
    enum { FUNCTION_COUNT = 17 };
    TurboJSIRFunction functions[FUNCTION_COUNT];
    TurboJSNativeFunction *natives[FUNCTION_COUNT] = {0};
    TurboJSIRDiagnostic diagnostic;
    TurboJSRuntimeHelperTable helpers;
    TurboJSCodeCache *vault = NULL;
    int64_t argument = 10, result = 0;

    vault = TurboJS_CodeCacheCreate(16, 1024 * 1024);
    CHECK(vault != NULL);
    TurboJS_RuntimeHelperTableInit(&helpers, NULL);
    TurboJS_RuntimeHelperAttachContinuationVault(&helpers, vault);
    CHECK(TurboJS_RuntimeHelperRegister(&helpers, 1, helper, NULL) == TURBOJS_IR_OK);
    for (int i = 0; i < FUNCTION_COUNT; ++i) {
        TurboJS_IRFunctionInit(&functions[i], 1);
        CHECK(TurboJS_IRAllocateRegister(&functions[i]) == 0);
        CHECK(TurboJS_IRAllocateRegister(&functions[i]) == 1);
        CHECK(TurboJS_IREmit(&functions[i], op(TURBOJS_IR_ARGUMENT, 0, 0, 0)) == TURBOJS_IR_OK);
        CHECK(TurboJS_IREmit(&functions[i], op(TURBOJS_IR_RUNTIME_HELPER, 1, 0, 1)) == TURBOJS_IR_OK);
        CHECK(TurboJS_IREmit(&functions[i], op(TURBOJS_IR_RETURN_I64, 0, 1, 0)) == TURBOJS_IR_OK);
        CHECK(TurboJS_BaselineCompile(&functions[i], &natives[i], &diagnostic) == TURBOJS_IR_OK);
        result = 0;
        CHECK(TurboJS_NativeInvokeWithRuntime(natives[i], &functions[i], &helpers,
                                               &argument, 1, &result) == TURBOJS_IR_OK);
        CHECK(result == 11);
    }
    CHECK(helpers.native_continuation_cache_misses == FUNCTION_COUNT);
    CHECK(helpers.native_continuation_cache_evictions == 1);

    result = 0;
    CHECK(TurboJS_NativeInvokeWithRuntime(natives[0], &functions[0], &helpers,
                                           &argument, 1, &result) == TURBOJS_IR_OK);
    CHECK(result == 11);
    CHECK(helpers.native_continuation_cache_misses == FUNCTION_COUNT + 1);
    CHECK(helpers.native_continuation_cache_evictions == 2);

    CHECK(TurboJS_CodeCacheGetStats(vault).continuation_entry_count == 16);
    TurboJS_RuntimeHelperTableDestroy(&helpers);
    TurboJS_CodeCacheDestroy(vault);
    for (int i = 0; i < FUNCTION_COUNT; ++i) {
        TurboJS_NativeFunctionDestroy(natives[i]);
        TurboJS_IRFunctionDestroy(&functions[i]);
    }
    if (failures) return 1;
    puts("runtime continuation cache LRU passed");
    return 0;
}
