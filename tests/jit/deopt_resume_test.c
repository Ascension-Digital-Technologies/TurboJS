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

int main(void)
{
    TurboJSIRFunction f;
    TurboJSNativeFunction *native = NULL;
    TurboJSIRDiagnostic d;
    TurboJSDeoptFrame frame;
    uint16_t value, divisor, loaded, quotient, combined;
    int64_t args[2] = {42, 0}, native_result = 0, resumed_result = 0;

    TurboJS_IRFunctionInit(&f, 2);
    TurboJS_IRFunctionSetLocalCount(&f, 1);
    value = TurboJS_IRAllocateRegister(&f);
    divisor = TurboJS_IRAllocateRegister(&f);
    loaded = TurboJS_IRAllocateRegister(&f);
    quotient = TurboJS_IRAllocateRegister(&f);
    combined = TurboJS_IRAllocateRegister(&f);
    CHECK(combined != TURBOJS_IR_NO_REGISTER);
    CHECK(emit(&f, TURBOJS_IR_ARGUMENT, value, 0, 0, 0, 0));
    CHECK(emit(&f, TURBOJS_IR_ARGUMENT, divisor, 0, 0, 1, 1));
    CHECK(emit(&f, TURBOJS_IR_LOCAL_SET, 0, value, 0, 0, 2));
    CHECK(emit(&f, TURBOJS_IR_LOCAL_GET, loaded, 0, 0, 0, 3));
    CHECK(emit(&f, TURBOJS_IR_DIV_I32_CHECKED, quotient, loaded, divisor, 0, 40));
    CHECK(emit(&f, TURBOJS_IR_LOCAL_GET, loaded, 0, 0, 0, 41));
    CHECK(emit(&f, TURBOJS_IR_ADD_I64, combined, quotient, loaded, 0, 42));
    CHECK(emit(&f, TURBOJS_IR_RETURN_I64, 0, combined, 0, 0, 43));

    CHECK(TurboJS_BaselineCompile(&f, &native, &d) == TURBOJS_IR_OK);
    CHECK(TurboJS_NativeInvoke(native, args, 2, &native_result) == TURBOJS_IR_BAILOUT);
    frame = TurboJS_NativeLastDeoptFrame(native);
    CHECK(frame.bailout.instruction_index == 4);
    CHECK(frame.bailout.bytecode_offset == 40);
    CHECK(frame.stack_count == 0);
    CHECK((frame.live_register_mask & ((uint64_t)1u << loaded)) != 0);
    CHECK((frame.live_register_mask & ((uint64_t)1u << divisor)) != 0);
    CHECK((frame.live_local_mask & 1u) != 0);
    CHECK(frame.register_kinds[loaded] == TURBOJS_VALUE_I64);
    CHECK(frame.local_kinds[0] == TURBOJS_VALUE_I64);

    /* Simulate the generic JavaScript slow path producing 100, then resume at
       the following IR instruction without replaying the prefix. */
    CHECK(TurboJS_IRResumeAfterBailout(&f, &frame, 100, &resumed_result) == TURBOJS_IR_OK);
    CHECK(resumed_result == 142);

    TurboJS_NativeFunctionDestroy(native);
    TurboJS_IRFunctionDestroy(&f);
    puts("control-flow liveness and exact deoptimization resume test passed");
    return 0;
}
