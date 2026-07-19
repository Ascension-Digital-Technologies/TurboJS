#include <stdlib.h>
#include <string.h>

#include "jit.h"

static TurboJSBoxedValue tagged_undefined(void)
{
    TurboJSBoxedValue value;
    memset(&value, 0, sizeof(value));
    value.tag = TURBOJS_BOXED_UNDEFINED;
    return value;
}

static int tagged_truthy(const TurboJSBoxedValue *value)
{
    if (!value)
        return 0;
    switch (value->tag) {
    case TURBOJS_BOXED_UNDEFINED:
        return 0;
    case TURBOJS_BOXED_BOOLEAN:
    case TURBOJS_BOXED_INT32:
    case TURBOJS_BOXED_INT64:
        return value->as.integer != 0;
    case TURBOJS_BOXED_FLOAT64:
        return value->as.number != 0.0 && value->as.number == value->as.number;
    case TURBOJS_BOXED_HEAP_REFERENCE:
        return value->as.reference != NULL;
    case TURBOJS_BOXED_CALLABLE_REFERENCE:
        return value->as.callable != NULL && TurboJS_CallableReferenceIsLive(value->as.callable);
    default:
        return 0;
    }
}

TurboJSIRStatus TurboJS_IRExecuteTagged(const TurboJSIRFunction *function,
                                         const TurboJSBoxedValue *arguments,
                                         size_t argument_count,
                                         TurboJSBoxedValue *result)
{
    TurboJSBoxedValue *registers = NULL;
    TurboJSBoxedValue *locals = NULL;
    size_t pc = 0, steps = 0, step_limit;
    uint16_t i;
    TurboJSIRDiagnostic diagnostic;
    TurboJSIRStatus status = TURBOJS_IR_OK;

    if (!function || !result || argument_count != function->argument_count ||
        (argument_count && !arguments))
        return TURBOJS_IR_INVALID_ARGUMENT;
    if (TurboJS_IRVerify(function, &diagnostic) != TURBOJS_IR_OK)
        return diagnostic.status;

    registers = (TurboJSBoxedValue *)calloc(function->register_count ? function->register_count : 1u,
                                             sizeof(*registers));
    locals = (TurboJSBoxedValue *)calloc(function->local_count ? function->local_count : 1u,
                                          sizeof(*locals));
    if (!registers || !locals) {
        status = TURBOJS_IR_OUT_OF_MEMORY;
        goto done;
    }
    for (i = 0; i < function->register_count; ++i)
        registers[i] = tagged_undefined();
    for (i = 0; i < function->local_count; ++i)
        locals[i] = tagged_undefined();

    step_limit = function->instruction_count * 1000u + 1024u;
    while (pc < function->instruction_count) {
        const TurboJSIRInstruction *ins = &function->instructions[pc];
        if (++steps > step_limit) {
            status = TURBOJS_IR_EXECUTION_LIMIT;
            goto done;
        }
        switch (ins->opcode) {
        case TURBOJS_IR_NOP:
            ++pc;
            break;
        case TURBOJS_IR_VALUE_ARGUMENT:
            registers[ins->destination] = arguments[(size_t)ins->immediate];
            ++pc;
            break;
        case TURBOJS_IR_VALUE_UNDEFINED:
            registers[ins->destination] = tagged_undefined();
            ++pc;
            break;
        case TURBOJS_IR_VALUE_CONSTANT_I32:
            registers[ins->destination].tag = TURBOJS_BOXED_INT32;
            registers[ins->destination].as.integer = (int32_t)ins->immediate;
            ++pc;
            break;
        case TURBOJS_IR_VALUE_MOVE:
            registers[ins->destination] = registers[ins->left];
            ++pc;
            break;
        case TURBOJS_IR_VALUE_LOCAL_GET:
            registers[ins->destination] = locals[(size_t)ins->immediate];
            ++pc;
            break;
        case TURBOJS_IR_VALUE_LOCAL_SET:
            locals[(size_t)ins->immediate] = registers[ins->left];
            ++pc;
            break;
        case TURBOJS_IR_VALUE_TO_BOOLEAN:
            registers[ins->destination].tag = TURBOJS_BOXED_BOOLEAN;
            registers[ins->destination].as.integer = tagged_truthy(&registers[ins->left]);
            ++pc;
            break;
        case TURBOJS_IR_VALUE_CALLABLE_CONSTANT:
            registers[ins->destination].tag = TURBOJS_BOXED_CALLABLE_REFERENCE;
            registers[ins->destination].as.callable =
                (const TurboJSCallableReference *)(uintptr_t)ins->immediate;
            ++pc;
            break;
        case TURBOJS_IR_VALUE_CALL_I64: {
            const TurboJSCallableReference *callable = registers[ins->left].as.callable;
            int64_t arguments[TURBOJS_CLUTCH_MAX_ARGUMENTS] = {0};
            int64_t native_result = 0;
            size_t argc = (size_t)ins->immediate;
            size_t first = (size_t)ins->right;
            size_t ai;
            if (registers[ins->left].tag != TURBOJS_BOXED_CALLABLE_REFERENCE ||
                !callable || argc > TURBOJS_CLUTCH_MAX_ARGUMENTS ||
                first + argc > function->register_count) { status = TURBOJS_IR_UNSUPPORTED; goto done; }
            for (ai = 0; ai < argc; ++ai) {
                TurboJSBoxedValue *arg = &registers[first + ai];
                if (arg->tag != TURBOJS_BOXED_INT32 && arg->tag != TURBOJS_BOXED_INT64 &&
                    arg->tag != TURBOJS_BOXED_BOOLEAN) { status = TURBOJS_IR_UNSUPPORTED; goto done; }
                arguments[ai] = arg->as.integer;
            }
            status = TurboJS_CallableReferenceInvokeI64(callable, arguments, argc, &native_result);
            if (status != TURBOJS_IR_OK) goto done;
            registers[ins->destination].tag = TURBOJS_BOXED_INT64;
            registers[ins->destination].as.integer = native_result;
            ++pc;
            break;
        }
        case TURBOJS_IR_VALUE_CALL_F64: {
            const TurboJSCallableReference *callable = registers[ins->left].as.callable;
            double arguments[TURBOJS_CLUTCH_MAX_ARGUMENTS] = {0};
            double native_result = 0.0;
            size_t argc = (size_t)ins->immediate;
            size_t first = (size_t)ins->right;
            size_t ai;
            if (registers[ins->left].tag != TURBOJS_BOXED_CALLABLE_REFERENCE ||
                !callable || argc > TURBOJS_CLUTCH_MAX_ARGUMENTS ||
                first + argc > function->register_count) { status = TURBOJS_IR_UNSUPPORTED; goto done; }
            for (ai = 0; ai < argc; ++ai) {
                TurboJSBoxedValue *arg = &registers[first + ai];
                if (arg->tag == TURBOJS_BOXED_FLOAT64) arguments[ai] = arg->as.number;
                else if (arg->tag == TURBOJS_BOXED_INT32 || arg->tag == TURBOJS_BOXED_INT64) arguments[ai] = (double)arg->as.integer;
                else { status = TURBOJS_IR_UNSUPPORTED; goto done; }
            }
            status = TurboJS_CallableReferenceInvokeF64(callable, arguments, argc, &native_result);
            if (status != TURBOJS_IR_OK) goto done;
            registers[ins->destination].tag = TURBOJS_BOXED_FLOAT64;
            registers[ins->destination].as.number = native_result;
            ++pc;
            break;
        }
        case TURBOJS_IR_JUMP:
            pc = ins->target;
            break;
        case TURBOJS_IR_BRANCH_TRUE:
            pc = tagged_truthy(&registers[ins->left]) ? ins->target : pc + 1u;
            break;
        case TURBOJS_IR_BRANCH_FALSE:
            pc = tagged_truthy(&registers[ins->left]) ? pc + 1u : ins->target;
            break;
        case TURBOJS_IR_VALUE_RETURN:
            *result = registers[ins->left];
            goto done;
        default:
            status = TURBOJS_IR_UNSUPPORTED;
            goto done;
        }
    }
    status = TURBOJS_IR_MISSING_RETURN;

done:
    free(registers);
    free(locals);
    return status;
}
