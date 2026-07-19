#include <stdio.h>
#include <stdint.h>
#include "jit.h"
typedef enum TurboJSEngineOpcode {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) OP_ ## id,
#define def(id, size, n_pop, n_push, f)
#include "internal/bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
} TurboJSEngineOpcode;

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

int main(void)
{
    /* Equivalent optimized engine bytecode for: (a, b) => (a + b) * 3 */
    const uint8_t code[] = {
        OP_get_arg0,
        OP_get_arg1,
        OP_add,
        OP_push_i32, 3, 0, 0, 0,
        OP_mul,
        OP_return
    };
    TurboJSEngineBytecodeInfo input = { code, sizeof(code), 2, 0, 3, TURBOJS_ENGINE_NUMERIC_INT32 };
    TurboJSIRFunction ir;
    TurboJSIRDiagnostic diagnostic = {0};
    TurboJSNativeFunction *native = NULL;
    int64_t arguments[2];
    int64_t interpreted, compiled;
    int a, b;

    CHECK(TurboJS_EngineBytecodeToIR(&input, &ir, &diagnostic) == TURBOJS_IR_OK);
    CHECK(ir.instruction_count == 6);
    CHECK(TurboJS_BaselineCompile(&ir, &native, &diagnostic) == TURBOJS_IR_OK);
    for (a = -50; a <= 50; ++a) {
        for (b = -50; b <= 50; ++b) {
            arguments[0] = a;
            arguments[1] = b;
            CHECK(TurboJS_IRExecute(&ir, arguments, 2, &interpreted) == TURBOJS_IR_OK);
            CHECK(TurboJS_NativeInvoke(native, arguments, 2, &compiled) == TURBOJS_IR_OK);
            CHECK(interpreted == compiled);
            CHECK(compiled == (int64_t)(a + b) * 3);
        }
    }
    printf("engine bytecode bridge passed 10,201 differential cases; native=%zu bytes\n",
           TurboJS_NativeCodeSize(native));
    TurboJS_NativeFunctionDestroy(native);
    TurboJS_IRFunctionDestroy(&ir);
    return 0;
}
