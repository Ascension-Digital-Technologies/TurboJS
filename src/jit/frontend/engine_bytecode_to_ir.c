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

TurboJSIRStatus TurboJS_EngineBytecodeToIR(const TurboJSEngineBytecodeInfo *bc,
                                           TurboJSIRFunction *out,
                                           TurboJSIRDiagnostic *diagnostic)
{
    uint16_t stack[TURBOJS_IR_MAX_REGISTERS];
    size_t sp = 0;
    size_t pc = 0;
    size_t offset = 0;
    int saw_return = 0;
    size_t *bytecode_to_ir = NULL;
    uint8_t *branch_targets = NULL;
    struct BranchFixup { size_t ir_index; size_t bytecode_target; };
    struct BranchFixup fixups[TURBOJS_IR_MAX_REGISTERS * 4u];
    size_t fixup_count = 0;

    if (!bc || !out || (!bc->bytecode && bc->bytecode_length != 0))
        return engine_fail(diagnostic, TURBOJS_IR_INVALID_ARGUMENT, 0, "invalid engine bytecode input");
    if (bc->argument_count > TURBOJS_IR_MAX_REGISTERS || bc->stack_size > TURBOJS_IR_MAX_REGISTERS)
        return engine_fail(diagnostic, TURBOJS_IR_INVALID_REGISTER, 0, "engine bytecode exceeds baseline register limits");

    TurboJS_IRFunctionInit(out, bc->argument_count);
    TurboJS_IRFunctionSetLocalCount(out, bc->local_count);
    bytecode_to_ir = (size_t *)malloc((bc->bytecode_length + 1u) * sizeof(*bytecode_to_ir));
    branch_targets = (uint8_t *)calloc(bc->bytecode_length + 1u, 1u);
    if (!bytecode_to_ir || !branch_targets)
        goto out_of_memory;
    for (size_t i = 0; i <= bc->bytecode_length; ++i)
        bytecode_to_ir[i] = SIZE_MAX;

    /* Pre-scan branch operands so stack state can be checked at every target. */
    {
        size_t scan = 0;
        while (scan < bc->bytecode_length) {
            size_t op_offset = scan;
            uint8_t op = bc->bytecode[scan++];
            size_t operand_size = 0;
            int64_t displacement = 0;
            switch (op) {
            case OP_push_i32: operand_size = 4; break;
            case OP_get_arg: case OP_get_loc: case OP_put_loc: case OP_set_loc: operand_size = 2; break;
            case OP_get_loc8: case OP_put_loc8: case OP_set_loc8: operand_size = 1; break;
            case OP_goto: case OP_if_true: case OP_if_false: operand_size = 4; break;
            case OP_goto16: operand_size = 2; break;
            case OP_goto8: case OP_if_true8: case OP_if_false8: operand_size = 1; break;
            default: operand_size = 0; break;
            }
            if (scan + operand_size > bc->bytecode_length)
                goto truncated;
            if (op == OP_goto || op == OP_if_true || op == OP_if_false)
                displacement = read_i32(bc->bytecode + scan);
            else if (op == OP_goto16)
                displacement = read_i16(bc->bytecode + scan);
            else if (op == OP_goto8 || op == OP_if_true8 || op == OP_if_false8)
                displacement = (int8_t)bc->bytecode[scan];
            if (op == OP_goto || op == OP_if_true || op == OP_if_false ||
                op == OP_goto16 || op == OP_goto8 || op == OP_if_true8 || op == OP_if_false8) {
                int64_t target = (int64_t)scan + displacement;
                if (target < 0 || (uint64_t)target >= bc->bytecode_length)
                    goto invalid_branch_target;
                branch_targets[(size_t)target] = 1;
            }
            scan += operand_size;
            (void)op_offset;
        }
    }

    while (pc < bc->bytecode_length) {
        offset = pc;
        if (branch_targets[offset] && sp != 0)
            goto incompatible_stack_state;
        bytecode_to_ir[offset] = out->instruction_count;
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
        case OP_add: case OP_sub: case OP_mul: case OP_div: case OP_mod: case OP_lt:
            if (sp < 2)
                goto stack_underflow;
            destination = TurboJS_IRAllocateRegister(out);
            if (destination == TURBOJS_IR_NO_REGISTER)
                goto register_limit;
            ins.destination = destination;
            ins.right = stack[--sp];
            ins.left = stack[--sp];
            if (opcode == OP_add) ins.opcode = TURBOJS_IR_ADD_I32_CHECKED;
            else if (opcode == OP_sub) ins.opcode = TURBOJS_IR_SUB_I32_CHECKED;
            else if (opcode == OP_mul) ins.opcode = TURBOJS_IR_MUL_I32_CHECKED;
            else if (opcode == OP_div) ins.opcode = TURBOJS_IR_DIV_I32_CHECKED;
            else if (opcode == OP_mod) ins.opcode = TURBOJS_IR_REM_I32_CHECKED;
            else ins.opcode = TURBOJS_IR_LESS_THAN_I64;
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
            if (sp != 0)
                goto incompatible_stack_state;
            ins.target = 0;
            if (emit(out, ins, diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            if (fixup_count >= sizeof(fixups) / sizeof(fixups[0]))
                goto register_limit;
            fixups[fixup_count].ir_index = out->instruction_count - 1u;
            fixups[fixup_count].bytecode_target = (size_t)target;
            fixup_count++;
            break;
        }
        case OP_return:
            if (sp < 1)
                goto stack_underflow;
            ins.opcode = TURBOJS_IR_RETURN_I64;
            ins.left = stack[--sp];
            if (emit(out, ins, diagnostic, offset) != TURBOJS_IR_OK)
                goto fail;
            saw_return = 1;
            sp = 0;
            break;
        default:
            free(bytecode_to_ir); free(branch_targets);
            TurboJS_IRFunctionDestroy(out);
            return engine_fail(diagnostic, TURBOJS_IR_UNSUPPORTED, offset,
                               "engine opcode requires dynamic JavaScript fallback");
        }
    }

    if (!saw_return) {
        free(bytecode_to_ir); free(branch_targets);
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
    bytecode_to_ir = NULL;
    branch_targets = NULL;
    {
        TurboJSIRStatus status = TurboJS_IRVerify(out, diagnostic);
        if (status != TURBOJS_IR_OK)
            TurboJS_IRFunctionDestroy(out);
        return status;
    }

truncated:
    free(bytecode_to_ir); free(branch_targets);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_INVALID_OPCODE, offset, "truncated engine bytecode operand");
stack_underflow:
    free(bytecode_to_ir); free(branch_targets);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_INVALID_OPCODE, offset, "engine operand stack underflow");
