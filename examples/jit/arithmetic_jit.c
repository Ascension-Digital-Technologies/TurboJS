#include <inttypes.h>
#include <stdio.h>
#include "jit.h"

#define EMIT(op, dst, lhs, rhs, imm) do { \
    TurboJSIRInstruction i = { op, dst, lhs, rhs, imm, 0 }; \
    if (TurboJS_IREmit(&function, i) != TURBOJS_IR_OK) return 1; \
} while (0)

int main(void)
{
    TurboJSIRFunction function;
    TurboJSIRDiagnostic diagnostic;
    TurboJSNativeFunction *native = NULL;
    int64_t arguments[] = { 40, 2 };
    int64_t interpreted = 0;
    int64_t compiled = 0;
    uint16_t a, b, result;

    TurboJS_IRFunctionInit(&function, 2);
    a = TurboJS_IRAllocateRegister(&function);
    b = TurboJS_IRAllocateRegister(&function);
    result = TurboJS_IRAllocateRegister(&function);
    EMIT(TURBOJS_IR_ARGUMENT, a, 0, 0, 0);
    EMIT(TURBOJS_IR_ARGUMENT, b, 0, 0, 1);
    EMIT(TURBOJS_IR_ADD_I64, result, a, b, 0);
    EMIT(TURBOJS_IR_RETURN_I64, 0, result, 0, 0);

    if (TurboJS_IRExecute(&function, arguments, 2, &interpreted) != TURBOJS_IR_OK)
        return 1;
    if (TurboJS_BaselineCompile(&function, &native, &diagnostic) == TURBOJS_IR_OK) {
        TurboJS_NativeInvoke(native, arguments, 2, &compiled);
        printf("interpreter=%" PRId64 " baseline-jit=%" PRId64 " code=%zu bytes\n",
               interpreted, compiled, TurboJS_NativeCodeSize(native));
    } else {
        printf("interpreter=%" PRId64 " baseline-jit=fallback (%s)\n",
               interpreted, diagnostic.message);
    }
    TurboJS_NativeFunctionDestroy(native);
    TurboJS_IRFunctionDestroy(&function);
    return 0;
}
