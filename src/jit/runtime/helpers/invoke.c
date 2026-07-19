#include <string.h>

#include "internal.h"

static TurboJSIRStatus boxed_result_to_i64(const TurboJSBoxedValue *boxed,
                                           int64_t *value)
{
    if (!boxed || !value)
        return TURBOJS_IR_INVALID_ARGUMENT;
    switch (boxed->tag) {
    case TURBOJS_BOXED_INT32:
    case TURBOJS_BOXED_INT64:
    case TURBOJS_BOXED_BOOLEAN:
        *value = boxed->as.integer;
        return TURBOJS_IR_OK;
    default:
        return TURBOJS_IR_UNSUPPORTED;
    }
}

TurboJSIRStatus TurboJS_NativeInvokeWithRuntime(
    const TurboJSNativeFunction *native_function,
    const TurboJSIRFunction *ir_function,
    TurboJSRuntimeHelperTable *helpers,
    const int64_t *arguments,
    size_t argument_count,
    int64_t *result)
{
    TurboJSIRStatus status;
    TurboJSDeoptFrame native_frame;

    if (!native_function || !ir_function || !helpers || !result)
        return TURBOJS_IR_INVALID_ARGUMENT;
    status = TurboJS_NativeInvoke(native_function, arguments, argument_count, result);
    if (status != TURBOJS_IR_BAILOUT)
        return status;
    native_frame = TurboJS_NativeLastDeoptFrame(native_function);
    if (native_frame.bailout.reason != TURBOJS_BAILOUT_RUNTIME_HELPER)
        return status;
    return TurboJS_IRResumeWithRuntimeHelpers(
        ir_function, &native_frame, helpers, result);
}

TurboJSIRStatus TurboJS_RuntimeHelperInvokeAt(
    const TurboJSIRFunction *function,
    TurboJSRuntimeHelperTable *helpers,
    size_t instruction_index,
    int64_t *registers,
    int64_t *locals,
    const TurboJSValueKind *register_kinds,
    const TurboJSValueKind *local_kinds,
    int64_t *helper_value)
{
    TurboJSDeoptFrame frame;
    TurboJSBoxedDeoptFrame boxed;
    TurboJSBoxedValue boxed_result;
    TurboJSRuntimeHelperEntry *entry;
    const TurboJSIRInstruction *instruction;
    TurboJSIRStatus status;
    uint16_t helper_id;

    if (!function || !helpers || !registers || !locals || !helper_value ||
        instruction_index >= function->instruction_count)
        return TURBOJS_IR_INVALID_ARGUMENT;
    instruction = &function->instructions[instruction_index];
    if (instruction->opcode != TURBOJS_IR_RUNTIME_HELPER ||
        instruction->immediate < 0 ||
        instruction->immediate >= TURBOJS_RUNTIME_HELPER_LIMIT)
        return TURBOJS_IR_INVALID_ARGUMENT;

    helper_id = (uint16_t)instruction->immediate;
    entry = &helpers->entries[helper_id];
    if (!entry->callback) {
        helpers->missing_helpers++;
        return TURBOJS_IR_UNSUPPORTED;
    }

    memset(&frame, 0, sizeof(frame));
    frame.bailout.reason = TURBOJS_BAILOUT_RUNTIME_HELPER;
    frame.bailout.instruction_index = (uint32_t)instruction_index;
    frame.bailout.bytecode_offset = instruction->bytecode_offset;
    frame.register_count = function->register_count;
    frame.local_count = function->local_count;
    frame.register_values = registers;
    frame.local_values = locals;
    frame.register_kinds = register_kinds;
    frame.local_kinds = local_kinds;
    frame.materialized_register_mask =
        TurboJS_RuntimeHelperMaterializedMask(function->register_count);
    frame.materialized_local_mask =
        TurboJS_RuntimeHelperMaterializedMask(function->local_count);
    frame.live_register_mask = frame.materialized_register_mask;
    frame.live_local_mask = frame.materialized_local_mask;

    status = TurboJS_BoxDeoptFrameRooted(&frame, &helpers->rooting, &boxed);
    if (status != TURBOJS_IR_OK)
        return status;

    memset(&boxed_result, 0, sizeof(boxed_result));
    boxed_result.tag = TURBOJS_BOXED_UNDEFINED;
    helpers->calls++;
    status = entry->callback(entry->opaque, &boxed, instruction, &boxed_result);
    if (status == TURBOJS_IR_EXCEPTION)
        helpers->exceptions++;
    if (status == TURBOJS_IR_OK)
        status = boxed_result_to_i64(&boxed_result, helper_value);
    TurboJS_BoxedDeoptFrameDestroy(&boxed);
    return status;
}
