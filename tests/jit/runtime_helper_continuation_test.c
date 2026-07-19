#include <stdio.h>
#include <string.h>
#include "jit.h"

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)

typedef struct Context { int calls7; int calls8; } Context;

static TurboJSIRStatus helper(void *opaque, const TurboJSBoxedDeoptFrame *frame,
                              const TurboJSIRInstruction *instruction,
                              TurboJSBoxedValue *result)
{
    Context *ctx = (Context *)opaque;
    CHECK(frame->bailout.reason == TURBOJS_BAILOUT_RUNTIME_HELPER);
    result->tag = TURBOJS_BOXED_INT64;
    if (instruction->immediate == 7) {
        ctx->calls7++;
        result->as.integer = frame->registers[instruction->left].as.integer +
                             frame->registers[instruction->right].as.integer;
    } else if (instruction->immediate == 8) {
        ctx->calls8++;
        result->as.integer = frame->registers[instruction->left].as.integer *
                             frame->registers[instruction->right].as.integer;
    } else {
        return TURBOJS_IR_UNSUPPORTED;
    }
    return TURBOJS_IR_OK;
}

static TurboJSIRInstruction op(TurboJSIROpcode opcode, uint16_t dst,
                               uint16_t left, uint16_t right, int64_t imm)
{
    TurboJSIRInstruction in;
    memset(&in, 0, sizeof(in));
    in.opcode = opcode; in.destination = dst; in.left = left; in.right = right;
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
    Context ctx = {0, 0};
    int64_t args[2] = {20, 2};
    int64_t result = 0;

    TurboJS_IRFunctionInit(&fn, 2);
    for (int i = 0; i < 6; ++i) CHECK(TurboJS_IRAllocateRegister(&fn) == i);
    CHECK(TurboJS_IREmit(&fn, op(TURBOJS_IR_ARGUMENT, 0, 0, 0, 0)) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&fn, op(TURBOJS_IR_ARGUMENT, 1, 0, 0, 1)) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&fn, op(TURBOJS_IR_RUNTIME_HELPER, 2, 0, 1, 7)) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&fn, op(TURBOJS_IR_CONSTANT_I64, 3, 0, 0, 1)) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&fn, op(TURBOJS_IR_ADD_I64, 4, 2, 3, 0)) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&fn, op(TURBOJS_IR_RUNTIME_HELPER, 5, 4, 1, 8)) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&fn, op(TURBOJS_IR_RETURN_I64, 0, 5, 0, 0)) == TURBOJS_IR_OK);

    CHECK(TurboJS_BaselineCompile(&fn, &native, &diagnostic) == TURBOJS_IR_OK);
    vault = TurboJS_CodeCacheCreate(32, 1024 * 1024);
    CHECK(vault != NULL);
    TurboJS_RuntimeHelperTableInit(&helpers, NULL);
    TurboJS_RuntimeHelperAttachContinuationVault(&helpers, vault);
    CHECK(TurboJS_RuntimeHelperRegister(&helpers, 7, helper, &ctx) == TURBOJS_IR_OK);
    CHECK(TurboJS_RuntimeHelperRegister(&helpers, 8, helper, &ctx) == TURBOJS_IR_OK);
    CHECK(TurboJS_NativeInvokeWithRuntime(native, &fn, &helpers, args, 2, &result) == TURBOJS_IR_OK);
    CHECK(result == 46);
    CHECK(ctx.calls7 == 1);
    CHECK(ctx.calls8 == 1);
    CHECK(helpers.calls == 2);
    CHECK(helpers.native_continuation_compiles >= 2);
    CHECK(helpers.native_continuation_entries >= 2);
    CHECK(helpers.native_continuation_cache_misses == 2);
    CHECK(helpers.native_continuation_cache_hits == 0);

    TurboJS_RuntimeHelperTableDestroy(&helpers);
    TurboJS_RuntimeHelperTableInit(&helpers, NULL);
    TurboJS_RuntimeHelperAttachContinuationVault(&helpers, vault);
    CHECK(TurboJS_RuntimeHelperRegister(&helpers, 7, helper, &ctx) == TURBOJS_IR_OK);
    CHECK(TurboJS_RuntimeHelperRegister(&helpers, 8, helper, &ctx) == TURBOJS_IR_OK);
    result = 0;
    CHECK(TurboJS_NativeInvokeWithRuntime(native, &fn, &helpers, args, 2, &result) == TURBOJS_IR_OK);
    CHECK(result == 46);
    CHECK(ctx.calls7 == 2);
    CHECK(ctx.calls8 == 2);
    CHECK(helpers.native_continuation_compiles == 0);
    CHECK(helpers.native_continuation_entries >= 2);
    CHECK(helpers.native_continuation_cache_misses == 0);
    CHECK(helpers.native_continuation_cache_hits == 2);
    CHECK(TurboJS_CodeCacheGetStats(vault).continuation_entry_count == 2);

    TurboJS_RuntimeHelperTableDestroy(&helpers);
    TurboJS_CodeCacheDestroy(vault);
    TurboJS_NativeFunctionDestroy(native);
    TurboJS_IRFunctionDestroy(&fn);
    if (failures) return 1;
    puts("multiple runtime-helper continuation passed");
    return 0;
}
