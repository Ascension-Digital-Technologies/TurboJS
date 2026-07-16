#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "jit.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static int emit(TurboJSIRFunction *f, TurboJSIROpcode op, uint16_t dst,
                uint16_t left, uint16_t right, int64_t imm, uint32_t bc)
{
    TurboJSIRInstruction in;
    memset(&in, 0, sizeof(in));
    in.opcode = op; in.destination = dst; in.left = left; in.right = right;
    in.immediate = imm; in.bytecode_offset = bc;
    return TurboJS_IREmit(f, in) == TURBOJS_IR_OK;
}

typedef struct SlowPathState { int calls; } SlowPathState;

static TurboJSIRStatus division_slow_path(void *opaque,
                                         const TurboJSBoxedDeoptFrame *frame,
                                         const TurboJSIRInstruction *failed,
                                         TurboJSBoxedValue *result)
{
    SlowPathState *state = opaque;
    state->calls++;
    if (frame->bailout.reason != TURBOJS_BAILOUT_DIVISION_BY_ZERO ||
        frame->bailout.bytecode_offset != 88 ||
        failed->opcode != TURBOJS_IR_DIV_I32_CHECKED)
        return TURBOJS_IR_INVALID_ARGUMENT;
    if (frame->registers[0].tag != TURBOJS_BOXED_INT64 || frame->registers[0].as.integer != 42)
        return TURBOJS_IR_INVALID_ARGUMENT;
    if (frame->locals[0].tag != TURBOJS_BOXED_INT64 || frame->locals[0].as.integer != 42)
        return TURBOJS_IR_INVALID_ARGUMENT;
    result->tag = TURBOJS_BOXED_INT64;
    result->as.integer = 100;
    return TURBOJS_IR_OK;
}

int main(void)
{
    TurboJSIRFunction f;
    TurboJSNativeFunction *native = NULL;
    TurboJSIRDiagnostic d;
    TurboJSDeoptFrame frame;
    SlowPathState state = {0};
    uint16_t value, divisor, loaded, quotient, combined;
    int64_t args[2] = {42, 0}, native_result = 0, resumed_result = 0;

    TurboJS_IRFunctionInit(&f, 2);
    TurboJS_IRFunctionSetLocalCount(&f, 1);
    value = TurboJS_IRAllocateRegister(&f);
    divisor = TurboJS_IRAllocateRegister(&f);
    loaded = TurboJS_IRAllocateRegister(&f);
    quotient = TurboJS_IRAllocateRegister(&f);
    combined = TurboJS_IRAllocateRegister(&f);
    CHECK(emit(&f, TURBOJS_IR_ARGUMENT, value, 0, 0, 0, 0));
    CHECK(emit(&f, TURBOJS_IR_ARGUMENT, divisor, 0, 0, 1, 1));
    CHECK(emit(&f, TURBOJS_IR_LOCAL_SET, 0, value, 0, 0, 2));
    CHECK(emit(&f, TURBOJS_IR_LOCAL_GET, loaded, 0, 0, 0, 3));
    CHECK(emit(&f, TURBOJS_IR_DIV_I32_CHECKED, quotient, loaded, divisor, 0, 88));
    CHECK(emit(&f, TURBOJS_IR_LOCAL_GET, loaded, 0, 0, 0, 89));
    CHECK(emit(&f, TURBOJS_IR_ADD_I64, combined, quotient, loaded, 0, 90));
    CHECK(emit(&f, TURBOJS_IR_RETURN_I64, 0, combined, 0, 0, 91));

    CHECK(TurboJS_BaselineCompile(&f, &native, &d) == TURBOJS_IR_OK);
    CHECK(TurboJS_NativeInvoke(native, args, 2, &native_result) == TURBOJS_IR_BAILOUT);
    frame = TurboJS_NativeLastDeoptFrame(native);
    CHECK(TurboJS_IRResumeWithSlowPath(&f, &frame, division_slow_path, &state, &resumed_result) == TURBOJS_IR_OK);
    CHECK(state.calls == 1);
    CHECK(resumed_result == 142);

    TurboJS_NativeFunctionDestroy(native);
    TurboJS_IRFunctionDestroy(&f);
    puts("boxed deoptimization slow-path resume test passed");
    return 0;
}
