#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "jit.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static int build(TurboJSIRFunction *f, TurboJSIROpcode opcode, uint32_t bytecode_offset)
{
    TurboJSIRInstruction in = {0};
    uint16_t a, b, result;
    TurboJS_IRFunctionInit(f, 2);
    a = TurboJS_IRAllocateRegister(f);
    b = TurboJS_IRAllocateRegister(f);
    result = TurboJS_IRAllocateRegister(f);
    CHECK(a != TURBOJS_IR_NO_REGISTER && b != TURBOJS_IR_NO_REGISTER && result != TURBOJS_IR_NO_REGISTER);
    in.opcode = TURBOJS_IR_ARGUMENT; in.destination = a; in.immediate = 0; in.bytecode_offset = 0;
    CHECK(TurboJS_IREmit(f, in) == TURBOJS_IR_OK);
    in.opcode = TURBOJS_IR_ARGUMENT; in.destination = b; in.immediate = 1; in.bytecode_offset = 1;
    CHECK(TurboJS_IREmit(f, in) == TURBOJS_IR_OK);
    in.opcode = opcode; in.destination = result; in.left = a; in.right = b; in.bytecode_offset = bytecode_offset;
    CHECK(TurboJS_IREmit(f, in) == TURBOJS_IR_OK);
    in.opcode = TURBOJS_IR_RETURN_I64; in.left = result; in.bytecode_offset = bytecode_offset + 1;
    CHECK(TurboJS_IREmit(f, in) == TURBOJS_IR_OK);
    return 0;
}

static int run_case(TurboJSIROpcode opcode, int64_t left, int64_t right,
                    TurboJSIRStatus expected_status, int64_t expected_value,
                    TurboJSBailoutReason expected_reason)
{
    TurboJSIRFunction f;
    TurboJSNativeFunction *native = NULL;
    TurboJSIRDiagnostic d;
    TurboJSBailoutInfo bailout;
    int64_t args[2] = {left, right};
    int64_t interpreted = 0, compiled = 0;
    TurboJSIRStatus is, ns;
    CHECK(build(&f, opcode, 77) == 0);
    CHECK(TurboJS_BaselineCompile(&f, &native, &d) == TURBOJS_IR_OK);
    is = TurboJS_IRExecute(&f, args, 2, &interpreted);
    ns = TurboJS_NativeInvoke(native, args, 2, &compiled);
    CHECK(is == expected_status);
    CHECK(ns == expected_status);
    if (expected_status == TURBOJS_IR_OK) {
        CHECK(interpreted == expected_value);
        CHECK(compiled == expected_value);
    } else {
        bailout = TurboJS_NativeLastBailout(native);
        CHECK(bailout.reason == expected_reason);
        CHECK(bailout.instruction_index == 2);
        CHECK(bailout.bytecode_offset == 77);
    }
    TurboJS_NativeFunctionDestroy(native);
    TurboJS_IRFunctionDestroy(&f);
    return 0;
}

int main(void)
{
    TurboJSIRFunction original, loaded;
    TurboJSAOTBuffer image = {0};
    TurboJSIRDiagnostic d;
    CHECK(run_case(TURBOJS_IR_DIV_I32_CHECKED, 21, 3, TURBOJS_IR_OK, 7, TURBOJS_BAILOUT_NONE) == 0);
    CHECK(run_case(TURBOJS_IR_DIV_I32_CHECKED, 21, 0, TURBOJS_IR_BAILOUT, 0, TURBOJS_BAILOUT_DIVISION_BY_ZERO) == 0);
    CHECK(run_case(TURBOJS_IR_DIV_I32_CHECKED, INT32_MIN, -1, TURBOJS_IR_BAILOUT, 0, TURBOJS_BAILOUT_DIVISION_OVERFLOW) == 0);
    CHECK(run_case(TURBOJS_IR_REM_I32_CHECKED, 22, 5, TURBOJS_IR_OK, 2, TURBOJS_BAILOUT_NONE) == 0);
    CHECK(run_case(TURBOJS_IR_REM_I32_CHECKED, INT32_MIN, -1, TURBOJS_IR_OK, 0, TURBOJS_BAILOUT_NONE) == 0);
    CHECK(run_case(TURBOJS_IR_REM_I32_CHECKED, 22, 0, TURBOJS_IR_BAILOUT, 0, TURBOJS_BAILOUT_DIVISION_BY_ZERO) == 0);

    CHECK(build(&original, TURBOJS_IR_DIV_I32_CHECKED, 1234) == 0);
    CHECK(TurboJS_AOTSerializeIR(&original, &image, &d) == TURBOJS_IR_OK);
    CHECK(TurboJS_AOTDeserializeIR(image.data, image.size, &loaded, &d) == TURBOJS_IR_OK);
    CHECK(loaded.instructions[2].bytecode_offset == 1234);
    TurboJS_IRFunctionDestroy(&loaded);
    TurboJS_AOTBufferDestroy(&image);
    TurboJS_IRFunctionDestroy(&original);
    puts("checked division, remainder, and source-position tests passed");
    return 0;
}
