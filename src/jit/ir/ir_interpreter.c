#include <limits.h>
#include <string.h>

#include "jit.h"

static TurboJSIRStatus execute_from(const TurboJSIRFunction *function,
                                    int64_t *registers,
                                    int64_t *locals,
                                    size_t pc,
                                    int64_t *result)
{
    size_t steps = 0;
    size_t step_limit = function->instruction_count * 1000u + 1024u;
    while (pc < function->instruction_count) {
        const TurboJSIRInstruction *instruction;
        if (++steps > step_limit)
            return TURBOJS_IR_EXECUTION_LIMIT;
        instruction = &function->instructions[pc];
        switch (instruction->opcode) {
        case TURBOJS_IR_NOP: ++pc; break;
        case TURBOJS_IR_ARGUMENT:
            /* Arguments are materialized before this helper is entered. */
            return TURBOJS_IR_INVALID_ARGUMENT;
        case TURBOJS_IR_CONSTANT_I64:
        case TURBOJS_IR_CONSTANT_F64: registers[instruction->destination] = instruction->immediate; ++pc; break;
        case TURBOJS_IR_ADD_I64: registers[instruction->destination] = registers[instruction->left] + registers[instruction->right]; ++pc; break;
        case TURBOJS_IR_SUB_I64: registers[instruction->destination] = registers[instruction->left] - registers[instruction->right]; ++pc; break;
        case TURBOJS_IR_MUL_I64: registers[instruction->destination] = registers[instruction->left] * registers[instruction->right]; ++pc; break;
        case TURBOJS_IR_ADD_F64:
        case TURBOJS_IR_SUB_F64:
        case TURBOJS_IR_MUL_F64:
        case TURBOJS_IR_DIV_F64: {
            double left, right, value;
            memcpy(&left, &registers[instruction->left], sizeof(left));
            memcpy(&right, &registers[instruction->right], sizeof(right));
            if (instruction->opcode == TURBOJS_IR_ADD_F64) value = left + right;
            else if (instruction->opcode == TURBOJS_IR_SUB_F64) value = left - right;
            else if (instruction->opcode == TURBOJS_IR_MUL_F64) value = left * right;
            else value = left / right;
            memcpy(&registers[instruction->destination], &value, sizeof(value));
            ++pc; break;
        }
        case TURBOJS_IR_LESS_THAN_F64:
        case TURBOJS_IR_LESS_EQUAL_F64:
        case TURBOJS_IR_EQUAL_F64: {
            double left, right;
            memcpy(&left, &registers[instruction->left], sizeof(left));
            memcpy(&right, &registers[instruction->right], sizeof(right));
            registers[instruction->destination] = instruction->opcode == TURBOJS_IR_LESS_THAN_F64 ? left < right :
                instruction->opcode == TURBOJS_IR_LESS_EQUAL_F64 ? left <= right : left == right;
            ++pc; break;
        }
        case TURBOJS_IR_I64_TO_F64: {
            double value = (double)registers[instruction->left];
            memcpy(&registers[instruction->destination], &value, sizeof(value));
            ++pc; break;
        }
        case TURBOJS_IR_F64_TO_I64_TRUNC: {
            double value;
            memcpy(&value, &registers[instruction->left], sizeof(value));
            registers[instruction->destination] = (int64_t)value;
            ++pc; break;
        }
        case TURBOJS_IR_ADD_I32_CHECKED:
        case TURBOJS_IR_SUB_I32_CHECKED:
        case TURBOJS_IR_MUL_I32_CHECKED:
        case TURBOJS_IR_DIV_I32_CHECKED:
        case TURBOJS_IR_REM_I32_CHECKED: {
            int64_t left = registers[instruction->left], right = registers[instruction->right], value;
            if (instruction->opcode == TURBOJS_IR_DIV_I32_CHECKED || instruction->opcode == TURBOJS_IR_REM_I32_CHECKED) {
                if (right == 0 || (instruction->opcode == TURBOJS_IR_DIV_I32_CHECKED && left == INT32_MIN && right == -1))
                    return TURBOJS_IR_BAILOUT;
                value = instruction->opcode == TURBOJS_IR_DIV_I32_CHECKED ? left / right : left % right;
            } else {
                if (instruction->opcode == TURBOJS_IR_ADD_I32_CHECKED) value = left + right;
                else if (instruction->opcode == TURBOJS_IR_SUB_I32_CHECKED) value = left - right;
                else value = left * right;
                if (value < INT32_MIN || value > INT32_MAX) return TURBOJS_IR_BAILOUT;
            }
            registers[instruction->destination] = value; ++pc; break;
        }
        case TURBOJS_IR_CALL_NATIVE_I64:
        case TURBOJS_IR_CALL_NATIVE_F64: {
            const TurboJSClutchCallSite *site =
                (const TurboJSClutchCallSite *)(uintptr_t)instruction->immediate;
            int64_t call_arguments[TURBOJS_CLUTCH_MAX_ARGUMENTS];
            uint16_t call_index;
            TurboJSIRStatus status;
            if (!site || site->argument_count > TURBOJS_CLUTCH_MAX_ARGUMENTS)
                return TURBOJS_IR_BAILOUT;
            for (call_index = 0; call_index < site->argument_count; ++call_index) {
                uint16_t source = site->argument_registers[call_index];
                if (source >= function->register_count)
                    return TURBOJS_IR_BAILOUT;
                call_arguments[call_index] = registers[source];
            }
            if (instruction->opcode == TURBOJS_IR_CALL_NATIVE_F64) {
                double f64_arguments[TURBOJS_CLUTCH_MAX_ARGUMENTS];
                double f64_result;
                for (call_index = 0; call_index < site->argument_count; ++call_index)
                    memcpy(&f64_arguments[call_index], &call_arguments[call_index], sizeof(double));
                status = TurboJS_ClutchCallSiteInvokeF64(site, f64_arguments, &f64_result);
                if (status == TURBOJS_IR_OK)
                    memcpy(&registers[instruction->destination], &f64_result, sizeof(double));
            } else {
                status = TurboJS_ClutchCallSiteInvokeI64(
                    site, call_arguments, &registers[instruction->destination]);
            }
            if (status != TURBOJS_IR_OK) return TURBOJS_IR_BAILOUT;
            ++pc; break;
        }
        case TURBOJS_IR_RUNTIME_HELPER: return TURBOJS_IR_BAILOUT;
        case TURBOJS_IR_LESS_THAN_I64: registers[instruction->destination] = registers[instruction->left] < registers[instruction->right]; ++pc; break;
        case TURBOJS_IR_LOCAL_GET: registers[instruction->destination] = locals[(size_t)instruction->immediate]; ++pc; break;
        case TURBOJS_IR_LOCAL_SET: locals[(size_t)instruction->immediate] = registers[instruction->left]; ++pc; break;
        case TURBOJS_IR_JUMP: pc = instruction->target; break;
        case TURBOJS_IR_BRANCH_TRUE: pc = registers[instruction->left] ? instruction->target : pc + 1u; break;
        case TURBOJS_IR_BRANCH_FALSE: pc = !registers[instruction->left] ? instruction->target : pc + 1u; break;
        case TURBOJS_IR_RETURN_I64:
        case TURBOJS_IR_RETURN_F64: *result = registers[instruction->left]; return TURBOJS_IR_OK;
        default: return TURBOJS_IR_INVALID_OPCODE;
        }
    }
    return TURBOJS_IR_MISSING_RETURN;
}

