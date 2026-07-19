#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "jit.h"

typedef enum TurboJSEngineOpcode {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) OP_##id,
#define def(id, size, n_pop, n_push, f)
#include "internal/bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
    OP_COUNT
} TurboJSEngineOpcode;

static const uint8_t opcode_size[] = {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) [OP_##id] = size,
#define def(id, size, n_pop, n_push, f)
#include "internal/bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
};
static int16_t i16(const uint8_t *p) { return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static int32_t i32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)); }
static TurboJSIRStatus fail(TurboJSIRDiagnostic *d, TurboJSIRStatus s, size_t i, const char *m) {
    if (d) { d->status=s; d->instruction_index=i; d->message=m; }
    return s;
}
static int branch_info(uint8_t op, const uint8_t *operand, int64_t *disp, int *conditional) {
    *conditional=0;
    switch(op) {
    case OP_goto: *disp=i32(operand); return 1;
    case OP_goto16: *disp=i16(operand); return 1;
    case OP_goto8: *disp=(int8_t)operand[0]; return 1;
    case OP_if_true: case OP_if_false: *disp=i32(operand); *conditional=1; return 1;
    case OP_if_true8: case OP_if_false8: *disp=(int8_t)operand[0]; *conditional=1; return 1;
    default: return 0;
    }
}
static int direct_opcode(uint8_t op) {
    switch(op) {
    case OP_nop: case OP_push_i32: case OP_undefined: case OP_null:
    case OP_push_false: case OP_push_true: case OP_drop: case OP_dup:
    case OP_get_arg: case OP_get_arg0: case OP_get_arg1: case OP_get_arg2: case OP_get_arg3:
    case OP_get_loc: case OP_put_loc: case OP_set_loc:
    case OP_get_loc8: case OP_put_loc8: case OP_set_loc8:
    case OP_get_loc0: case OP_get_loc1: case OP_get_loc2: case OP_get_loc3:
    case OP_put_loc0: case OP_put_loc1: case OP_put_loc2: case OP_put_loc3:
    case OP_set_loc0: case OP_set_loc1: case OP_set_loc2: case OP_set_loc3:
    case OP_add: case OP_sub: case OP_mul: case OP_div: case OP_mod:
    case OP_lt: case OP_lte: case OP_gt: case OP_gte: case OP_eq:
    case OP_post_inc: case OP_post_dec:
    case OP_goto: case OP_goto16: case OP_goto8:
    case OP_if_true: case OP_if_false: case OP_if_true8: case OP_if_false8:
    case OP_return: return 1;
    default: return 0;
    }
}
static void classify(uint8_t op, TurboJSBytecodeAnalysis *a) {
    if (direct_opcode(op)) { a->direct_instruction_count++; return; }
    a->helper_instruction_count++;
    switch(op) {
    case OP_call: case OP_call_method: case OP_tail_call: case OP_tail_call_method:
    case OP_call_constructor: case OP_call0: case OP_call1: case OP_call2: case OP_call3:
        a->has_calls=1; break;
    default: break;
    }
    switch(op) {
    case OP_get_field: case OP_get_field2: case OP_put_field: case OP_get_var:
    case OP_put_var: case OP_set_name: a->has_property_operations=1; break;
    default: break;
    }
    switch(op) {
    case OP_get_array_el: case OP_get_array_el2: case OP_put_array_el:
        a->has_indexed_operations=1; break;
    default: break;
    }
    switch(op) {
    case OP_throw: case OP_throw_error: case OP_catch: case OP_gosub: case OP_ret:
        a->has_exceptional_operations=1; break;
    default: break;
    }
}
TurboJSIRStatus TurboJS_EngineBytecodeAnalyze(const TurboJSEngineBytecodeInfo *bc,
                                               TurboJSBytecodeAnalysis *a,
                                               TurboJSIRDiagnostic *d) {
    TurboJSBytecodeCFG cfg;
    TurboJSIRStatus status;
    size_t pc = 0;
    if (!bc || !a || (!bc->bytecode && bc->bytecode_length))
        return fail(d, TURBOJS_IR_INVALID_ARGUMENT, 0, "invalid bytecode analysis input");
    memset(a, 0, sizeof(*a));
    memset(&cfg, 0, sizeof(cfg));
    status = TurboJS_EngineBytecodeBuildCFG(bc, &cfg, d);
    if (status != TURBOJS_IR_OK) {
        a->invalid_instruction_count = 1;
        return status;
    }
    a->instruction_count = (uint32_t)cfg.instruction_count;
    a->basic_block_count = (uint32_t)cfg.block_count;
    a->maximum_stack_depth = cfg.maximum_stack_depth;
    while (pc < bc->bytecode_length) {
        uint8_t op = bc->bytecode[pc];
        size_t size = opcode_size[op];
        int64_t disp = 0;
        int conditional = 0;
        classify(op, a);
        if (branch_info(op, bc->bytecode + pc + 1, &disp, &conditional)) {
            int64_t target = (int64_t)(pc + 1u) + disp;
            a->branch_count++;
            if (target <= (int64_t)pc) a->backedge_count++;
        }
        pc += size;
    }
    TurboJS_EngineBytecodeCFGDestroy(&cfg);
    if (d) { d->status = TURBOJS_IR_OK; d->instruction_index = 0; d->message = NULL; }
    return TURBOJS_IR_OK;
}
