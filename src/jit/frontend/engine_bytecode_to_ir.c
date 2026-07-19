#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

static TurboJSIRStatus engine_fail(TurboJSIRDiagnostic *d,
                                   TurboJSIRStatus status,
                                   size_t offset,
                                   const char *message)
{
    if (d) {
        d->status = status;
        d->instruction_index = offset;
        d->message = message;
    }
    return status;
}

static void record_rejection(const TurboJSEngineBytecodeInfo *bc,
                             TurboJSSpoolRejectionReason reason,
                             size_t offset)
{
    if (bc && bc->lowering_stats) {
        bc->lowering_stats->rejection_reason = (uint32_t)reason;
        bc->lowering_stats->rejection_bytecode_offset = (uint32_t)offset;
    }
}

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int16_t read_i16(const uint8_t *p)
{
    return (int16_t)read_u16(p);
}

static int32_t read_i32(const uint8_t *p)
{
    uint32_t value = (uint32_t)p[0] |
                     ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) |
                     ((uint32_t)p[3] << 24);
    return (int32_t)value;
}

static TurboJSIRStatus emit(TurboJSIRFunction *out,
                            TurboJSIRInstruction instruction,
                            TurboJSIRDiagnostic *diagnostic,
                            size_t offset)
{
    TurboJSIRStatus status = TurboJS_IREmit(out, instruction);
    if (status != TURBOJS_IR_OK)
        return engine_fail(diagnostic, status, offset, "unable to append engine bytecode IR");
    return TURBOJS_IR_OK;
}

static const uint8_t engine_opcode_size[] = {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) [OP_ ## id] = (uint8_t)(size),
#define def(id, size, n_pop, n_push, f)
#include "internal/bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
};

static const int8_t engine_opcode_pop_count[] = {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) [OP_ ## id] = (int8_t)(n_pop),
#define def(id, size, n_pop, n_push, f)
#include "internal/bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
};

static const int8_t engine_opcode_push_count[] = {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) [OP_ ## id] = (int8_t)(n_push),
#define def(id, size, n_pop, n_push, f)
#include "internal/bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
};

typedef struct TurboJSEngineStackEffect {
    uint16_t pop_count;
    uint16_t push_count;
    size_t next_offset;
    size_t branch_target;
    uint8_t has_branch;
    uint8_t has_fallthrough;
} TurboJSEngineStackEffect;

static TurboJSIRStatus engine_stack_effect(const TurboJSEngineBytecodeInfo *bc,
                                            size_t offset,
                                            TurboJSEngineStackEffect *effect)
{
    uint8_t opcode;
    size_t size;
    int64_t displacement = 0;
    if (!bc || !effect || offset >= bc->bytecode_length)
        return TURBOJS_IR_INVALID_ARGUMENT;
    memset(effect, 0, sizeof(*effect));
    opcode = bc->bytecode[offset];
    if ((size_t)opcode >= sizeof(engine_opcode_size) || !(size = engine_opcode_size[opcode]) ||
        offset + size > bc->bytecode_length)
        return TURBOJS_IR_INVALID_OPCODE;
    effect->next_offset = offset + size;
    effect->has_fallthrough = 1;
    switch (opcode) {
    case OP_nop: break;
    case OP_push_i32: case OP_get_arg: case OP_get_arg0: case OP_get_arg1:
    case OP_get_arg2: case OP_get_arg3: case OP_get_loc: case OP_get_loc8:
    case OP_get_loc0: case OP_get_loc1: case OP_get_loc2: case OP_get_loc3:
    case OP_get_var: case OP_get_var_undef: case OP_get_var_ref:
    case OP_get_var_ref_check: case OP_get_var_ref0: case OP_get_var_ref1:
    case OP_get_var_ref2: case OP_get_var_ref3:
        effect->push_count = 1; break;
    case OP_put_loc: case OP_put_loc8: case OP_put_loc0: case OP_put_loc1:
    case OP_put_loc2: case OP_put_loc3:
        effect->pop_count = 1; break;
    case OP_set_loc: case OP_set_loc8: case OP_set_loc0: case OP_set_loc1:
    case OP_set_loc2: case OP_set_loc3:
        break;
    case OP_add: case OP_sub: case OP_mul: case OP_div: case OP_mod: case OP_lt:
        effect->pop_count = 2; effect->push_count = 1; break;
    case OP_if_true: case OP_if_false:
        effect->pop_count = 1; effect->has_branch = 1;
        displacement = read_i32(bc->bytecode + offset + 1u);
        effect->branch_target = (size_t)((int64_t)(offset + 1u) + displacement);
        break;
    case OP_if_true8: case OP_if_false8:
        effect->pop_count = 1; effect->has_branch = 1;
        displacement = (int8_t)bc->bytecode[offset + 1u];
        effect->branch_target = (size_t)((int64_t)(offset + 1u) + displacement);
        break;
    case OP_goto:
        effect->has_branch = 1; effect->has_fallthrough = 0;
        displacement = read_i32(bc->bytecode + offset + 1u);
        effect->branch_target = (size_t)((int64_t)(offset + 1u) + displacement);
        break;
    case OP_goto16:
        effect->has_branch = 1; effect->has_fallthrough = 0;
        displacement = read_i16(bc->bytecode + offset + 1u);
        effect->branch_target = (size_t)((int64_t)(offset + 1u) + displacement);
        break;
    case OP_goto8:
        effect->has_branch = 1; effect->has_fallthrough = 0;
        displacement = (int8_t)bc->bytecode[offset + 1u];
        effect->branch_target = (size_t)((int64_t)(offset + 1u) + displacement);
        break;
    case OP_call0: effect->pop_count = 1; effect->push_count = 1; break;
    case OP_call1: effect->pop_count = 2; effect->push_count = 1; break;
    case OP_call2: effect->pop_count = 3; effect->push_count = 1; break;
    case OP_call3: effect->pop_count = 4; effect->push_count = 1; break;
    case OP_call: {
        uint16_t argc = read_u16(bc->bytecode + offset + 1u);
        effect->pop_count = (uint16_t)(argc + 1u); effect->push_count = 1; break;
    }
    case OP_return:
        effect->pop_count = 1; effect->has_fallthrough = 0; break;
    default:
        if ((size_t)opcode >= sizeof(engine_opcode_pop_count) ||
            engine_opcode_pop_count[opcode] < 0 || engine_opcode_push_count[opcode] < 0)
            return TURBOJS_IR_UNSUPPORTED;
        effect->pop_count = (uint16_t)engine_opcode_pop_count[opcode];
        effect->push_count = (uint16_t)engine_opcode_push_count[opcode];
        break;
    }
    if (effect->has_branch && effect->branch_target >= bc->bytecode_length)
        return TURBOJS_IR_INVALID_TARGET;
    return TURBOJS_IR_OK;
}

