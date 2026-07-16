#include <stdio.h>
#include "jit.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "check failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

static TurboJSIRInstruction ins(TurboJSIROpcode opcode, uint16_t dst,
                                uint16_t left, uint16_t right, int64_t imm,
                                uint32_t target)
{
    TurboJSIRInstruction value = { opcode, dst, left, right, imm, target };
    return value;
}

int main(void)
{
    TurboJSIRFunction function;
    TurboJSIRDiagnostic diagnostic;
    int64_t args[] = { 7, 5 };
    int64_t result = 0;
    uint16_t a, b, sum, scale, output;

    TurboJS_IRFunctionInit(&function, 2);
    a = TurboJS_IRAllocateRegister(&function);
    b = TurboJS_IRAllocateRegister(&function);
    sum = TurboJS_IRAllocateRegister(&function);
    scale = TurboJS_IRAllocateRegister(&function);
    output = TurboJS_IRAllocateRegister(&function);
    CHECK(output != TURBOJS_IR_NO_REGISTER);

    CHECK(TurboJS_IREmit(&function, ins(TURBOJS_IR_ARGUMENT, a, 0, 0, 0, 0)) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&function, ins(TURBOJS_IR_ARGUMENT, b, 0, 0, 1, 0)) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&function, ins(TURBOJS_IR_ADD_I64, sum, a, b, 0, 0)) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&function, ins(TURBOJS_IR_CONSTANT_I64, scale, 0, 0, 3, 0)) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&function, ins(TURBOJS_IR_MUL_I64, output, sum, scale, 0, 0)) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&function, ins(TURBOJS_IR_RETURN_I64, 0, output, 0, 0, 0)) == TURBOJS_IR_OK);

    CHECK(TurboJS_IRVerify(&function, &diagnostic) == TURBOJS_IR_OK);
    CHECK(TurboJS_IRExecute(&function, args, 2, &result) == TURBOJS_IR_OK);
    CHECK(result == 36);
    TurboJS_IRFunctionDestroy(&function);
    puts("IR verification and interpreter tests passed");
    return 0;
}
