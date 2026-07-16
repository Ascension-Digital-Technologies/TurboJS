#include <stdio.h>
#include <string.h>
#include "jit.h"

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)

typedef struct HelperContext { int throw_exception; int calls; } HelperContext;

static TurboJSIRStatus helper(void *opaque, const TurboJSBoxedDeoptFrame *frame,
                              const TurboJSIRInstruction *instruction,
                              TurboJSBoxedValue *result)
{
    HelperContext *ctx = (HelperContext *)opaque;
    ctx->calls++;
    CHECK(frame->bailout.reason == TURBOJS_BAILOUT_RUNTIME_HELPER);
    CHECK(instruction->opcode == TURBOJS_IR_RUNTIME_HELPER);
    CHECK(instruction->immediate == 7);
    if (ctx->throw_exception)
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
    TurboJSDeoptFrame frame;
    HelperContext ctx = {0, 0};
    int64_t args[2] = {40, 2};
    int64_t result = 0;
    size_t i;
    int saw_call_map = 0;

    TurboJS_IRFunctionInit(&fn, 2);
    CHECK(TurboJS_IRAllocateRegister(&fn) == 0);
    CHECK(TurboJS_IRAllocateRegister(&fn) == 1);
    CHECK(TurboJS_IRAllocateRegister(&fn) == 2);
    memset(&in, 0, sizeof(in));
    in.opcode = TURBOJS_IR_ARGUMENT; in.destination = 0; in.immediate = 0;
    CHECK(TurboJS_IREmit(&fn, in) == TURBOJS_IR_OK);
    in.destination = 1; in.immediate = 1;
    CHECK(TurboJS_IREmit(&fn, in) == TURBOJS_IR_OK);
    memset(&in, 0, sizeof(in));
    in.opcode = TURBOJS_IR_RUNTIME_HELPER; in.destination = 2; in.left = 0; in.right = 1; in.immediate = 7; in.bytecode_offset = 55;
    CHECK(TurboJS_IREmit(&fn, in) == TURBOJS_IR_OK);
    memset(&in, 0, sizeof(in));
    in.opcode = TURBOJS_IR_RETURN_I64; in.left = 2;
    CHECK(TurboJS_IREmit(&fn, in) == TURBOJS_IR_OK);

    CHECK(TurboJS_BaselineCompile(&fn, &native, &diagnostic) == TURBOJS_IR_OK);
    CHECK(TurboJS_NativeInvoke(native, args, 2, &result) == TURBOJS_IR_BAILOUT);
    frame = TurboJS_NativeLastDeoptFrame(native);
    CHECK(frame.bailout.reason == TURBOJS_BAILOUT_RUNTIME_HELPER);
    CHECK(frame.bailout.instruction_index == 2);
    CHECK(frame.bailout.bytecode_offset == 55);

    for (i = 0; i < TurboJS_NativeStackMapCount(native); ++i) {
        const TurboJSStackMap *map = TurboJS_NativeStackMapAt(native, i);
        if (map && map->kind == TURBOJS_SAFEPOINT_RUNTIME_CALL && map->instruction_index == 2)
            saw_call_map = 1;
    }
    CHECK(saw_call_map);
    CHECK(TurboJS_IRResumeWithSlowPath(&fn, &frame, helper, &ctx, &result) == TURBOJS_IR_OK);
    CHECK(result == 42);
    CHECK(ctx.calls == 1);

    ctx.throw_exception = 1;
    CHECK(TurboJS_IRResumeWithSlowPath(&fn, &frame, helper, &ctx, &result) == TURBOJS_IR_EXCEPTION);
    CHECK(ctx.calls == 2);

    TurboJS_NativeFunctionDestroy(native);
    TurboJS_IRFunctionDestroy(&fn);
    if (failures) return 1;
    puts("runtime helper exits and exception propagation passed");
    return 0;
}