static TurboJSIRStatus analyze_stack_depths(const TurboJSEngineBytecodeInfo *bc,
                                             int32_t *depths,
                                             uint8_t *branch_targets,
                                             uint32_t *maximum_depth,
                                             size_t *bad_offset)
{
    size_t *worklist;
    size_t work_count = 0, work_index = 0;
    TurboJSIRStatus status = TURBOJS_IR_OK;
    if (!bc || !depths || !branch_targets || !maximum_depth)
        return TURBOJS_IR_INVALID_ARGUMENT;
    worklist = (size_t *)malloc((bc->bytecode_length + 1u) * sizeof(*worklist));
    if (!worklist)
        return TURBOJS_IR_OUT_OF_MEMORY;
    for (size_t i = 0; i <= bc->bytecode_length; ++i)
        depths[i] = -1;
    depths[0] = 0;
    worklist[work_count++] = 0;
    *maximum_depth = 0;
    while (work_index < work_count) {
        size_t offset = worklist[work_index++];
        TurboJSEngineStackEffect effect;
        int32_t out_depth;
        status = engine_stack_effect(bc, offset, &effect);
        if (status != TURBOJS_IR_OK) { if (bad_offset) *bad_offset = offset; break; }
        if (depths[offset] < (int32_t)effect.pop_count) {
            status = TURBOJS_IR_INVALID_OPCODE; if (bad_offset) *bad_offset = offset; break;
        }
        out_depth = depths[offset] - (int32_t)effect.pop_count + (int32_t)effect.push_count;
        if ((uint32_t)out_depth > *maximum_depth) *maximum_depth = (uint32_t)out_depth;
#define MERGE_SUCCESSOR(successor_) do {             size_t successor = (successor_);             if (depths[successor] < 0) { depths[successor] = out_depth; worklist[work_count++] = successor; }             else if (depths[successor] != out_depth) { status = TURBOJS_IR_UNSUPPORTED; if (bad_offset) *bad_offset = successor; goto done; }         } while (0)
        if (effect.has_branch) {
            branch_targets[effect.branch_target] = 1;
            MERGE_SUCCESSOR(effect.branch_target);
        }
        if (effect.has_fallthrough && effect.next_offset < bc->bytecode_length)
            MERGE_SUCCESSOR(effect.next_offset);
#undef MERGE_SUCCESSOR
    }
done:
    free(worklist);
    return status;
}

static TurboJSIRStatus spill_stack_to_locals(TurboJSIRFunction *out,
                                              const uint16_t *stack,
                                              size_t sp,
                                              uint16_t stack_local_base,
                                              TurboJSIRDiagnostic *diagnostic,
                                              size_t offset)
{
    for (size_t i = 0; i < sp; ++i) {
        TurboJSIRInstruction store;
        memset(&store, 0, sizeof(store));
        store.opcode = TURBOJS_IR_LOCAL_SET;
        store.destination = TURBOJS_IR_NO_REGISTER;
        store.left = stack[i];
        store.right = TURBOJS_IR_NO_REGISTER;
        store.immediate = (int64_t)(stack_local_base + i);
        store.bytecode_offset = (uint32_t)offset;
        if (emit(out, store, diagnostic, offset) != TURBOJS_IR_OK)
            return diagnostic ? diagnostic->status : TURBOJS_IR_OUT_OF_MEMORY;
    }
    return TURBOJS_IR_OK;
}

static TurboJSIRStatus reload_stack_from_locals(TurboJSIRFunction *out,
                                                 uint16_t *stack,
                                                 size_t sp,
                                                 uint16_t stack_local_base,
                                                 TurboJSIRDiagnostic *diagnostic,
                                                 size_t offset)
{
    for (size_t i = 0; i < sp; ++i) {
        TurboJSIRInstruction load;
        uint16_t destination = TurboJS_IRAllocateRegister(out);
        if (destination == TURBOJS_IR_NO_REGISTER)
            return engine_fail(diagnostic, TURBOJS_IR_INVALID_REGISTER, offset,
                               "branch stack reload exceeds IR register limit");
        memset(&load, 0, sizeof(load));
        load.opcode = TURBOJS_IR_LOCAL_GET;
        load.destination = destination;
        load.left = TURBOJS_IR_NO_REGISTER;
        load.right = TURBOJS_IR_NO_REGISTER;
        load.immediate = (int64_t)(stack_local_base + i);
        load.bytecode_offset = (uint32_t)offset;
        if (emit(out, load, diagnostic, offset) != TURBOJS_IR_OK)
            return diagnostic ? diagnostic->status : TURBOJS_IR_OUT_OF_MEMORY;
        stack[i] = destination;
    }
    return TURBOJS_IR_OK;
}

