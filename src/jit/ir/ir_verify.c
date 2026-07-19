#include "jit.h"

static TurboJSIRStatus fail(TurboJSIRDiagnostic *diagnostic,
                            TurboJSIRStatus status,
                            size_t index,
                            const char *message)
{
    if (diagnostic) {
        diagnostic->status = status;
        diagnostic->instruction_index = index;
        diagnostic->message = message;
    }
    return status;
}

static int valid_register(const TurboJSIRFunction *function, uint16_t reg)
{
    return reg < function->register_count;
}

TurboJSIRStatus TurboJS_IRVerify(const TurboJSIRFunction *function,
                                 TurboJSIRDiagnostic *diagnostic)
{
    size_t index;
    int has_return = 0;

    if (!function || (!function->instructions && function->instruction_count != 0))
        return fail(diagnostic, TURBOJS_IR_INVALID_ARGUMENT, 0, "invalid function storage");
    if (function->register_count > TURBOJS_IR_MAX_REGISTERS)
        return fail(diagnostic, TURBOJS_IR_INVALID_REGISTER, 0, "register limit exceeded");

    for (index = 0; index < function->instruction_count; ++index) {
        const TurboJSIRInstruction *instruction = &function->instructions[index];
        switch (instruction->opcode) {
        case TURBOJS_IR_NOP:
            break;
        case TURBOJS_IR_ARGUMENT:
        case TURBOJS_IR_VALUE_ARGUMENT:
            if (!valid_register(function, instruction->destination))
                return fail(diagnostic, TURBOJS_IR_INVALID_REGISTER, index, "invalid destination register");
            if (instruction->immediate < 0 || (uint64_t)instruction->immediate >= function->argument_count)
                return fail(diagnostic, TURBOJS_IR_INVALID_ARGUMENT, index, "argument index out of range");
            break;
        case TURBOJS_IR_CONSTANT_I64:
        case TURBOJS_IR_CONSTANT_F64:
        case TURBOJS_IR_VALUE_UNDEFINED:
        case TURBOJS_IR_VALUE_CONSTANT_I32:
        case TURBOJS_IR_VALUE_CALLABLE_CONSTANT:
            if (!valid_register(function, instruction->destination))
                return fail(diagnostic, TURBOJS_IR_INVALID_REGISTER, index, "invalid destination register");
            break;
        case TURBOJS_IR_ADD_I64:
        case TURBOJS_IR_SUB_I64:
        case TURBOJS_IR_MUL_I64:
        case TURBOJS_IR_ADD_F64:
        case TURBOJS_IR_SUB_F64:
        case TURBOJS_IR_MUL_F64:
        case TURBOJS_IR_DIV_F64:
        case TURBOJS_IR_LESS_THAN_F64:
        case TURBOJS_IR_LESS_EQUAL_F64:
        case TURBOJS_IR_EQUAL_F64:
        case TURBOJS_IR_ADD_I32_CHECKED:
        case TURBOJS_IR_SUB_I32_CHECKED:
        case TURBOJS_IR_MUL_I32_CHECKED:
        case TURBOJS_IR_DIV_I32_CHECKED:
        case TURBOJS_IR_REM_I32_CHECKED:
        case TURBOJS_IR_LESS_THAN_I64:
            if (!valid_register(function, instruction->destination) ||
                !valid_register(function, instruction->left) ||
                !valid_register(function, instruction->right))
                return fail(diagnostic, TURBOJS_IR_INVALID_REGISTER, index, "invalid arithmetic register");
            break;
        case TURBOJS_IR_I64_TO_F64:
        case TURBOJS_IR_F64_TO_I64_TRUNC:
        case TURBOJS_IR_VALUE_MOVE:
        case TURBOJS_IR_VALUE_TO_BOOLEAN:
            if (!valid_register(function, instruction->destination) ||
                !valid_register(function, instruction->left))
                return fail(diagnostic, TURBOJS_IR_INVALID_REGISTER, index, "invalid conversion register");
            break;
        case TURBOJS_IR_VALUE_CALL_I64:
        case TURBOJS_IR_VALUE_CALL_F64:
            if (!valid_register(function, instruction->destination) ||
                !valid_register(function, instruction->left) ||
                instruction->immediate < 0 ||
                (uint64_t)instruction->immediate > TURBOJS_CLUTCH_MAX_ARGUMENTS ||
                ((uint64_t)instruction->right + (uint64_t)instruction->immediate) > function->register_count)
                return fail(diagnostic, TURBOJS_IR_INVALID_ARGUMENT, index, "invalid tagged callable invocation");
            break;
        case TURBOJS_IR_CALL_NATIVE_I64:
        case TURBOJS_IR_CALL_NATIVE_F64: {
            const TurboJSClutchCallSite *site =
                (const TurboJSClutchCallSite *)(uintptr_t)instruction->immediate;
            uint16_t call_index;
            if (!valid_register(function, instruction->destination) || !site ||
                site->argument_count > TURBOJS_CLUTCH_MAX_ARGUMENTS)
                return fail(diagnostic, TURBOJS_IR_INVALID_ARGUMENT, index, "invalid Clutch native call");
            for (call_index = 0; call_index < site->argument_count; ++call_index)
                if (!valid_register(function, site->argument_registers[call_index]))
                    return fail(diagnostic, TURBOJS_IR_INVALID_REGISTER, index, "invalid Clutch argument register");
            break;
        }
        case TURBOJS_IR_RUNTIME_HELPER:
            if (!valid_register(function, instruction->destination) ||
                !valid_register(function, instruction->left) ||
                !valid_register(function, instruction->right) || instruction->immediate < 0)
                return fail(diagnostic, TURBOJS_IR_INVALID_ARGUMENT, index, "invalid runtime helper call");
            break;
        case TURBOJS_IR_LOCAL_GET:
        case TURBOJS_IR_VALUE_LOCAL_GET:
            if (!valid_register(function, instruction->destination) || instruction->immediate < 0 ||
                (uint64_t)instruction->immediate >= function->local_count)
                return fail(diagnostic, TURBOJS_IR_INVALID_REGISTER, index, "invalid local load");
            break;
        case TURBOJS_IR_LOCAL_SET:
        case TURBOJS_IR_VALUE_LOCAL_SET:
            if (!valid_register(function, instruction->left) || instruction->immediate < 0 ||
                (uint64_t)instruction->immediate >= function->local_count)
                return fail(diagnostic, TURBOJS_IR_INVALID_REGISTER, index, "invalid local store");
            break;
        case TURBOJS_IR_JUMP:
            if (instruction->target >= function->instruction_count)
                return fail(diagnostic, TURBOJS_IR_INVALID_TARGET, index, "jump target out of range");
            break;
        case TURBOJS_IR_BRANCH_TRUE:
        case TURBOJS_IR_BRANCH_FALSE:
            if (!valid_register(function, instruction->left))
                return fail(diagnostic, TURBOJS_IR_INVALID_REGISTER, index, "invalid condition register");
            if (instruction->target >= function->instruction_count)
                return fail(diagnostic, TURBOJS_IR_INVALID_TARGET, index, "branch target out of range");
            break;
        case TURBOJS_IR_RETURN_I64:
        case TURBOJS_IR_RETURN_F64:
        case TURBOJS_IR_VALUE_RETURN:
            if (!valid_register(function, instruction->left))
                return fail(diagnostic, TURBOJS_IR_INVALID_REGISTER, index, "invalid return register");
            has_return = 1;
            break;
        default:
            return fail(diagnostic, TURBOJS_IR_INVALID_OPCODE, index, "unknown IR opcode");
        }
    }
    if (!has_return)
        return fail(diagnostic, TURBOJS_IR_MISSING_RETURN, function->instruction_count, "function has no return");
    if (diagnostic) {
        diagnostic->status = TURBOJS_IR_OK;
        diagnostic->instruction_index = 0;
        diagnostic->message = "ok";
    }
    return TURBOJS_IR_OK;
}
