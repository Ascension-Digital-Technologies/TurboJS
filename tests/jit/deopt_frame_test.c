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
    uint16_t dividend, divisor, local_value, quotient;
    int64_t args[2] = {42, 0}, result = 0;

    TurboJS_IRFunctionInit(&f, 2);
    TurboJS_IRFunctionSetLocalCount(&f, 1);
    dividend = TurboJS_IRAllocateRegister(&f);
    divisor = TurboJS_IRAllocateRegister(&f);
    local_value = TurboJS_IRAllocateRegister(&f);
    quotient = TurboJS_IRAllocateRegister(&f);
    CHECK(quotient != TURBOJS_IR_NO_REGISTER);
    CHECK(emit(&f, TURBOJS_IR_ARGUMENT, dividend, 0, 0, 0, 0));
    CHECK(emit(&f, TURBOJS_IR_ARGUMENT, divisor, 0, 0, 1, 1));
    CHECK(emit(&f, TURBOJS_IR_LOCAL_SET, 0, dividend, 0, 0, 2));
    CHECK(emit(&f, TURBOJS_IR_LOCAL_GET, local_value, 0, 0, 0, 3));
    CHECK(emit(&f, TURBOJS_IR_DIV_I32_CHECKED, quotient, local_value, divisor, 0, 88));
    CHECK(emit(&f, TURBOJS_IR_RETURN_I64, 0, quotient, 0, 0, 89));
    CHECK(TurboJS_BaselineCompile(&f, &native, &d) == TURBOJS_IR_OK);
    CHECK(TurboJS_NativeInvoke(native, args, 2, &result) == TURBOJS_IR_BAILOUT);

    frame = TurboJS_NativeLastDeoptFrame(native);
    CHECK(frame.bailout.reason == TURBOJS_BAILOUT_DIVISION_BY_ZERO);
    CHECK(frame.bailout.instruction_index == 4);
    CHECK(frame.bailout.bytecode_offset == 88);
    CHECK(frame.register_count == 4);
    CHECK(frame.local_count == 1);
    CHECK((frame.materialized_register_mask & 0x7u) == 0x7u);
    CHECK((frame.materialized_register_mask & 0x8u) == 0u);
    CHECK((frame.materialized_local_mask & 0x1u) == 0x1u);
    CHECK(frame.register_values[dividend] == 42);
    CHECK(frame.register_values[divisor] == 0);
    CHECK(frame.register_values[local_value] == 42);
    CHECK(frame.local_values[0] == 42);

    TurboJS_NativeFunctionDestroy(native);
    TurboJS_IRFunctionDestroy(&f);
    puts("native deoptimization frame reconstruction test passed");
    return 0;
}
