#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "jit.h"

typedef enum EngineOpcode {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) OP_##id,
#define def(id, size, n_pop, n_push, f)
#include "internal/bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
} EngineOpcode;

int main(void)
{
    uint8_t bytecode[8] = { OP_get_arg0, OP_push_const, 0, 0, 0, 0, OP_return };
    TurboJSEngineBytecodeInfo info;
    TurboJSSpoolLoweringStats stats;
    TurboJSIRFunction ir;
    TurboJSIRDiagnostic diagnostic = {0};
    TurboJSNativeFunction *native = NULL;
    int64_t argument = 7, result = 0;
    TurboJSBailoutInfo bailout;

    memset(&info, 0, sizeof(info));
    memset(&stats, 0, sizeof(stats));
    info.bytecode = bytecode;
    info.bytecode_length = 7;
    info.argument_count = 1;
    info.stack_size = 2;
    info.numeric_mode = TURBOJS_ENGINE_NUMERIC_INT32;
    info.lowering_stats = &stats;

    if (TurboJS_EngineBytecodeToIR(&info, &ir, &diagnostic) != TURBOJS_IR_OK) {
        fprintf(stderr, "lowering failed at %zu: %s\n", diagnostic.instruction_index, diagnostic.message);
        return 1;
    }
    if (stats.runtime_helper_exit_count != 1 || stats.partial_function_count != 1 ||
        stats.rejection_reason != TURBOJS_SPOOL_REJECT_NONE) {
        fprintf(stderr, "unexpected stats helper=%u partial=%u reject=%u\n",
                stats.runtime_helper_exit_count, stats.partial_function_count,
                stats.rejection_reason);
        TurboJS_IRFunctionDestroy(&ir);
        return 1;
    }
    if (TurboJS_BaselineCompile(&ir, &native, &diagnostic) != TURBOJS_IR_OK) {
        fprintf(stderr, "compile failed: %s\n", diagnostic.message);
        TurboJS_IRFunctionDestroy(&ir);
        return 1;
    }
    if (TurboJS_NativeInvoke(native, &argument, 1, &result) != TURBOJS_IR_BAILOUT) {
        fprintf(stderr, "expected runtime-helper bailout\n");
        TurboJS_NativeFunctionDestroy(native);
        TurboJS_IRFunctionDestroy(&ir);
        return 1;
    }
    bailout = TurboJS_NativeLastBailout(native);
    if (bailout.reason != TURBOJS_BAILOUT_RUNTIME_HELPER ||
        bailout.bytecode_offset != 1) {
        fprintf(stderr, "bad bailout reason=%d offset=%u\n",
                (int)bailout.reason, bailout.bytecode_offset);
        TurboJS_NativeFunctionDestroy(native);
        TurboJS_IRFunctionDestroy(&ir);
        return 1;
    }
    printf("partial Spool prefix exits to Rewind at bytecode %u\n", bailout.bytecode_offset);
    TurboJS_NativeFunctionDestroy(native);
    TurboJS_IRFunctionDestroy(&ir);
    return 0;
}
