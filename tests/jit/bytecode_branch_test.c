#include <inttypes.h>
#include <stdio.h>
#include "jit.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

/* sum 0..n-1 using a loop. Registers: n, i, sum, one, cond. */
int main(void)
{
    static const TurboJSBytecodeInstruction code[] = {
        { TURBOJS_BC_ARGUMENT, 0, 0, 0, 0, 0 },
        { TURBOJS_BC_CONSTANT_I64, 1, 0, 0, 0, 0 },
        { TURBOJS_BC_CONSTANT_I64, 2, 0, 0, 0, 0 },
        { TURBOJS_BC_CONSTANT_I64, 3, 0, 0, 1, 0 },
        { TURBOJS_BC_LESS_THAN_I64, 4, 1, 0, 0, 0 },
        { TURBOJS_BC_BRANCH_TRUE, 0, 4, 0, 0, 7 },
        { TURBOJS_BC_RETURN_I64, 0, 2, 0, 0, 0 },
        { TURBOJS_BC_ADD_I64, 2, 2, 1, 0, 0 },
        { TURBOJS_BC_ADD_I64, 1, 1, 3, 0, 0 },
        { TURBOJS_BC_JUMP, 0, 0, 0, 0, 4 }
    };
    TurboJSBytecodeFunction bc = { code, sizeof(code)/sizeof(code[0]), 5, 1 };
    TurboJSIRFunction ir;
    TurboJSNativeFunction *native = NULL;
    TurboJSIRDiagnostic diagnostic;
    int64_t args[1], interpreted, compiled;
    int64_t n;
    CHECK(TurboJS_BytecodeToIR(&bc, &ir, &diagnostic) == TURBOJS_IR_OK);
    CHECK(TurboJS_BaselineCompile(&ir, &native, &diagnostic) == TURBOJS_IR_OK);
    for (n = 0; n <= 500; ++n) {
        args[0] = n;
        CHECK(TurboJS_IRExecute(&ir, args, 1, &interpreted) == TURBOJS_IR_OK);
        CHECK(TurboJS_NativeInvoke(native, args, 1, &compiled) == TURBOJS_IR_OK);
        CHECK(interpreted == compiled);
        CHECK(compiled == (n * (n - 1)) / 2);
    }
    printf("bytecode frontend and native branch tests passed; code=%zu bytes\n",
           TurboJS_NativeCodeSize(native));
    TurboJS_NativeFunctionDestroy(native);
    TurboJS_IRFunctionDestroy(&ir);
    return 0;
}
