#include <stdlib.h>
#include <string.h>

#include "internal.h"

static TurboJSIRStatus execute_clutch_call(const TurboJSIRInstruction *instruction,
                                           int64_t *registers)
{
    const TurboJSClutchCallSite *site =
        (const TurboJSClutchCallSite *)(uintptr_t)instruction->immediate;
    int64_t arguments[TURBOJS_CLUTCH_MAX_ARGUMENTS];

    if (!site || site->argument_count > TURBOJS_CLUTCH_MAX_ARGUMENTS)
        return TURBOJS_IR_BAILOUT;
    for (uint16_t i = 0; i < site->argument_count; ++i)
        arguments[i] = registers[site->argument_registers[i]];
    return TurboJS_ClutchCallSiteInvokeI64(
        site, arguments, &registers[instruction->destination]);
}


static TurboJSIRStatus try_native_continuation_segment(
    const TurboJSIRFunction *function,
    size_t *pc,
    int64_t *registers,
    int64_t *locals,
    TurboJSRuntimeHelperTable *helpers,
    int64_t *result)
{
    const TurboJSNativeFunction *native = NULL;
    TurboJSDeoptFrame frame;
    int64_t *arguments = NULL;
    int64_t segment_result = 0;
    size_t prologue_count = 0;
    TurboJSIRStatus status;

    status = TurboJS_RuntimeContinuationCacheAcquire(
        helpers, function, *pc, registers, locals, &native, &arguments,
        &prologue_count);
    if (status != TURBOJS_IR_OK) {
        helpers->native_continuation_fallbacks++;
        return TURBOJS_IR_UNSUPPORTED;
    }
    status = TurboJS_NativeInvoke(native, arguments,
                                  function->register_count + function->local_count,
                                  &segment_result);
    helpers->native_continuation_entries++;
    if (status == TURBOJS_IR_OK) {
        *result = segment_result;
    } else if (status == TURBOJS_IR_BAILOUT) {
        frame = TurboJS_NativeLastDeoptFrame(native);
        if (frame.bailout.reason == TURBOJS_BAILOUT_RUNTIME_HELPER &&
            frame.bailout.instruction_index >= prologue_count) {
            size_t source_offset = frame.bailout.instruction_index - prologue_count;
            if (*pc + source_offset < function->instruction_count) {
                for (size_t i = 0; i < function->register_count; ++i)
                    if (frame.materialized_register_mask & (UINT64_C(1) << i))
                        registers[i] = frame.register_values[i];
                for (size_t i = 0; i < function->local_count; ++i)
                    if (frame.materialized_local_mask & (UINT64_C(1) << i))
                        locals[i] = frame.local_values[i];
                *pc += source_offset;
            } else {
                status = TURBOJS_IR_INVALID_TARGET;
            }
        }
    }
    free(arguments);
    return status;
}

