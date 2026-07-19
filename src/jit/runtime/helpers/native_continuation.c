#include <stdlib.h>
#include <string.h>

#include "internal.h"

static int is_straight_line_opcode(TurboJSIROpcode opcode)
{
    switch (opcode) {
    case TURBOJS_IR_NOP:
    case TURBOJS_IR_CONSTANT_I64:
    case TURBOJS_IR_CONSTANT_F64:
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
    case TURBOJS_IR_I64_TO_F64:
    case TURBOJS_IR_F64_TO_I64_TRUNC:
    case TURBOJS_IR_ADD_I32_CHECKED:
    case TURBOJS_IR_SUB_I32_CHECKED:
    case TURBOJS_IR_MUL_I32_CHECKED:
    case TURBOJS_IR_DIV_I32_CHECKED:
    case TURBOJS_IR_REM_I32_CHECKED:
    case TURBOJS_IR_LESS_THAN_I64:
    case TURBOJS_IR_LOCAL_GET:
    case TURBOJS_IR_LOCAL_SET:
    case TURBOJS_IR_CALL_NATIVE_I64:
    case TURBOJS_IR_CALL_NATIVE_F64:
    case TURBOJS_IR_RUNTIME_HELPER:
    case TURBOJS_IR_RETURN_I64:
    case TURBOJS_IR_RETURN_F64:
        return 1;
    default:
        return 0;
    }
}

TurboJSIRStatus TurboJS_RuntimeCompileContinuationSegment(
    const TurboJSIRFunction *source, size_t start_instruction,
    const int64_t *registers, const int64_t *locals,
    TurboJSNativeFunction **out_native, TurboJSIRFunction *out_ir,
    int64_t **out_arguments, size_t *out_prologue_count)
{
    TurboJSIRDiagnostic diagnostic;
    size_t i;
    uint16_t temp;
    TurboJSIRStatus status = TURBOJS_IR_OK;
    int64_t *arguments = NULL;

    if (!source || !registers || !locals || !out_native || !out_ir ||
        !out_arguments || !out_prologue_count ||
        start_instruction >= source->instruction_count)
        return TURBOJS_IR_INVALID_ARGUMENT;
    if ((size_t)source->register_count + source->local_count > TURBOJS_IR_MAX_REGISTERS)
        return TURBOJS_IR_UNSUPPORTED;

    for (i = start_instruction; i < source->instruction_count; ++i) {
        if (!is_straight_line_opcode(source->instructions[i].opcode))
            return TURBOJS_IR_UNSUPPORTED;
        if (source->instructions[i].opcode == TURBOJS_IR_RUNTIME_HELPER ||
            source->instructions[i].opcode == TURBOJS_IR_RETURN_I64 ||
            source->instructions[i].opcode == TURBOJS_IR_RETURN_F64)
            break;
    }
    if (i >= source->instruction_count)
        return TURBOJS_IR_MISSING_RETURN;

    TurboJS_IRFunctionInit(out_ir,
        (uint16_t)(source->register_count + source->local_count));
    TurboJS_IRFunctionSetLocalCount(out_ir, source->local_count);
    for (i = 0; i < source->register_count; ++i) {
        TurboJSIRInstruction in = {0};
        if (TurboJS_IRAllocateRegister(out_ir) == TURBOJS_IR_NO_REGISTER) {
            status = TURBOJS_IR_OUT_OF_MEMORY;
            goto fail;
        }
        in.opcode = TURBOJS_IR_ARGUMENT;
        in.destination = (uint16_t)i;
        in.immediate = (int64_t)i;
        if ((status = TurboJS_IREmit(out_ir, in)) != TURBOJS_IR_OK)
            goto fail;
        TurboJS_IRFunctionSetRegisterKind(out_ir, (uint16_t)i,
            TurboJS_IRFunctionRegisterKind(source, (uint16_t)i));
    }
    for (i = 0; i < source->local_count; ++i) {
        TurboJSIRInstruction arg = {0}, set = {0};
        temp = TurboJS_IRAllocateRegister(out_ir);
        if (temp == TURBOJS_IR_NO_REGISTER) {
            status = TURBOJS_IR_OUT_OF_MEMORY;
            goto fail;
        }
        arg.opcode = TURBOJS_IR_ARGUMENT;
        arg.destination = temp;
        arg.immediate = (int64_t)(source->register_count + i);
        set.opcode = TURBOJS_IR_LOCAL_SET;
        set.left = temp;
        set.immediate = (int64_t)i;
        if ((status = TurboJS_IREmit(out_ir, arg)) != TURBOJS_IR_OK ||
            (status = TurboJS_IREmit(out_ir, set)) != TURBOJS_IR_OK)
            goto fail;
        TurboJS_IRFunctionSetRegisterKind(out_ir, temp,
            TurboJS_IRFunctionLocalKind(source, (uint16_t)i));
        TurboJS_IRFunctionSetLocalKind(out_ir, (uint16_t)i,
            TurboJS_IRFunctionLocalKind(source, (uint16_t)i));
    }
    *out_prologue_count = out_ir->instruction_count;
    for (i = start_instruction; i < source->instruction_count; ++i) {
        TurboJSIRInstruction in = source->instructions[i];
        if ((status = TurboJS_IREmit(out_ir, in)) != TURBOJS_IR_OK)
            goto fail;
        if (in.opcode == TURBOJS_IR_RUNTIME_HELPER ||
            in.opcode == TURBOJS_IR_RETURN_I64 ||
            in.opcode == TURBOJS_IR_RETURN_F64) {
            if (in.opcode == TURBOJS_IR_RUNTIME_HELPER) {
                TurboJSIRInstruction unreachable = {0};
                unreachable.opcode = TURBOJS_IR_RETURN_I64;
                unreachable.left = in.destination;
                unreachable.bytecode_offset = in.bytecode_offset;
                if ((status = TurboJS_IREmit(out_ir, unreachable)) != TURBOJS_IR_OK)
                    goto fail;
            }
            break;
        }
    }

    arguments = calloc(out_ir->argument_count ? out_ir->argument_count : 1u,
                       sizeof(*arguments));
    if (!arguments) {
        status = TURBOJS_IR_OUT_OF_MEMORY;
        goto fail;
    }
    memcpy(arguments, registers, source->register_count * sizeof(*arguments));
    memcpy(arguments + source->register_count, locals,
           source->local_count * sizeof(*arguments));
    status = TurboJS_BaselineCompile(out_ir, out_native, &diagnostic);
    if (status != TURBOJS_IR_OK)
        goto fail;
    *out_arguments = arguments;
    return TURBOJS_IR_OK;

fail:
    free(arguments);
    TurboJS_IRFunctionDestroy(out_ir);
    return status;
}
