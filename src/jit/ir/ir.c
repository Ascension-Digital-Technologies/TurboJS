#include <stdlib.h>
#include <string.h>

#include "jit.h"

void TurboJS_IRFunctionInit(TurboJSIRFunction *function, uint16_t argument_count)
{
    if (!function)
        return;
    memset(function, 0, sizeof(*function));
    function->argument_count = argument_count;
}

void TurboJS_IRFunctionSetLocalCount(TurboJSIRFunction *function, uint16_t local_count)
{
    if (function)
        function->local_count = local_count;
}

void TurboJS_IRFunctionDestroy(TurboJSIRFunction *function)
{
    if (!function)
        return;
    free(function->instructions);
    memset(function, 0, sizeof(*function));
}

uint16_t TurboJS_IRAllocateRegister(TurboJSIRFunction *function)
{
    if (!function || function->register_count >= TURBOJS_IR_MAX_REGISTERS)
        return TURBOJS_IR_NO_REGISTER;
    return function->register_count++;
}

TurboJSIRStatus TurboJS_IREmit(TurboJSIRFunction *function,
                               TurboJSIRInstruction instruction)
{
    TurboJSIRInstruction *instructions;
    size_t capacity;

    if (!function)
        return TURBOJS_IR_INVALID_ARGUMENT;
    if (function->instruction_count == function->instruction_capacity) {
        capacity = function->instruction_capacity ? function->instruction_capacity * 2u : 16u;
        instructions = (TurboJSIRInstruction *)realloc(
            function->instructions, capacity * sizeof(*instructions));
        if (!instructions)
            return TURBOJS_IR_OUT_OF_MEMORY;
        function->instructions = instructions;
        function->instruction_capacity = capacity;
    }
    function->instructions[function->instruction_count++] = instruction;
    return TURBOJS_IR_OK;
}

const char *TurboJS_IRStatusName(TurboJSIRStatus status)
{
    switch (status) {
    case TURBOJS_IR_OK: return "ok";
    case TURBOJS_IR_INVALID_ARGUMENT: return "invalid-argument";
    case TURBOJS_IR_OUT_OF_MEMORY: return "out-of-memory";
    case TURBOJS_IR_INVALID_OPCODE: return "invalid-opcode";
    case TURBOJS_IR_INVALID_REGISTER: return "invalid-register";
    case TURBOJS_IR_INVALID_TARGET: return "invalid-target";
    case TURBOJS_IR_MISSING_RETURN: return "missing-return";
    case TURBOJS_IR_EXECUTION_LIMIT: return "execution-limit";
    case TURBOJS_IR_BAILOUT: return "bailout";
    case TURBOJS_IR_EXCEPTION: return "exception";
    case TURBOJS_IR_UNSUPPORTED: return "unsupported";
    default: return "unknown";
    }
}

const char *TurboJS_IROpcodeName(TurboJSIROpcode opcode)
{
    switch (opcode) {
    case TURBOJS_IR_NOP: return "nop";
    case TURBOJS_IR_ARGUMENT: return "argument";
    case TURBOJS_IR_CONSTANT_I64: return "constant.i64";
    case TURBOJS_IR_ADD_I64: return "add.i64";
    case TURBOJS_IR_SUB_I64: return "sub.i64";
    case TURBOJS_IR_MUL_I64: return "mul.i64";
    case TURBOJS_IR_ADD_I32_CHECKED: return "add.i32.checked";
    case TURBOJS_IR_SUB_I32_CHECKED: return "sub.i32.checked";
    case TURBOJS_IR_MUL_I32_CHECKED: return "mul.i32.checked";
    case TURBOJS_IR_DIV_I32_CHECKED: return "div.i32.checked";
    case TURBOJS_IR_REM_I32_CHECKED: return "rem.i32.checked";
    case TURBOJS_IR_RUNTIME_HELPER: return "runtime.helper";
    case TURBOJS_IR_LESS_THAN_I64: return "less-than.i64";
    case TURBOJS_IR_LOCAL_GET: return "local.get";
    case TURBOJS_IR_LOCAL_SET: return "local.set";
    case TURBOJS_IR_JUMP: return "jump";
    case TURBOJS_IR_BRANCH_TRUE: return "branch-true";
    case TURBOJS_IR_BRANCH_FALSE: return "branch-false";
    case TURBOJS_IR_RETURN_I64: return "return.i64";
    default: return "invalid";
    }
}