TurboJSIRStatus TurboJS_EngineBytecodeToIR(const TurboJSEngineBytecodeInfo *bc,
                                           TurboJSIRFunction *out,
                                           TurboJSIRDiagnostic *diagnostic)
{
    uint16_t stack[TURBOJS_IR_MAX_REGISTERS];
    uint64_t method_shape_identity[TURBOJS_IR_MAX_REGISTERS] = {0};
    uint32_t method_shape_offset[TURBOJS_IR_MAX_REGISTERS] = {0};
    size_t sp = 0;
    size_t pc = 0;
    size_t offset = 0;
    int saw_return = 0;
    size_t *bytecode_to_ir = NULL;
    uint8_t *branch_targets = NULL;
    int32_t *stack_depths = NULL;
    uint32_t maximum_stack_depth = 0;
    uint16_t stack_local_base = 0;
    int fallthrough_reachable = 1;
    struct BranchFixup { size_t ir_index; size_t bytecode_target; };
    struct BranchFixup fixups[TURBOJS_IR_MAX_REGISTERS * 4u];
    size_t fixup_count = 0;

    if (bc && bc->lowering_stats)
        memset(bc->lowering_stats, 0, sizeof(*bc->lowering_stats));
    if (!bc || !out || (!bc->bytecode && bc->bytecode_length != 0)) {
        record_rejection(bc, TURBOJS_SPOOL_REJECT_INVALID_INPUT, 0);
        return engine_fail(diagnostic, TURBOJS_IR_INVALID_ARGUMENT, 0, "invalid engine bytecode input");
    }
    if (bc->argument_count > TURBOJS_IR_MAX_REGISTERS || bc->stack_size > TURBOJS_IR_MAX_REGISTERS ||
        (uint32_t)bc->local_count + (uint32_t)bc->stack_size > TURBOJS_IR_MAX_REGISTERS) {
        record_rejection(bc, TURBOJS_SPOOL_REJECT_FRAME_LIMIT, 0);
        return engine_fail(diagnostic, TURBOJS_IR_INVALID_REGISTER, 0, "engine bytecode exceeds baseline frame limits");
    }

    TurboJS_IRFunctionInit(out, bc->argument_count);
    stack_local_base = bc->local_count;
    TurboJS_IRFunctionSetLocalCount(out, (uint16_t)(bc->local_count + bc->stack_size));
    out->source_local_count = bc->local_count;
    bytecode_to_ir = (size_t *)malloc((bc->bytecode_length + 1u) * sizeof(*bytecode_to_ir));
    branch_targets = (uint8_t *)calloc(bc->bytecode_length + 1u, 1u);
    stack_depths = (int32_t *)malloc((bc->bytecode_length + 1u) * sizeof(*stack_depths));
    if (!bytecode_to_ir || !branch_targets || !stack_depths)
        goto out_of_memory;
    for (size_t i = 0; i <= bc->bytecode_length; ++i)
        bytecode_to_ir[i] = SIZE_MAX;
    {
        size_t bad_offset = 0;
        TurboJSIRStatus analysis_status = analyze_stack_depths(
            bc, stack_depths, branch_targets, &maximum_stack_depth, &bad_offset);
        if (analysis_status != TURBOJS_IR_OK) {
            offset = bad_offset;
            if (analysis_status == TURBOJS_IR_UNSUPPORTED)
                goto incompatible_stack_state;
            if (analysis_status == TURBOJS_IR_INVALID_TARGET)
                goto invalid_branch_target;
            if (analysis_status == TURBOJS_IR_OUT_OF_MEMORY)
                goto out_of_memory;
            goto truncated;
        }
        if (bc->lowering_stats) {
            bc->lowering_stats->maximum_stack_depth = maximum_stack_depth;
            for (size_t i = 0; i < bc->bytecode_length; ++i)
                if (branch_targets[i]) bc->lowering_stats->branch_target_count++;
        }
        if (maximum_stack_depth > bc->stack_size)
            goto stack_overflow;
    }

    while (pc < bc->bytecode_length) {
        offset = pc;
        if (stack_depths[offset] < 0) {
            TurboJSEngineStackEffect skipped;
            if (engine_stack_effect(bc, offset, &skipped) != TURBOJS_IR_OK)
                goto truncated;
            pc = skipped.next_offset;
            fallthrough_reachable = 0;
            continue;
        }
        if (branch_targets[offset]) {
            size_t expected_sp = (size_t)stack_depths[offset];
            if (bc->lowering_stats && expected_sp)
                bc->lowering_stats->nonempty_stack_merge_count++;
            if (fallthrough_reachable) {
                if (sp != expected_sp)
                    goto incompatible_stack_state;
                if (spill_stack_to_locals(out, stack, sp, stack_local_base,
                                          diagnostic, offset) != TURBOJS_IR_OK)
                    goto fail;
                if (bc->lowering_stats)
                    bc->lowering_stats->stack_spill_store_count += (uint32_t)sp;
            }
            bytecode_to_ir[offset] = out->instruction_count;
            sp = expected_sp;
            if (reload_stack_from_locals(out, stack, sp, stack_local_base,
                                         diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            if (bc->lowering_stats)
                bc->lowering_stats->stack_reload_count += (uint32_t)sp;
            fallthrough_reachable = 1;
        } else {
            bytecode_to_ir[offset] = out->instruction_count;
        }
        uint8_t opcode = bc->bytecode[pc++];
        TurboJSIRInstruction ins;
        uint16_t destination;
        memset(&ins, 0, sizeof(ins));
        ins.destination = TURBOJS_IR_NO_REGISTER;
        ins.left = TURBOJS_IR_NO_REGISTER;
        ins.right = TURBOJS_IR_NO_REGISTER;
        ins.bytecode_offset = (uint32_t)offset;

        switch (opcode) {
        case OP_nop:
            break;
        case OP_push_i32:
            if (pc + 4 > bc->bytecode_length)
                goto truncated;
            destination = TurboJS_IRAllocateRegister(out);
            if (destination == TURBOJS_IR_NO_REGISTER)
                goto register_limit;
            ins.opcode = TURBOJS_IR_CONSTANT_I64;
            ins.destination = destination;
            ins.immediate = read_i32(bc->bytecode + pc);
            pc += 4;
            if (emit(out, ins, diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            if (sp >= TURBOJS_IR_MAX_REGISTERS)
                goto stack_overflow;
            stack[sp++] = destination;
            break;
        case OP_get_arg:
            if (pc + 2 > bc->bytecode_length)
                goto truncated;
            ins.immediate = read_u16(bc->bytecode + pc);
            pc += 2;
            if ((uint64_t)ins.immediate >= bc->argument_count)
                goto invalid_argument_index;
            destination = TurboJS_IRAllocateRegister(out);
            if (destination == TURBOJS_IR_NO_REGISTER)
                goto register_limit;
            ins.opcode = TURBOJS_IR_ARGUMENT;
            ins.destination = destination;
            if (emit(out, ins, diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            if (sp >= TURBOJS_IR_MAX_REGISTERS)
                goto stack_overflow;
            stack[sp++] = destination;
            break;
        case OP_get_loc:
            if (pc + 2 > bc->bytecode_length)
                goto truncated;
            ins.immediate = read_u16(bc->bytecode + pc);
            pc += 2;
            if ((uint64_t)ins.immediate >= bc->local_count)
                goto invalid_local_index;
            destination = TurboJS_IRAllocateRegister(out);
            if (destination == TURBOJS_IR_NO_REGISTER)
                goto register_limit;
            ins.opcode = TURBOJS_IR_LOCAL_GET;
            ins.destination = destination;
            if (emit(out, ins, diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            if (sp >= TURBOJS_IR_MAX_REGISTERS)
                goto stack_overflow;
            stack[sp++] = destination;
            break;
        case OP_put_loc: case OP_set_loc:
            if (pc + 2 > bc->bytecode_length)
                goto truncated;
            ins.immediate = read_u16(bc->bytecode + pc);
            pc += 2;
            if ((uint64_t)ins.immediate >= bc->local_count)
                goto invalid_local_index;
            if (sp < 1)
                goto stack_underflow;
            ins.opcode = TURBOJS_IR_LOCAL_SET;
            ins.left = stack[sp - 1];
            if (emit(out, ins, diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            if (opcode == OP_put_loc)
                --sp;
            break;
        case OP_get_loc8:
            if (pc + 1 > bc->bytecode_length)
                goto truncated;
            ins.immediate = bc->bytecode[pc++];
            if ((uint64_t)ins.immediate >= bc->local_count)
                goto invalid_local_index;
            destination = TurboJS_IRAllocateRegister(out);
            if (destination == TURBOJS_IR_NO_REGISTER)
                goto register_limit;
            ins.opcode = TURBOJS_IR_LOCAL_GET;
            ins.destination = destination;
            if (emit(out, ins, diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            if (sp >= TURBOJS_IR_MAX_REGISTERS)
                goto stack_overflow;
            stack[sp++] = destination;
            break;
        case OP_put_loc8: case OP_set_loc8:
            if (pc + 1 > bc->bytecode_length)
                goto truncated;
            ins.immediate = bc->bytecode[pc++];
            if ((uint64_t)ins.immediate >= bc->local_count)
                goto invalid_local_index;
            if (sp < 1)
                goto stack_underflow;
            ins.opcode = TURBOJS_IR_LOCAL_SET;
            ins.left = stack[sp - 1];
            if (emit(out, ins, diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            if (opcode == OP_put_loc8)
                --sp;
            break;
        case OP_get_loc0: case OP_get_loc1: case OP_get_loc2: case OP_get_loc3:
            ins.immediate = opcode - OP_get_loc0;
            if ((uint64_t)ins.immediate >= bc->local_count)
                goto invalid_local_index;
            destination = TurboJS_IRAllocateRegister(out);
            if (destination == TURBOJS_IR_NO_REGISTER)
                goto register_limit;
            ins.opcode = TURBOJS_IR_LOCAL_GET;
            ins.destination = destination;
            if (emit(out, ins, diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            if (sp >= TURBOJS_IR_MAX_REGISTERS)
                goto stack_overflow;
            stack[sp++] = destination;
            break;
        case OP_put_loc0: case OP_put_loc1: case OP_put_loc2: case OP_put_loc3:
        case OP_set_loc0: case OP_set_loc1: case OP_set_loc2: case OP_set_loc3:
            ins.immediate = opcode >= OP_set_loc0 ? opcode - OP_set_loc0 : opcode - OP_put_loc0;
            if ((uint64_t)ins.immediate >= bc->local_count)
                goto invalid_local_index;
            if (sp < 1)
                goto stack_underflow;
            ins.opcode = TURBOJS_IR_LOCAL_SET;
            ins.left = stack[sp - 1];
            if (emit(out, ins, diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            if (opcode >= OP_put_loc0 && opcode <= OP_put_loc3)
                --sp;
            break;
        case OP_get_arg0: case OP_get_arg1: case OP_get_arg2: case OP_get_arg3:
            ins.immediate = opcode - OP_get_arg0;
            if ((uint64_t)ins.immediate >= bc->argument_count)
                goto invalid_argument_index;
            destination = TurboJS_IRAllocateRegister(out);
            if (destination == TURBOJS_IR_NO_REGISTER)
                goto register_limit;
            ins.opcode = TURBOJS_IR_ARGUMENT;
            ins.destination = destination;
            if (emit(out, ins, diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            if (sp >= TURBOJS_IR_MAX_REGISTERS)
                goto stack_overflow;
            stack[sp++] = destination;
            break;
        case OP_get_field2: {
            uint32_t atom;
            uint32_t shape_offset = 0;
            uint64_t shape_identity = 0;
            TurboJSCallableReference *reference;
            TurboJSIRStatus resolve_status;
            if (pc + 4 > bc->bytecode_length) goto truncated;
            atom = (uint32_t)read_i32(bc->bytecode + pc);
            pc += 4;
            if (sp < 1) goto stack_underflow;
            if (!bc->method_property_resolver) goto unsupported_property_method;
            reference = TurboJS_IRAllocateCallableReference(out);
            if (!reference) goto out_of_memory;
            resolve_status = bc->method_property_resolver(
                bc->method_property_resolver_opaque, (uint32_t)offset, atom,
                reference, &shape_offset, &shape_identity);
            if (resolve_status != TURBOJS_IR_OK || !shape_identity) {
                if (bc->lowering_stats) bc->lowering_stats->property_method_rejection_count++;
                TurboJSCallableReference *rollback =
                    out->owned_callable_references[--out->owned_callable_reference_count];
                TurboJS_CallableReferenceDestroy(rollback);
                free(rollback);
                goto unsupported_property_method;
            }
            destination = TurboJS_IRAllocateRegister(out);
            if (destination == TURBOJS_IR_NO_REGISTER) goto register_limit;
            ins.opcode = TURBOJS_IR_VALUE_CALLABLE_CONSTANT;
            ins.destination = destination;
            ins.immediate = (int64_t)(uintptr_t)reference;
            if (emit(out, ins, diagnostic, offset) != TURBOJS_IR_OK) goto fail;
            if (sp >= TURBOJS_IR_MAX_REGISTERS) goto stack_overflow;
            stack[sp++] = destination;
            method_shape_identity[destination] = shape_identity;
            method_shape_offset[destination] = shape_offset;
            if (bc->lowering_stats) bc->lowering_stats->property_method_load_count++;
            break;
        }
        case OP_get_var: case OP_get_var_undef:
        case OP_get_var_ref: case OP_get_var_ref_check:
        case OP_get_var_ref0: case OP_get_var_ref1:
        case OP_get_var_ref2: case OP_get_var_ref3: {
            TurboJSEngineCallableLoadKind kind;
            uint32_t index_or_atom;
            TurboJSCallableReference *reference;
            TurboJSIRStatus resolve_status;
            if (!bc->callable_resolver)
                goto unsupported_callable_load;
            if (opcode == OP_get_var || opcode == OP_get_var_undef) {
                if (pc + 4 > bc->bytecode_length) goto truncated;
                kind = TURBOJS_ENGINE_CALLABLE_GLOBAL;
                index_or_atom = (uint32_t)read_i32(bc->bytecode + pc);
                pc += 4;
            } else {
                kind = TURBOJS_ENGINE_CALLABLE_CLOSURE;
                if (opcode == OP_get_var_ref || opcode == OP_get_var_ref_check) {
                    if (pc + 2 > bc->bytecode_length) goto truncated;
                    index_or_atom = read_u16(bc->bytecode + pc);
                    pc += 2;
                } else {
                    index_or_atom = (uint32_t)(opcode - OP_get_var_ref0);
                }
            }
            reference = TurboJS_IRAllocateCallableReference(out);
            if (!reference) goto out_of_memory;
            resolve_status = bc->callable_resolver(
                bc->callable_resolver_opaque, (uint32_t)offset, kind,
                index_or_atom, reference);
            if (resolve_status != TURBOJS_IR_OK) {
                if (bc->lowering_stats) bc->lowering_stats->callable_load_rejection_count++;
                TurboJSCallableReference *rollback =
                    out->owned_callable_references[--out->owned_callable_reference_count];
                TurboJS_CallableReferenceDestroy(rollback);
                free(rollback);
                goto unsupported_callable_load;
            }
            if (bc->lowering_stats) {
                if (kind == TURBOJS_ENGINE_CALLABLE_GLOBAL) bc->lowering_stats->callable_global_load_count++;
                else bc->lowering_stats->callable_closure_load_count++;
            }
            destination = TurboJS_IRAllocateRegister(out);
            if (destination == TURBOJS_IR_NO_REGISTER) goto register_limit;
            ins.opcode = TURBOJS_IR_VALUE_CALLABLE_CONSTANT;
            ins.destination = destination;
            ins.immediate = (int64_t)(uintptr_t)reference;
            if (emit(out, ins, diagnostic, offset) != TURBOJS_IR_OK) goto fail;
            if (sp >= TURBOJS_IR_MAX_REGISTERS) goto stack_overflow;
            stack[sp++] = destination;
            break;
        }
        case OP_add: case OP_sub: case OP_mul: case OP_div: case OP_mod: case OP_lt:
            if (sp < 2)
                goto stack_underflow;
            destination = TurboJS_IRAllocateRegister(out);
            if (destination == TURBOJS_IR_NO_REGISTER)
                goto register_limit;
            ins.destination = destination;
            ins.right = stack[--sp];
            ins.left = stack[--sp];
            if (bc->numeric_mode == TURBOJS_ENGINE_NUMERIC_FLOAT64) {
                if (opcode == OP_add) ins.opcode = TURBOJS_IR_ADD_F64;
                else if (opcode == OP_sub) ins.opcode = TURBOJS_IR_SUB_F64;
                else if (opcode == OP_mul) ins.opcode = TURBOJS_IR_MUL_F64;
                else if (opcode == OP_div) ins.opcode = TURBOJS_IR_DIV_F64;
                else goto unsupported_numeric_mode;
            } else {
                if (opcode == OP_add) ins.opcode = TURBOJS_IR_ADD_I32_CHECKED;
                else if (opcode == OP_sub) ins.opcode = TURBOJS_IR_SUB_I32_CHECKED;
                else if (opcode == OP_mul) ins.opcode = TURBOJS_IR_MUL_I32_CHECKED;
                else if (opcode == OP_div) ins.opcode = TURBOJS_IR_DIV_I32_CHECKED;
                else if (opcode == OP_mod) ins.opcode = TURBOJS_IR_REM_I32_CHECKED;
                else ins.opcode = TURBOJS_IR_LESS_THAN_I64;
            }
            if (emit(out, ins, diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            stack[sp++] = destination;
            break;
        case OP_goto: case OP_goto16: case OP_goto8:
        case OP_if_true: case OP_if_false: case OP_if_true8: case OP_if_false8: {
            int64_t displacement;
            size_t operand_start = pc;
            int conditional = opcode == OP_if_true || opcode == OP_if_false ||
                              opcode == OP_if_true8 || opcode == OP_if_false8;
            if (opcode == OP_goto || opcode == OP_if_true || opcode == OP_if_false) {
                if (pc + 4 > bc->bytecode_length) goto truncated;
                displacement = read_i32(bc->bytecode + pc); pc += 4;
            } else if (opcode == OP_goto16) {
                if (pc + 2 > bc->bytecode_length) goto truncated;
                displacement = read_i16(bc->bytecode + pc); pc += 2;
            } else {
                if (pc + 1 > bc->bytecode_length) goto truncated;
                displacement = (int8_t)bc->bytecode[pc++];
            }
            int64_t target = (int64_t)operand_start + displacement;
            if (target < 0 || (uint64_t)target >= bc->bytecode_length)
                goto invalid_branch_target;
            if (conditional) {
                if (sp < 1) goto stack_underflow;
                ins.left = stack[--sp];
                ins.opcode = (opcode == OP_if_true || opcode == OP_if_true8) ?
                             TURBOJS_IR_BRANCH_TRUE : TURBOJS_IR_BRANCH_FALSE;
            } else {
                ins.opcode = TURBOJS_IR_JUMP;
            }
            if (spill_stack_to_locals(out, stack, sp, stack_local_base,
                                      diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            if (bc->lowering_stats)
                bc->lowering_stats->stack_spill_store_count += (uint32_t)sp;
            ins.target = 0;
            if (emit(out, ins, diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            if (fixup_count >= sizeof(fixups) / sizeof(fixups[0]))
                goto register_limit;
            fixups[fixup_count].ir_index = out->instruction_count - 1u;
            fixups[fixup_count].bytecode_target = (size_t)target;
            fixup_count++;
            if (!conditional)
                fallthrough_reachable = 0;
            break;
        }
        case OP_call_method: case OP_tail_call_method: {
            uint16_t argc;
            size_t base;
            TurboJSClutchCallSite *site;
            TurboJSIRStatus resolve_status;
            if (pc + 2 > bc->bytecode_length)
                goto truncated;
            argc = read_u16(bc->bytecode + pc);
            pc += 2;
            if (argc + 1u > TURBOJS_CLUTCH_MAX_ARGUMENTS ||
                sp < (size_t)argc + 2u)
                goto unsupported_call;
            if (!bc->call_resolver)
                goto unsupported_call;
            base = sp - ((size_t)argc + 2u);
            site = TurboJS_IRAllocateClutchCallSite(out);
            if (!site)
                goto out_of_memory;
            resolve_status = bc->call_resolver(
                bc->call_resolver_opaque, (uint32_t)offset, argc,
                bc->numeric_mode, site);
            if (resolve_status != TURBOJS_IR_OK) {
                TurboJSClutchCallSite *rollback_site =
                    out->owned_clutch_sites[--out->owned_clutch_site_count];
                TurboJS_ClutchCallSiteDestroy(rollback_site);
                free(rollback_site);
                goto unsupported_call;
            }
            site->continuation_bytecode_offset = (uint32_t)pc;
            for (uint16_t i = 0; i < argc; ++i) {
                if (TurboJS_ClutchCallSiteSetArgument(site, i,
                        stack[base + 2u + i]) != TURBOJS_IR_OK)
                    goto unsupported_call;
            }
            if (TurboJS_ClutchCallSiteSetReceiver(site, stack[base]) !=
                TURBOJS_IR_OK)
                goto unsupported_call;
            if (method_shape_identity[stack[base + 1u]]) {
                if (TurboJS_ClutchCallSiteSetReceiverShapeGuard(
                        site, method_shape_offset[stack[base + 1u]],
                        method_shape_identity[stack[base + 1u]]) != TURBOJS_IR_OK)
                    goto unsupported_call;
                if (bc->lowering_stats) bc->lowering_stats->property_shape_guard_count++;
            }
            destination = TurboJS_IRAllocateRegister(out);
            if (destination == TURBOJS_IR_NO_REGISTER)
                goto register_limit;
            ins.opcode = bc->numeric_mode == TURBOJS_ENGINE_NUMERIC_FLOAT64 ?
                         TURBOJS_IR_CALL_NATIVE_F64 : TURBOJS_IR_CALL_NATIVE_I64;
            ins.destination = destination;
            ins.immediate = (int64_t)(uintptr_t)site;
            ins.target = site->argument_count;
            if (emit(out, ins, diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            if (bc->lowering_stats)
                bc->lowering_stats->receiver_method_call_count++;
            sp = base;
            stack[sp++] = destination;
            if (opcode == OP_tail_call_method) {
                ins.opcode = bc->numeric_mode == TURBOJS_ENGINE_NUMERIC_FLOAT64 ?
                             TURBOJS_IR_RETURN_F64 : TURBOJS_IR_RETURN_I64;
                ins.left = destination;
                ins.destination = 0;
                ins.immediate = 0;
                ins.target = 0;
                if (emit(out, ins, diagnostic, offset) != TURBOJS_IR_OK)
                    goto fail;
                saw_return = 1;
                sp = 0;
                fallthrough_reachable = 0;
            }
            break;
        }
        case OP_call0: case OP_call1: case OP_call2: case OP_call3:
        case OP_call: {
            uint16_t argc;
            size_t base;
            TurboJSClutchCallSite *site;
            TurboJSIRStatus resolve_status;
            if (opcode == OP_call) {
                if (pc + 2 > bc->bytecode_length)
                    goto truncated;
                argc = read_u16(bc->bytecode + pc);
                pc += 2;
            } else {
                argc = (uint16_t)(opcode - OP_call0);
            }
            if (argc > TURBOJS_CLUTCH_MAX_ARGUMENTS || sp < (size_t)argc + 1u)
                goto unsupported_call;
            if (!bc->call_resolver)
                goto unsupported_call;
            base = sp - ((size_t)argc + 1u);
            site = TurboJS_IRAllocateClutchCallSite(out);
            if (!site)
                goto out_of_memory;
            resolve_status = bc->call_resolver(
                bc->call_resolver_opaque, (uint32_t)offset, argc,
                bc->numeric_mode, site);
            if (resolve_status != TURBOJS_IR_OK) {
                TurboJSClutchCallSite *rollback_site =
                    out->owned_clutch_sites[--out->owned_clutch_site_count];
                TurboJS_ClutchCallSiteDestroy(rollback_site);
                free(rollback_site);
                goto unsupported_call;
            }
            site->continuation_bytecode_offset = (uint32_t)pc;
            for (uint16_t i = 0; i < argc; ++i) {
                if (TurboJS_ClutchCallSiteSetArgument(site, i,
                        stack[base + 1u + i]) != TURBOJS_IR_OK)
                    goto unsupported_call;
            }
            destination = TurboJS_IRAllocateRegister(out);
            if (destination == TURBOJS_IR_NO_REGISTER)
                goto register_limit;
            ins.opcode = bc->numeric_mode == TURBOJS_ENGINE_NUMERIC_FLOAT64 ?
                         TURBOJS_IR_CALL_NATIVE_F64 : TURBOJS_IR_CALL_NATIVE_I64;
            ins.destination = destination;
            ins.immediate = (int64_t)(uintptr_t)site;
            ins.target = argc;
            if (emit(out, ins, diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            sp = base;
            stack[sp++] = destination;
            break;
        }
        case OP_return:
            if (sp < 1)
                goto stack_underflow;
            ins.opcode = bc->numeric_mode == TURBOJS_ENGINE_NUMERIC_FLOAT64 ?
                         TURBOJS_IR_RETURN_F64 : TURBOJS_IR_RETURN_I64;
            ins.left = stack[--sp];
            if (emit(out, ins, diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            saw_return = 1;
            sp = 0;
            fallthrough_reachable = 0;
            break;
        default: {
            TurboJSIRInstruction zero;
            uint16_t left = TurboJS_IRAllocateRegister(out);
            uint16_t right = TurboJS_IRAllocateRegister(out);
            uint16_t destination = TurboJS_IRAllocateRegister(out);
            if (left == TURBOJS_IR_NO_REGISTER || right == TURBOJS_IR_NO_REGISTER ||
                destination == TURBOJS_IR_NO_REGISTER)
                goto register_limit;
            memset(&zero, 0, sizeof(zero));
            zero.opcode = TURBOJS_IR_CONSTANT_I64;
            zero.destination = left;
            zero.bytecode_offset = (uint32_t)offset;
            if (emit(out, zero, diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            zero.destination = right;
            if (emit(out, zero, diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            memset(&ins, 0, sizeof(ins));
            ins.opcode = TURBOJS_IR_RUNTIME_HELPER;
            ins.destination = destination;
            ins.left = left;
            ins.right = right;
            ins.immediate = (int64_t)opcode;
            ins.bytecode_offset = (uint32_t)offset;
            if (emit(out, ins, diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            /* Keep the verifier's explicit-return invariant. This return is
             * unreachable because every backend treats RUNTIME_HELPER as a
             * Rewind bailout at the exact source bytecode offset. */
            memset(&ins, 0, sizeof(ins));
            ins.opcode = TURBOJS_IR_RETURN_I64;
            ins.left = destination;
            ins.bytecode_offset = (uint32_t)offset;
            if (emit(out, ins, diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            saw_return = 1;
            fallthrough_reachable = 0;
            if (bc->lowering_stats) {
                bc->lowering_stats->runtime_helper_exit_count++;
                bc->lowering_stats->partial_function_count = 1;
            }
            pc = bc->bytecode_length;
            break;
        }
        }
    }

    if (!saw_return) {
        free(bytecode_to_ir); free(branch_targets); free(stack_depths);
        TurboJS_IRFunctionDestroy(out);
        return engine_fail(diagnostic, TURBOJS_IR_MISSING_RETURN, pc, "engine bytecode has no supported return");
    }
    bytecode_to_ir[bc->bytecode_length] = out->instruction_count;
    for (size_t i = 0; i < fixup_count; ++i) {
        size_t target = fixups[i].bytecode_target;
        if (bytecode_to_ir[target] == SIZE_MAX)
            goto invalid_branch_target;
        out->instructions[fixups[i].ir_index].target = (uint32_t)bytecode_to_ir[target];
    }
    free(bytecode_to_ir);
    free(branch_targets);
    free(stack_depths);
    bytecode_to_ir = NULL;
    branch_targets = NULL;
    stack_depths = NULL;
    {
        TurboJSIRStatus status = TurboJS_IRVerify(out, diagnostic);
        if (status != TURBOJS_IR_OK)
            TurboJS_IRFunctionDestroy(out);
        return status;
    }

truncated:
    record_rejection(bc, TURBOJS_SPOOL_REJECT_TRUNCATED_BYTECODE, offset);
    free(bytecode_to_ir); free(branch_targets); free(stack_depths);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_INVALID_OPCODE, offset, "truncated engine bytecode operand");
stack_underflow:
    record_rejection(bc, TURBOJS_SPOOL_REJECT_STACK_UNDERFLOW, offset);
    free(bytecode_to_ir); free(branch_targets); free(stack_depths);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_INVALID_OPCODE, offset, "engine operand stack underflow");
stack_overflow:
    record_rejection(bc, TURBOJS_SPOOL_REJECT_STACK_OVERFLOW, offset);
    free(bytecode_to_ir); free(branch_targets); free(stack_depths);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_INVALID_REGISTER, offset, "engine operand stack exceeds baseline limit");
register_limit:
    record_rejection(bc, TURBOJS_SPOOL_REJECT_REGISTER_LIMIT, offset);
    free(bytecode_to_ir); free(branch_targets); free(stack_depths);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_INVALID_REGISTER, offset, "engine bytecode requires too many IR registers");
invalid_local_index:
    record_rejection(bc, TURBOJS_SPOOL_REJECT_LOCAL_INDEX, offset);
    free(bytecode_to_ir); free(branch_targets); free(stack_depths);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_INVALID_REGISTER, offset, "engine bytecode local index is out of range");
unsupported_property_method:
    record_rejection(bc, TURBOJS_SPOOL_REJECT_CALL_FEEDBACK, offset);
    free(bytecode_to_ir); free(branch_targets); free(stack_depths);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_UNSUPPORTED, offset,
                       "property method load lacks monomorphic shape feedback");
unsupported_callable_load:
    record_rejection(bc, TURBOJS_SPOOL_REJECT_CALL_FEEDBACK, offset);
    free(bytecode_to_ir); free(branch_targets); free(stack_depths);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_UNSUPPORTED, offset,
                       "callable load lacks stable rooted CallableRef metadata");
unsupported_call:
    record_rejection(bc, TURBOJS_SPOOL_REJECT_CALL_FEEDBACK, offset);
    free(bytecode_to_ir); free(branch_targets); free(stack_depths);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_UNSUPPORTED, offset,
                       "call site lacks stable monomorphic Clutch metadata");
unsupported_numeric_mode:
    record_rejection(bc, TURBOJS_SPOOL_REJECT_NUMERIC_MODE, offset);
    free(bytecode_to_ir); free(branch_targets); free(stack_depths);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_UNSUPPORTED, offset,
                       "numeric opcode is unsupported in selected specialization");
invalid_argument_index:
    record_rejection(bc, TURBOJS_SPOOL_REJECT_ARGUMENT_INDEX, offset);
    free(bytecode_to_ir); free(branch_targets); free(stack_depths);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_INVALID_REGISTER, offset, "engine bytecode argument index is out of range");
invalid_branch_target:
    record_rejection(bc, TURBOJS_SPOOL_REJECT_BRANCH_TARGET, offset);
    free(bytecode_to_ir); free(branch_targets); free(stack_depths);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_INVALID_TARGET, offset, "engine branch target is invalid");
incompatible_stack_state:
    record_rejection(bc, TURBOJS_SPOOL_REJECT_STACK_MERGE, offset);
    free(bytecode_to_ir); free(branch_targets); free(stack_depths);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_UNSUPPORTED, offset, "control-flow predecessors disagree on operand-stack depth");
out_of_memory:
    record_rejection(bc, TURBOJS_SPOOL_REJECT_OUT_OF_MEMORY, offset);
    free(bytecode_to_ir); free(branch_targets); free(stack_depths);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_OUT_OF_MEMORY, offset, "unable to allocate bytecode control-flow maps");
fail:
    free(bytecode_to_ir); free(branch_targets); free(stack_depths);
    TurboJS_IRFunctionDestroy(out);
    return diagnostic ? diagnostic->status : TURBOJS_IR_OUT_OF_MEMORY;
}
