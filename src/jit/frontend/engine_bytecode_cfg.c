#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "jit.h"

typedef enum TurboJSEngineCFGOpcode {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) OP_##id,
#define def(id, size, n_pop, n_push, f)
#include "internal/bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
    OP_COUNT
} TurboJSEngineCFGOpcode;

static const uint8_t cfg_opcode_size[] = {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) [OP_##id] = size,
#define def(id, size, n_pop, n_push, f)
#include "internal/bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
};
static const int8_t cfg_opcode_pop[] = {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) [OP_##id] = n_pop,
#define def(id, size, n_pop, n_push, f)
#include "internal/bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
};
static const int8_t cfg_opcode_push[] = {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) [OP_##id] = n_push,
#define def(id, size, n_pop, n_push, f)
#include "internal/bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
};

static int16_t cfg_i16(const uint8_t *p) {
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static int32_t cfg_i32(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}
static TurboJSIRStatus cfg_fail(TurboJSIRDiagnostic *d, TurboJSIRStatus s,
                                size_t i, const char *m) {
    if (d) { d->status = s; d->instruction_index = i; d->message = m; }
    return s;
}
static int cfg_branch(uint8_t op, const uint8_t *operand,
                      int64_t *disp, int *conditional) {
    *conditional = 0;
    switch (op) {
    case OP_goto: *disp = cfg_i32(operand); return 1;
    case OP_goto16: *disp = cfg_i16(operand); return 1;
    case OP_goto8: *disp = (int8_t)operand[0]; return 1;
    case OP_if_true: case OP_if_false:
        *disp = cfg_i32(operand); *conditional = 1; return 1;
    case OP_if_true8: case OP_if_false8:
        *disp = (int8_t)operand[0]; *conditional = 1; return 1;
    default: return 0;
    }
}
static int cfg_terminates(uint8_t op) {
    switch (op) {
    case OP_return: case OP_return_undef: case OP_return_async:
    case OP_throw: case OP_throw_error: case OP_ret:
        return 1;
    default: return 0;
    }
}
static int cfg_helper(uint8_t op) {
    switch (op) {
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
    case OP_return: return 0;
    default: return 1;
    }
}

void TurboJS_EngineBytecodeCFGDestroy(TurboJSBytecodeCFG *cfg) {
    if (!cfg) return;
    free(cfg->blocks);
    free(cfg->instruction_offsets);
    free(cfg->offset_to_block);
    memset(cfg, 0, sizeof(*cfg));
}

TurboJSIRStatus TurboJS_EngineBytecodeBuildCFG(
    const TurboJSEngineBytecodeInfo *bc, TurboJSBytecodeCFG *cfg,
    TurboJSIRDiagnostic *d) {
    uint8_t *starts = NULL;
    uint8_t *opcodes = NULL;
    uint8_t *sizes = NULL;
    uint32_t *offset_to_instruction = NULL;
    uint32_t *work = NULL;
    size_t pc = 0, instruction_count = 0, block_count = 0, wi = 0, wn = 0;
    TurboJSIRStatus status = TURBOJS_IR_OK;
    if (!bc || !cfg || (!bc->bytecode && bc->bytecode_length))
        return cfg_fail(d, TURBOJS_IR_INVALID_ARGUMENT, 0, "invalid CFG input");
    memset(cfg, 0, sizeof(*cfg));
    if (!bc->bytecode_length)
        return cfg_fail(d, TURBOJS_IR_INVALID_ARGUMENT, 0, "empty engine bytecode");
    starts = (uint8_t *)calloc(bc->bytecode_length + 1u, 1u);
    opcodes = (uint8_t *)malloc(bc->bytecode_length);
    sizes = (uint8_t *)malloc(bc->bytecode_length);
    offset_to_instruction = (uint32_t *)malloc((bc->bytecode_length + 1u) * sizeof(uint32_t));
    if (!starts || !opcodes || !sizes || !offset_to_instruction) {
        status = cfg_fail(d, TURBOJS_IR_OUT_OF_MEMORY, 0, "unable to allocate CFG parser state");
        goto done;
    }
    for (pc = 0; pc <= bc->bytecode_length; ++pc)
        offset_to_instruction[pc] = UINT32_MAX;
    starts[0] = 1;
    pc = 0;
    while (pc < bc->bytecode_length) {
        uint8_t op = bc->bytecode[pc];
        size_t size;
        int64_t disp = 0;
        int conditional = 0;
        if (op >= OP_COUNT || op == OP_invalid || !(size = cfg_opcode_size[op])) {
            status = cfg_fail(d, TURBOJS_IR_INVALID_OPCODE, pc, "invalid engine opcode in CFG");
            goto done;
        }
        if (pc + size > bc->bytecode_length) {
            status = cfg_fail(d, TURBOJS_IR_INVALID_OPCODE, pc, "truncated engine instruction in CFG");
            goto done;
        }
        offset_to_instruction[pc] = (uint32_t)instruction_count;
        opcodes[instruction_count] = op;
        sizes[instruction_count] = (uint8_t)size;
        instruction_count++;
        if (cfg_branch(op, bc->bytecode + pc + 1, &disp, &conditional)) {
            int64_t target = (int64_t)(pc + 1u) + disp;
            if (target < 0 || (uint64_t)target >= bc->bytecode_length) {
                status = cfg_fail(d, TURBOJS_IR_INVALID_TARGET, pc, "CFG branch target is out of range");
                goto done;
            }
            starts[(size_t)target] = 1;
            if (conditional && pc + size < bc->bytecode_length)
                starts[pc + size] = 1;
        } else if (cfg_terminates(op)) {
            if (pc + size < bc->bytecode_length) starts[pc + size] = 1;
        }
        pc += size;
    }
    cfg->instruction_offsets = (uint32_t *)malloc(instruction_count * sizeof(uint32_t));
    cfg->offset_to_block = (uint32_t *)malloc((bc->bytecode_length + 1u) * sizeof(uint32_t));
    if (!cfg->instruction_offsets || !cfg->offset_to_block) {
        status = cfg_fail(d, TURBOJS_IR_OUT_OF_MEMORY, 0, "unable to allocate CFG maps");
        goto done;
    }
    for (pc = 0; pc <= bc->bytecode_length; ++pc)
        cfg->offset_to_block[pc] = TURBOJS_BYTECODE_NO_BLOCK;
    pc = 0;
    while (pc < bc->bytecode_length) {
        uint32_t ii = offset_to_instruction[pc];
        cfg->instruction_offsets[ii] = (uint32_t)pc;
        if (starts[pc]) block_count++;
        pc += sizes[ii];
    }
    cfg->blocks = (TurboJSBytecodeBlock *)calloc(block_count, sizeof(*cfg->blocks));
    work = (uint32_t *)malloc(block_count * sizeof(uint32_t));
    if (!cfg->blocks || !work) {
        status = cfg_fail(d, TURBOJS_IR_OUT_OF_MEMORY, 0, "unable to allocate CFG blocks");
        goto done;
    }
    {
        size_t bi = 0, ii = 0;
        pc = 0;
        while (pc < bc->bytecode_length) {
            if (starts[pc]) {
                TurboJSBytecodeBlock *b = &cfg->blocks[bi++];
                b->start_offset = (uint32_t)pc;
                b->first_instruction = (uint32_t)ii;
                b->entry_stack_depth = UINT32_MAX;
                b->exit_stack_depth = UINT32_MAX;
                { uint32_t pi;
                  b->successors[0] = b->successors[1] = TURBOJS_BYTECODE_NO_BLOCK;
                  for (pi = 0; pi < TURBOJS_BYTECODE_MAX_BLOCK_EDGES; ++pi)
                      b->predecessors[pi] = TURBOJS_BYTECODE_NO_BLOCK;
                }
            }
            cfg->offset_to_block[pc] = (uint32_t)(bi - 1u);
            pc += sizes[ii++];
        }
        for (bi = 0; bi < block_count; ++bi) {
            TurboJSBytecodeBlock *b = &cfg->blocks[bi];
            b->end_offset = bi + 1u < block_count ? cfg->blocks[bi + 1u].start_offset : (uint32_t)bc->bytecode_length;
            b->instruction_count = (uint32_t)((bi + 1u < block_count ? cfg->blocks[bi + 1u].first_instruction : instruction_count) - b->first_instruction);
        }
    }
    {
        size_t bi;
        for (bi = 0; bi < block_count; ++bi) {
            TurboJSBytecodeBlock *b = &cfg->blocks[bi];
            uint32_t li = b->first_instruction + b->instruction_count - 1u;
            uint32_t off = cfg->instruction_offsets[li];
            uint8_t op = opcodes[li];
            uint8_t size = sizes[li];
            int64_t disp = 0;
            int conditional = 0;
            uint32_t j;
            for (j = 0; j < b->instruction_count; ++j)
                if (cfg_helper(opcodes[b->first_instruction + j])) b->flags |= TURBOJS_BYTECODE_BLOCK_HELPER_EXIT;
            if (cfg_branch(op, bc->bytecode + off + 1, &disp, &conditional)) {
                uint32_t target = (uint32_t)((int64_t)(off + 1u) + disp);
                uint32_t tb;
                if (offset_to_instruction[target] == UINT32_MAX) {
                    status = cfg_fail(d, TURBOJS_IR_INVALID_TARGET, off, "CFG branch target is not an instruction boundary");
                    goto done;
                }
                tb = cfg->offset_to_block[target];
                if (tb == TURBOJS_BYTECODE_NO_BLOCK) {
                    status = cfg_fail(d, TURBOJS_IR_INVALID_TARGET, off, "CFG branch target has no basic block");
                    goto done;
                }
                b->successors[b->successor_count++] = tb;
                if (target <= off) {
                    b->flags |= TURBOJS_BYTECODE_BLOCK_HAS_BACKEDGE;
                    cfg->blocks[tb].flags |= TURBOJS_BYTECODE_BLOCK_LOOP_HEADER;
                }
                if (conditional && off + size < bc->bytecode_length) {
                    uint32_t fb = cfg->offset_to_block[off + size];
                    if (fb != tb) b->successors[b->successor_count++] = fb;
                }
            } else if (!cfg_terminates(op) && b->end_offset < bc->bytecode_length) {
                b->successors[b->successor_count++] = (uint32_t)(bi + 1u);
            }
        }
    }
    {
        size_t bi;
        for (bi = 0; bi < block_count; ++bi) {
            TurboJSBytecodeBlock *b = &cfg->blocks[bi];
            uint32_t si;
            for (si = 0; si < b->successor_count; ++si) {
                TurboJSBytecodeBlock *succ = &cfg->blocks[b->successors[si]];
                if (succ->predecessor_count >= TURBOJS_BYTECODE_MAX_BLOCK_EDGES) {
                    status = cfg_fail(d, TURBOJS_IR_UNSUPPORTED, succ->start_offset,
                                      "CFG predecessor limit exceeded");
                    goto done;
                }
                succ->predecessors[succ->predecessor_count++] = (uint32_t)bi;
            }
        }
    }
    cfg->blocks[0].entry_stack_depth = 0;
    work[wn++] = 0;
    while (wi < wn) {
        uint32_t bi = work[wi++];
        TurboJSBytecodeBlock *b = &cfg->blocks[bi];
        uint32_t depth = b->entry_stack_depth;
        uint32_t j;
        b->flags |= TURBOJS_BYTECODE_BLOCK_REACHABLE;
        for (j = 0; j < b->instruction_count; ++j) {
            uint32_t ii = b->first_instruction + j;
            int pop = cfg_opcode_pop[opcodes[ii]], push = cfg_opcode_push[opcodes[ii]];
            if (pop >= 0) {
                if (depth < (uint32_t)pop) {
                    status = cfg_fail(d, TURBOJS_IR_INVALID_OPCODE, cfg->instruction_offsets[ii], "CFG stack underflow");
                    goto done;
                }
                depth = depth - (uint32_t)pop + (uint32_t)push;
                if (depth > cfg->maximum_stack_depth) cfg->maximum_stack_depth = depth;
                if (bc->stack_size && depth > bc->stack_size) {
                    status = cfg_fail(d, TURBOJS_IR_INVALID_REGISTER, cfg->instruction_offsets[ii], "CFG stack exceeds declared stack size");
                    goto done;
                }
            }
        }
        b->exit_stack_depth = depth;
        for (j = 0; j < b->successor_count; ++j) {
            TurboJSBytecodeBlock *s = &cfg->blocks[b->successors[j]];
            if (s->entry_stack_depth == UINT32_MAX) {
                s->entry_stack_depth = depth;
                work[wn++] = b->successors[j];
            } else if (s->entry_stack_depth != depth) {
                status = cfg_fail(d, TURBOJS_IR_INVALID_OPCODE, s->start_offset, "inconsistent operand stack depth at CFG merge");
                goto done;
            }
        }
    }
    cfg->block_count = block_count;
    cfg->instruction_count = instruction_count;
    if (d) { d->status = TURBOJS_IR_OK; d->instruction_index = 0; d->message = NULL; }

done:
    free(starts);
    free(opcodes);
    free(sizes);
    free(offset_to_instruction);
    free(work);
    if (status != TURBOJS_IR_OK) TurboJS_EngineBytecodeCFGDestroy(cfg);
    return status;
}
