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

static void put_i32(uint8_t *p, int32_t value)
{
    uint32_t u = (uint32_t)value;
    p[0] = (uint8_t)u;
    p[1] = (uint8_t)(u >> 8);
    p[2] = (uint8_t)(u >> 16);
    p[3] = (uint8_t)(u >> 24);
}

static size_t emit_u8(uint8_t *buffer, size_t offset, uint8_t value)
{
    buffer[offset++] = value;
    return offset;
}

static size_t emit_i32(uint8_t *buffer, size_t offset, int32_t value)
{
    put_i32(buffer + offset, value);
    return offset + 4u;
}

int main(void)
{
    uint8_t bytecode[96];
    size_t pc = 0;
    size_t false_operand;
    size_t join_operand;
    size_t false_target;
    size_t join_target;
    TurboJSEngineBytecodeInfo info;
    TurboJSIRFunction ir;
    TurboJSIRDiagnostic diagnostic = {0};
    TurboJSNativeFunction *native = NULL;
    TurboJSSpoolLoweringStats stats;

    /* Keep 10 live below the condition. Each predecessor changes it and the
     * join returns the merged stack value. This used to be rejected because
     * branch boundaries required an empty operand stack. */
    pc = emit_u8(bytecode, pc, OP_get_arg0);
    pc = emit_u8(bytecode, pc, OP_put_loc0);
    pc = emit_u8(bytecode, pc, OP_push_i32);
    pc = emit_i32(bytecode, pc, 10);
    pc = emit_u8(bytecode, pc, OP_get_loc0);
    pc = emit_u8(bytecode, pc, OP_if_false);
    false_operand = pc;
    pc = emit_i32(bytecode, pc, 0);

    pc = emit_u8(bytecode, pc, OP_push_i32);
    pc = emit_i32(bytecode, pc, 5);
    pc = emit_u8(bytecode, pc, OP_add);
    pc = emit_u8(bytecode, pc, OP_goto);
    join_operand = pc;
    pc = emit_i32(bytecode, pc, 0);

    false_target = pc;
    pc = emit_u8(bytecode, pc, OP_push_i32);
    pc = emit_i32(bytecode, pc, 7);
    pc = emit_u8(bytecode, pc, OP_add);

    join_target = pc;
    pc = emit_u8(bytecode, pc, OP_return);

    put_i32(bytecode + false_operand,
            (int32_t)false_target - (int32_t)false_operand);
    put_i32(bytecode + join_operand,
            (int32_t)join_target - (int32_t)join_operand);

    memset(&info, 0, sizeof(info));
    info.bytecode = bytecode;
    info.bytecode_length = pc;
    info.argument_count = 1;
    info.local_count = 1;
    info.stack_size = 3;
    info.numeric_mode = TURBOJS_ENGINE_NUMERIC_INT32;
    info.lowering_stats = &stats;

    if (TurboJS_EngineBytecodeToIR(&info, &ir, &diagnostic) != TURBOJS_IR_OK) {
        fprintf(stderr, "frontend failed at %zu: %s\n",
                diagnostic.instruction_index, diagnostic.message);
        return 1;
    }
    if (TurboJS_BaselineCompile(&ir, &native, &diagnostic) != TURBOJS_IR_OK) {
        fprintf(stderr, "compile failed at %zu: %s\n",
                diagnostic.instruction_index, diagnostic.message);
        TurboJS_IRFunctionDestroy(&ir);
        return 1;
    }

    for (int condition = 0; condition <= 1; ++condition) {
        int64_t arguments[1] = { condition };
        int64_t interpreted = 0;
        int64_t compiled = 0;
        int64_t expected = condition ? 15 : 17;
        if (TurboJS_IRExecute(&ir, arguments, 1, &interpreted) != TURBOJS_IR_OK ||
            TurboJS_NativeInvoke(native, arguments, 1, &compiled) != TURBOJS_IR_OK ||
            interpreted != expected || compiled != expected) {
            fprintf(stderr,
                    "merge mismatch condition=%d expected=%lld ir=%lld native=%lld\n",
                    condition, (long long)expected, (long long)interpreted,
                    (long long)compiled);
            TurboJS_NativeFunctionDestroy(native);
            TurboJS_IRFunctionDestroy(&ir);
            return 1;
        }
    }

    if (stats.nonempty_stack_merge_count < 2 || stats.stack_spill_store_count < 3 ||
        stats.stack_reload_count < 2 || stats.rejection_reason != TURBOJS_SPOOL_REJECT_NONE) {
        fprintf(stderr, "unexpected lowering stats: merges=%u spills=%u reloads=%u reject=%u\n",
                stats.nonempty_stack_merge_count, stats.stack_spill_store_count,
                stats.stack_reload_count, stats.rejection_reason);
        TurboJS_NativeFunctionDestroy(native);
        TurboJS_IRFunctionDestroy(&ir);
        return 1;
    }
    printf("non-empty stack merge passed for both control-flow predecessors\n");
    printf("merges=%u spills=%u reloads=%u max-stack=%u\n",
           stats.nonempty_stack_merge_count, stats.stack_spill_store_count,
           stats.stack_reload_count, stats.maximum_stack_depth);
    printf("IR instructions: %zu, native code: %zu bytes\n",
           ir.instruction_count, TurboJS_NativeCodeSize(native));
    TurboJS_NativeFunctionDestroy(native);
    TurboJS_IRFunctionDestroy(&ir);
    return 0;
}