TurboJSIRStatus TurboJS_IRResumeWithRuntimeHelpers(
    const TurboJSIRFunction *function,
    const TurboJSDeoptFrame *native_frame,
    TurboJSRuntimeHelperTable *helpers,
    int64_t *result)
{
    int64_t *registers = NULL;
    int64_t *locals = NULL;
    size_t pc, steps = 0, step_limit;
    TurboJSIRDiagnostic diagnostic;
    TurboJSIRStatus status = TURBOJS_IR_INVALID_ARGUMENT;

    if (!function || !native_frame || !helpers || !result ||
        native_frame->bailout.reason != TURBOJS_BAILOUT_RUNTIME_HELPER ||
        native_frame->bailout.instruction_index >= function->instruction_count ||
        native_frame->register_count != function->register_count ||
        native_frame->local_count != function->local_count ||
        TurboJS_IRVerify(function, &diagnostic) != TURBOJS_IR_OK)
        return TURBOJS_IR_INVALID_ARGUMENT;

    registers = calloc(function->register_count ? function->register_count : 1u,
                       sizeof(*registers));
    locals = calloc(function->local_count ? function->local_count : 1u,
                    sizeof(*locals));
    if (!registers || !locals) {
        status = TURBOJS_IR_OUT_OF_MEMORY;
        goto done;
    }

    for (size_t i = 0; i < function->register_count; ++i)
        if (native_frame->materialized_register_mask & (UINT64_C(1) << i))
            registers[i] = native_frame->register_values[i];
    for (size_t i = 0; i < function->local_count; ++i)
        if (native_frame->materialized_local_mask & (UINT64_C(1) << i))
            locals[i] = native_frame->local_values[i];

    pc = native_frame->bailout.instruction_index;
    step_limit = function->instruction_count * 1000u + 1024u;
    while (pc < function->instruction_count) {
        const TurboJSIRInstruction *instruction = &function->instructions[pc];
        if (++steps > step_limit) {
            status = TURBOJS_IR_EXECUTION_LIMIT;
            goto done;
        }
        switch (instruction->opcode) {
        case TURBOJS_IR_NOP:
            ++pc;
            break;
        case TURBOJS_IR_CONSTANT_I64:
        case TURBOJS_IR_CONSTANT_F64:
            registers[instruction->destination] = instruction->immediate;
            ++pc;
            break;
        case TURBOJS_IR_ADD_I64:
            registers[instruction->destination] =
                registers[instruction->left] + registers[instruction->right];
            ++pc;
            break;
        case TURBOJS_IR_SUB_I64:
            registers[instruction->destination] =
                registers[instruction->left] - registers[instruction->right];
            ++pc;
            break;
        case TURBOJS_IR_MUL_I64:
            registers[instruction->destination] =
                registers[instruction->left] * registers[instruction->right];
            ++pc;
            break;
        case TURBOJS_IR_LESS_THAN_I64:
            registers[instruction->destination] =
                registers[instruction->left] < registers[instruction->right];
            ++pc;
            break;
        case TURBOJS_IR_LOCAL_GET:
            registers[instruction->destination] = locals[(size_t)instruction->immediate];
            ++pc;
            break;
        case TURBOJS_IR_LOCAL_SET:
            locals[(size_t)instruction->immediate] = registers[instruction->left];
            ++pc;
            break;
        case TURBOJS_IR_JUMP:
            pc = instruction->target;
            break;
        case TURBOJS_IR_BRANCH_TRUE:
            pc = registers[instruction->left] ? instruction->target : pc + 1u;
            break;
        case TURBOJS_IR_BRANCH_FALSE:
            pc = !registers[instruction->left] ? instruction->target : pc + 1u;
            break;
        case TURBOJS_IR_RUNTIME_HELPER: {
            int64_t helper_value = 0;
            status = TurboJS_RuntimeHelperInvokeAt(
                function, helpers, pc, registers, locals,
                native_frame->register_kinds, native_frame->local_kinds,
                &helper_value);
            if (status != TURBOJS_IR_OK)
                goto done;
            registers[instruction->destination] = helper_value;
            ++pc;
            status = try_native_continuation_segment(
                function, &pc, registers, locals, helpers, result);
            if (status == TURBOJS_IR_OK)
                goto done;
            if (status != TURBOJS_IR_UNSUPPORTED && status != TURBOJS_IR_BAILOUT)
                goto done;
            if (status == TURBOJS_IR_BAILOUT)
                break;
            status = TURBOJS_IR_OK;
            break;
        }
        case TURBOJS_IR_CALL_NATIVE_I64:
            status = execute_clutch_call(instruction, registers);
            if (status != TURBOJS_IR_OK)
                goto done;
            ++pc;
            break;
        case TURBOJS_IR_RETURN_I64:
        case TURBOJS_IR_RETURN_F64:
            *result = registers[instruction->left];
            status = TURBOJS_IR_OK;
            goto done;
        default:
            status = TURBOJS_IR_UNSUPPORTED;
            goto done;
        }
    }
    status = TURBOJS_IR_MISSING_RETURN;

done:
    free(locals);
    free(registers);
    return status;
}
