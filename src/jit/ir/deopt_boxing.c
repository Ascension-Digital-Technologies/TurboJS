#include <stdlib.h>
#include <string.h>

#include "jit.h"

static TurboJSBoxedValue box_value(int64_t value, TurboJSValueKind kind,
                                   const TurboJSRootingHooks *rooting,
                                   int *rooted)
{
    TurboJSBoxedValue boxed;
    memset(&boxed, 0, sizeof(boxed));
    *rooted = 0;
    switch (kind) {
    case TURBOJS_VALUE_I32:
        boxed.tag = TURBOJS_BOXED_INT32;
        boxed.as.integer = (int32_t)value;
        break;
    case TURBOJS_VALUE_BOOLEAN:
        boxed.tag = TURBOJS_BOXED_BOOLEAN;
        boxed.as.integer = value != 0;
        break;
    case TURBOJS_VALUE_I64:
        boxed.tag = TURBOJS_BOXED_INT64;
        boxed.as.integer = value;
        break;
    case TURBOJS_VALUE_F64:
        boxed.tag = TURBOJS_BOXED_FLOAT64;
        memcpy(&boxed.as.number, &value, sizeof(value));
        break;
    case TURBOJS_VALUE_HEAP_REFERENCE:
        boxed.tag = TURBOJS_BOXED_HEAP_REFERENCE;
        boxed.as.reference = (void *)(uintptr_t)(uint64_t)value;
        if (boxed.as.reference && rooting && rooting->retain && rooting->release) {
            boxed.as.reference = rooting->retain(rooting->opaque, boxed.as.reference);
            if (boxed.as.reference)
                *rooted = 1;
            else
                boxed.tag = TURBOJS_BOXED_UNDEFINED;
        } else if (boxed.as.reference) {
            /* Heap references may not escape a native frame without roots. */
            boxed.tag = TURBOJS_BOXED_UNDEFINED;
            boxed.as.reference = NULL;
        }
        break;
    default:
        boxed.tag = TURBOJS_BOXED_UNDEFINED;
        break;
    }
    return boxed;
}

TurboJSIRStatus TurboJS_BoxDeoptFrameRooted(const TurboJSDeoptFrame *native_frame,
                                            const TurboJSRootingHooks *rooting,
                                            TurboJSBoxedDeoptFrame *boxed_frame)
{
    size_t i;
    if (!native_frame || !boxed_frame)
        return TURBOJS_IR_INVALID_ARGUMENT;
    if (rooting && ((rooting->retain == NULL) != (rooting->release == NULL)))
        return TURBOJS_IR_INVALID_ARGUMENT;
    memset(boxed_frame, 0, sizeof(*boxed_frame));
    boxed_frame->bailout = native_frame->bailout;
    boxed_frame->register_count = native_frame->register_count;
    boxed_frame->local_count = native_frame->local_count;
    boxed_frame->stack_count = native_frame->stack_count;
    boxed_frame->live_register_mask = native_frame->live_register_mask;
    boxed_frame->live_local_mask = native_frame->live_local_mask;
    if (rooting)
        boxed_frame->rooting = *rooting;

    if (boxed_frame->register_count) {
        boxed_frame->registers = calloc(boxed_frame->register_count, sizeof(*boxed_frame->registers));
        if (!boxed_frame->registers)
            goto oom;
    }
    if (boxed_frame->local_count) {
        boxed_frame->locals = calloc(boxed_frame->local_count, sizeof(*boxed_frame->locals));
        if (!boxed_frame->locals)
            goto oom;
    }
    if (boxed_frame->stack_count) {
        boxed_frame->stack = calloc(boxed_frame->stack_count, sizeof(*boxed_frame->stack));
        if (!boxed_frame->stack)
            goto oom;
    }

    for (i = 0; i < boxed_frame->register_count; ++i) {
        uint64_t bit = (uint64_t)1u << i;
        int rooted = 0;
        if ((native_frame->materialized_register_mask & bit) != 0) {
            boxed_frame->registers[i] = box_value(native_frame->register_values[i], native_frame->register_kinds[i], rooting, &rooted);
            if (rooted)
                boxed_frame->reference_register_mask |= bit;
        }
    }
    for (i = 0; i < boxed_frame->local_count; ++i) {
        uint64_t bit = (uint64_t)1u << i;
        int rooted = 0;
        if ((native_frame->materialized_local_mask & bit) != 0) {
            boxed_frame->locals[i] = box_value(native_frame->local_values[i], native_frame->local_kinds[i], rooting, &rooted);
            if (rooted)
                boxed_frame->reference_local_mask |= bit;
        }
    }
    return TURBOJS_IR_OK;

oom:
    TurboJS_BoxedDeoptFrameDestroy(boxed_frame);
    return TURBOJS_IR_OUT_OF_MEMORY;
}

TurboJSIRStatus TurboJS_BoxDeoptFrame(const TurboJSDeoptFrame *native_frame,
                                      TurboJSBoxedDeoptFrame *boxed_frame)
{
    return TurboJS_BoxDeoptFrameRooted(native_frame, NULL, boxed_frame);
}

void TurboJS_BoxedDeoptFrameDestroy(TurboJSBoxedDeoptFrame *frame)
{
    size_t i;
    if (!frame)
        return;
    if (frame->rooting.release) {
        for (i = 0; i < frame->register_count; ++i) {
            uint64_t bit = (uint64_t)1u << i;
            if ((frame->reference_register_mask & bit) != 0)
                frame->rooting.release(frame->rooting.opaque, frame->registers[i].as.reference);
        }
        for (i = 0; i < frame->local_count; ++i) {
            uint64_t bit = (uint64_t)1u << i;
            if ((frame->reference_local_mask & bit) != 0)
                frame->rooting.release(frame->rooting.opaque, frame->locals[i].as.reference);
        }
    }
    free(frame->registers);
    free(frame->locals);
    free(frame->stack);
    memset(frame, 0, sizeof(*frame));
}

TurboJSIRStatus TurboJS_IRResumeWithSlowPath(const TurboJSIRFunction *function,
                                             const TurboJSDeoptFrame *native_frame,
                                             TurboJSSlowPathCallback slow_path,
                                             void *opaque,
                                             int64_t *result)
{
    TurboJSBoxedDeoptFrame boxed;
    TurboJSBoxedValue slow_result;
    TurboJSIRStatus status;
    const TurboJSIRInstruction *failed;
    int64_t value;

    if (!function || !native_frame || !slow_path || !result ||
        native_frame->bailout.instruction_index >= function->instruction_count)
        return TURBOJS_IR_INVALID_ARGUMENT;

    status = TurboJS_BoxDeoptFrame(native_frame, &boxed);
    if (status != TURBOJS_IR_OK)
        return status;
    failed = &function->instructions[native_frame->bailout.instruction_index];
    memset(&slow_result, 0, sizeof(slow_result));
    slow_result.tag = TURBOJS_BOXED_UNDEFINED;
    status = slow_path(opaque, &boxed, failed, &slow_result);
    if (status == TURBOJS_IR_OK) {
        switch (slow_result.tag) {
        case TURBOJS_BOXED_INT32:
        case TURBOJS_BOXED_INT64:
        case TURBOJS_BOXED_BOOLEAN:
            value = slow_result.as.integer;
            status = TurboJS_IRResumeAfterBailout(function, native_frame, value, result);
            break;
        default:
            status = TURBOJS_IR_UNSUPPORTED;
            break;
        }
    }
    TurboJS_BoxedDeoptFrameDestroy(&boxed);
    return status;
}
