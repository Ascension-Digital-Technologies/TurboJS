#include <inttypes.h>
#include <stdio.h>
#include "jit.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "check failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

static TurboJSIRInstruction ins(TurboJSIROpcode opcode, uint16_t dst,
                                uint16_t left, uint16_t right, int64_t imm)
{
    TurboJSIRInstruction value = { opcode, dst, left, right, imm, 0 };
    return value;
}

int main(void)
{
    TurboJSIRFunction function;
    TurboJSIRDiagnostic diagnostic;
    TurboJSNativeFunction *native = NULL;
    uint16_t a, b, sum, factor, output;
    int64_t arguments[2];
    int64_t interpreted;
    int64_t compiled;
    int x, y;

    TurboJS_IRFunctionInit(&function, 2);
    a = TurboJS_IRAllocateRegister(&function);
    b = TurboJS_IRAllocateRegister(&function);
    sum = TurboJS_IRAllocateRegister(&function);
    factor = TurboJS_IRAllocateRegister(&function);
    output = TurboJS_IRAllocateRegister(&function);
    CHECK(TurboJS_IREmit(&function, ins(TURBOJS_IR_ARGUMENT, a, 0, 0, 0)) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&function, ins(TURBOJS_IR_ARGUMENT, b, 0, 0, 1)) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&function, ins(TURBOJS_IR_ADD_I64, sum, a, b, 0)) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&function, ins(TURBOJS_IR_CONSTANT_I64, factor, 0, 0, 11)) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&function, ins(TURBOJS_IR_MUL_I64, output, sum, factor, 0)) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&function, ins(TURBOJS_IR_RETURN_I64, 0, output, 0, 0)) == TURBOJS_IR_OK);

    if (TurboJS_BaselineCompile(&function, &native, &diagnostic) == TURBOJS_IR_UNSUPPORTED) {
        printf("baseline backend skipped: %s\n", diagnostic.message);
        TurboJS_IRFunctionDestroy(&function);
        return 0;
    }
    CHECK(native != NULL);
    for (x = -20; x <= 20; ++x) {
        for (y = -20; y <= 20; ++y) {
            arguments[0] = x;
            arguments[1] = y;
            CHECK(TurboJS_IRExecute(&function, arguments, 2, &interpreted) == TURBOJS_IR_OK);
            CHECK(TurboJS_NativeInvoke(native, arguments, 2, &compiled) == TURBOJS_IR_OK);
            if (interpreted != compiled) {
                fprintf(stderr, "mismatch x=%d y=%d interpreted=%" PRId64 " native=%" PRId64 "\n",
                        x, y, interpreted, compiled);
                return 1;
            }
        }
    }
    printf("baseline differential tests passed (%zu native bytes)\n", TurboJS_NativeCodeSize(native));
    TurboJS_NativeFunctionDestroy(native);
    TurboJS_IRFunctionDestroy(&function);
    return 0;
}