stack_overflow:
    free(bytecode_to_ir); free(branch_targets);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_INVALID_REGISTER, offset, "engine operand stack exceeds baseline limit");
register_limit:
    free(bytecode_to_ir); free(branch_targets);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_INVALID_REGISTER, offset, "engine bytecode requires too many IR registers");
invalid_local_index:
    free(bytecode_to_ir); free(branch_targets);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_INVALID_REGISTER, offset, "engine bytecode local index is out of range");
invalid_argument_index:
    free(bytecode_to_ir); free(branch_targets);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_INVALID_REGISTER, offset, "engine bytecode argument index is out of range");
invalid_branch_target:
    free(bytecode_to_ir); free(branch_targets);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_INVALID_TARGET, offset, "engine branch target is invalid");
incompatible_stack_state:
    free(bytecode_to_ir); free(branch_targets);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_UNSUPPORTED, offset, "baseline control flow requires an empty operand stack at branch boundaries");
out_of_memory:
    free(bytecode_to_ir); free(branch_targets);
    TurboJS_IRFunctionDestroy(out);
    return engine_fail(diagnostic, TURBOJS_IR_OUT_OF_MEMORY, offset, "unable to allocate bytecode control-flow maps");
fail:
    free(bytecode_to_ir); free(branch_targets);
    TurboJS_IRFunctionDestroy(out);
    return diagnostic ? diagnostic->status : TURBOJS_IR_OUT_OF_MEMORY;
}
