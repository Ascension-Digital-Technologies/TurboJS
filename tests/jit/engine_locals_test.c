#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "jit.h"

typedef enum TestOpcode {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) OP_ ## id,
#define def(id, size, n_pop, n_push, f)
#include "internal/bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
} TestOpcode;

static int fail(const char *message) {
    fprintf(stderr, "engine locals test failed: %s\n", message);
    return 1;
}

int main(void) {
    /* function f(a,b) { let x = a + b; return x - 2; } */
    const uint8_t bytecode[] = {
        OP_get_arg0,
        OP_get_arg1,
        OP_add,
        OP_put_loc0,
        OP_get_loc0,
        OP_push_i32, 2, 0, 0, 0,
        OP_sub,
        OP_return
    };
    TurboJSEngineBytecodeInfo info;
    TurboJSIRFunction ir;
    TurboJSIRDiagnostic diagnostic;
    TurboJSNativeFunction *native = NULL;
    int64_t args[2];
    int64_t interpreted, compiled;
    int a, b;

    memset(&info, 0, sizeof(info));
    memset(&diagnostic, 0, sizeof(diagnostic));
    info.bytecode = bytecode;
    info.bytecode_length = sizeof(bytecode);
    info.argument_count = 2;
    info.local_count = 1;
    info.stack_size = 2;

    if (TurboJS_EngineBytecodeToIR(&info, &ir, &diagnostic) != TURBOJS_IR_OK)
        return fail(diagnostic.message ? diagnostic.message : "translation failed");
    if (ir.local_count != 1)
        return fail("local count was not preserved");
    if (TurboJS_BaselineCompile(&ir, &native, &diagnostic) != TURBOJS_IR_OK) {
        TurboJS_IRFunctionDestroy(&ir);
        return fail(diagnostic.message ? diagnostic.message : "native compile failed");
    }
    for (a = -50; a <= 50; ++a) {
        for (b = -50; b <= 50; ++b) {
            args[0] = a; args[1] = b;
            if (TurboJS_IRExecute(&ir, args, 2, &interpreted) != TURBOJS_IR_OK ||
                TurboJS_NativeInvoke(native, args, 2, &compiled) != TURBOJS_IR_OK ||
                interpreted != compiled || compiled != (int64_t)a + b - 2) {
                TurboJS_NativeFunctionDestroy(native);
                TurboJS_IRFunctionDestroy(&ir);
                return fail("interpreter/native mismatch");
            }
        }
    }
    printf("engine local bytecode passed 10,201 differential cases\n");
    printf("native code size: %zu bytes\n", TurboJS_NativeCodeSize(native));
    TurboJS_NativeFunctionDestroy(native);
    TurboJS_IRFunctionDestroy(&ir);
    return 0;
}
