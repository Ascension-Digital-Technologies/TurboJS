#include <string.h>

#include "jit.h"

void TurboJS_RuntimeHelperTableInit(TurboJSRuntimeHelperTable *table,
                                    const TurboJSRootingHooks *rooting)
{
    if (!table)
        return;
    memset(table, 0, sizeof(*table));
    if (rooting)
        table->rooting = *rooting;
}

TurboJSIRStatus TurboJS_RuntimeHelperRegister(TurboJSRuntimeHelperTable *table,
                                              uint16_t helper_id,
                                              TurboJSRuntimeHelperCallback callback,
                                              void *opaque)
{
    if (!table || !callback || helper_id >= TURBOJS_RUNTIME_HELPER_LIMIT)
        return TURBOJS_IR_INVALID_ARGUMENT;
    table->entries[helper_id].callback = callback;
    table->entries[helper_id].opaque = opaque;
    return TURBOJS_IR_OK;
}

void TurboJS_RuntimeHelperUnregister(TurboJSRuntimeHelperTable *table,
                                     uint16_t helper_id)
{
    if (!table || helper_id >= TURBOJS_RUNTIME_HELPER_LIMIT)
        return;
    memset(&table->entries[helper_id], 0, sizeof(table->entries[helper_id]));
}

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
    TurboJSBoxedDeoptFrame boxed_frame;
    TurboJSBoxedValue helper_result;
    const TurboJSIRInstruction *instruction;
    TurboJSRuntimeHelperEntry *entry;
    int64_t resumed_value;
    uint16_t helper_id;

    if (!native_function || !ir_function || !helpers || !result)
        return TURBOJS_IR_INVALID_ARGUMENT;

    status = TurboJS_NativeInvoke(native_function, arguments, argument_count, result);
    if (status != TURBOJS_IR_BAILOUT)
        return status;

    native_frame = TurboJS_NativeLastDeoptFrame(native_function);
    if (native_frame.bailout.reason != TURBOJS_BAILOUT_RUNTIME_HELPER ||
        native_frame.bailout.instruction_index >= ir_function->instruction_count)
        return status;

    instruction = &ir_function->instructions[native_frame.bailout.instruction_index];
    if (instruction->opcode != TURBOJS_IR_RUNTIME_HELPER ||
        instruction->immediate < 0 ||
        instruction->immediate >= TURBOJS_RUNTIME_HELPER_LIMIT) {
        helpers->missing_helpers++;
        return TURBOJS_IR_UNSUPPORTED;
    }

    helper_id = (uint16_t)instruction->immediate;
    entry = &helpers->entries[helper_id];
    if (!entry->callback) {
        helpers->missing_helpers++;
        return TURBOJS_IR_UNSUPPORTED;
    }

    status = TurboJS_BoxDeoptFrameRooted(&native_frame, &helpers->rooting, &boxed_frame);
    if (status != TURBOJS_IR_OK)
        return status;

    memset(&helper_result, 0, sizeof(helper_result));
    helper_result.tag = TURBOJS_BOXED_UNDEFINED;
    helpers->calls++;
    status = entry->callback(entry->opaque, &boxed_frame, instruction, &helper_result);
    if (status == TURBOJS_IR_EXCEPTION) {
        helpers->exceptions++;
        TurboJS_BoxedDeoptFrameDestroy(&boxed_frame);
        return status;
    }
    if (status == TURBOJS_IR_OK)
        status = boxed_result_to_i64(&helper_result, &resumed_value);
    if (status == TURBOJS_IR_OK)
        status = TurboJS_IRResumeAfterBailout(ir_function, &native_frame, resumed_value, result);

    TurboJS_BoxedDeoptFrameDestroy(&boxed_frame);
    return status;
}