TurboJSIRStatus TurboJS_IRExecute(const TurboJSIRFunction *function,
                                  const int64_t *arguments,
                                  size_t argument_count,
                                  int64_t *result)
{
    int64_t registers[TURBOJS_IR_MAX_REGISTERS] = {0};
    int64_t locals[TURBOJS_IR_MAX_REGISTERS] = {0};
    size_t pc;
    TurboJSIRDiagnostic diagnostic;
    TurboJSIRStatus status;
    if (!function || !result || (argument_count != 0 && !arguments) || argument_count < function->argument_count)
        return TURBOJS_IR_INVALID_ARGUMENT;
    status = TurboJS_IRVerify(function, &diagnostic);
    if (status != TURBOJS_IR_OK) return status;
    for (pc = 0; pc < function->instruction_count; ++pc) {
        const TurboJSIRInstruction *in = &function->instructions[pc];
        if (in->opcode == TURBOJS_IR_ARGUMENT) {
            registers[in->destination] = arguments[(size_t)in->immediate];
        } else {
            return execute_from(function, registers, locals, pc, result);
        }
    }
    return TURBOJS_IR_MISSING_RETURN;
}

TurboJSIRStatus TurboJS_IRResumeAfterBailout(const TurboJSIRFunction *function,
                                             const TurboJSDeoptFrame *frame,
                                             int64_t slow_path_result,
                                             int64_t *result)
{
    int64_t registers[TURBOJS_IR_MAX_REGISTERS] = {0};
    int64_t locals[TURBOJS_IR_MAX_REGISTERS] = {0};
    const TurboJSIRInstruction *failed;
    size_t i, resume_pc;
    TurboJSIRDiagnostic diagnostic;
    if (!function || !frame || !result || frame->bailout.reason == TURBOJS_BAILOUT_NONE ||
        frame->bailout.instruction_index >= function->instruction_count ||
        TurboJS_IRVerify(function, &diagnostic) != TURBOJS_IR_OK)
        return TURBOJS_IR_INVALID_ARGUMENT;
    if (frame->register_count != function->register_count || frame->local_count != function->local_count)
        return TURBOJS_IR_INVALID_ARGUMENT;
    for (i = 0; i < function->register_count; ++i)
        if (frame->materialized_register_mask & ((uint64_t)1u << i)) registers[i] = frame->register_values[i];
    for (i = 0; i < function->local_count; ++i)
        if (frame->materialized_local_mask & ((uint64_t)1u << i)) locals[i] = frame->local_values[i];
    failed = &function->instructions[frame->bailout.instruction_index];
    switch (failed->opcode) {
    case TURBOJS_IR_ADD_I32_CHECKED: case TURBOJS_IR_SUB_I32_CHECKED: case TURBOJS_IR_MUL_I32_CHECKED:
    case TURBOJS_IR_DIV_I32_CHECKED: case TURBOJS_IR_REM_I32_CHECKED:
    case TURBOJS_IR_RUNTIME_HELPER:
    case TURBOJS_IR_CALL_NATIVE_I64:
        registers[failed->destination] = slow_path_result;
        break;
    default:
        return TURBOJS_IR_UNSUPPORTED;
    }
    resume_pc = frame->bailout.instruction_index + 1u;
    return execute_from(function, registers, locals, resume_pc, result);
}

TurboJSIRStatus TurboJS_IRExecuteF64(const TurboJSIRFunction *function,
                                    const double *arguments,
                                    size_t argument_count,
                                    double *result)
{
    int64_t arg_bits[TURBOJS_IR_MAX_REGISTERS] = {0};
    int64_t result_bits = 0;
    size_t i;
    TurboJSIRStatus status;
    if (!result || argument_count > TURBOJS_IR_MAX_REGISTERS || (argument_count && !arguments))
        return TURBOJS_IR_INVALID_ARGUMENT;
    for (i = 0; i < argument_count; ++i) memcpy(&arg_bits[i], &arguments[i], sizeof(double));
    status = TurboJS_IRExecute(function, arg_bits, argument_count, &result_bits);
    if (status == TURBOJS_IR_OK) memcpy(result, &result_bits, sizeof(double));
    return status;
}
