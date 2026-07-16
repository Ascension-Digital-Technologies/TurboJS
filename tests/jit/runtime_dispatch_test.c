#include <stdio.h>
#include <string.h>
#include "jit.h"

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)

typedef struct Context { int calls; int throws; } Context;

static TurboJSIRStatus add_helper(void *opaque,
                                  const TurboJSBoxedDeoptFrame *frame,
                                  const TurboJSIRInstruction *instruction,
                                  TurboJSBoxedValue *result)
{
    Context *ctx = (Context *)opaque;
    ctx->calls++;
    CHECK(instruction->immediate == 9);
    CHECK(frame->bailout.bytecode_offset == 91);
    if (ctx->throws)
        return TURBOJS_IR_EXCEPTION;
    result->tag = TURBOJS_BOXED_INT64;
    result->as.integer = frame->registers[instruction->left].as.integer +
                         frame->registers[instruction->right].as.integer;
    return TURBOJS_IR_OK;
}

int main(void)
{
    TurboJSIRFunction fn;
    TurboJSNativeFunction *native = NULL;
    TurboJSIRDiagnostic diagnostic;
    TurboJSIRInstruction in;
    TurboJSRuntimeHelperTable helpers;
    Context ctx = {0, 0};
    int64_t args[2] = {20, 22};
    int64_t result = 0;

    TurboJS_IRFunctionInit(&fn, 2);
    CHECK(TurboJS_IRAllocateRegister(&fn) == 0);
    CHECK(TurboJS_IRAllocateRegister(&fn) == 1);
    CHECK(TurboJS_IRAllocateRegister(&fn) == 2);
    CHECK(TurboJS_IRAllocateRegister(&fn) == 3);

    memset(&in, 0, sizeof(in));
    in.opcode = TURBOJS_IR_ARGUMENT; in.destination = 0; in.immediate = 0;
    CHECK(TurboJS_IREmit(&fn, in) == TURBOJS_IR_OK);
    in.destination = 1; in.immediate = 1;
    CHECK(TurboJS_IREmit(&fn, in) == TURBOJS_IR_OK);
    memset(&in, 0, sizeof(in));
    in.opcode = TURBOJS_IR_RUNTIME_HELPER; in.destination = 2; in.left = 0; in.right = 1; in.immediate = 9; in.bytecode_offset = 91;
    CHECK(TurboJS_IREmit(&fn, in) == TURBOJS_IR_OK);
    memset(&in, 0, sizeof(in));
    in.opcode = TURBOJS_IR_CONSTANT_I64; in.destination = 3; in.immediate = 1;
    CHECK(TurboJS_IREmit(&fn, in) == TURBOJS_IR_OK);
    memset(&in, 0, sizeof(in));
    in.opcode = TURBOJS_IR_ADD_I64; in.destination = 2; in.left = 2; in.right = 3;
    CHECK(TurboJS_IREmit(&fn, in) == TURBOJS_IR_OK);
    memset(&in, 0, sizeof(in));
    in.opcode = TURBOJS_IR_RETURN_I64; in.left = 2;
    CHECK(TurboJS_IREmit(&fn, in) == TURBOJS_IR_OK);

    CHECK(TurboJS_BaselineCompile(&fn, &native, &diagnostic) == TURBOJS_IR_OK);
    TurboJS_RuntimeHelperTableInit(&helpers, NULL);
    CHECK(TurboJS_RuntimeHelperRegister(&helpers, 9, add_helper, &ctx) == TURBOJS_IR_OK);
    CHECK(TurboJS_NativeInvokeWithRuntime(native, &fn, &helpers, args, 2, &result) == TURBOJS_IR_OK);
    CHECK(result == 43);
    CHECK(ctx.calls == 1);
    CHECK(helpers.calls == 1);
    CHECK(helpers.exceptions == 0);

    ctx.throws = 1;
    CHECK(TurboJS_NativeInvokeWithRuntime(native, &fn, &helpers, args, 2, &result) == TURBOJS_IR_EXCEPTION);
    CHECK(helpers.exceptions == 1);

    TurboJS_RuntimeHelperUnregister(&helpers, 9);
    CHECK(TurboJS_NativeInvokeWithRuntime(native, &fn, &helpers, args, 2, &result) == TURBOJS_IR_UNSUPPORTED);
    CHECK(helpers.missing_helpers == 1);

    TurboJS_NativeFunctionDestroy(native);
    TurboJS_IRFunctionDestroy(&fn);
    if (failures) return 1;
    puts("runtime helper dispatch ABI passed");
    return 0;
}
