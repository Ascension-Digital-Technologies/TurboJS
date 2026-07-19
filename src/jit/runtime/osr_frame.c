#include "jit.h"
#include <stdlib.h>
#include <string.h>

static int valid_kind(TurboJSOSRValueKind kind) {
    return kind >= TURBOJS_OSR_VALUE_EMPTY && kind <= TURBOJS_OSR_VALUE_REFERENCE;
}

TurboJSIRStatus TurboJS_OSRFrameInit(TurboJSOSRFrame *frame,
                                     uint32_t local_count,
                                     uint32_t stack_count) {
    if (!frame)
        return TURBOJS_IR_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    if (local_count) {
        frame->locals = calloc(local_count, sizeof(*frame->locals));
        if (!frame->locals)
            return TURBOJS_IR_OUT_OF_MEMORY;
    }
    if (stack_count) {
        frame->stack = calloc(stack_count, sizeof(*frame->stack));
        if (!frame->stack) {
            free(frame->locals);
            memset(frame, 0, sizeof(*frame));
            return TURBOJS_IR_OUT_OF_MEMORY;
        }
    }
    frame->local_count = local_count;
    frame->stack_count = stack_count;
    return TURBOJS_IR_OK;
}

void TurboJS_OSRFrameDestroy(TurboJSOSRFrame *frame) {
    if (!frame)
        return;
    free(frame->locals);
    free(frame->stack);
    memset(frame, 0, sizeof(*frame));
}

TurboJSIRStatus TurboJS_OSRFrameValidate(const TurboJSOSRFrame *frame) {
    uint32_t i;
    if (!frame)
        return TURBOJS_IR_INVALID_ARGUMENT;
    if ((frame->local_count && !frame->locals) || (frame->stack_count && !frame->stack))
        return TURBOJS_IR_INVALID_ARGUMENT;
    for (i = 0; i < frame->local_count; ++i)
        if (!valid_kind(frame->locals[i].kind))
            return TURBOJS_IR_INVALID_ARGUMENT;
    for (i = 0; i < frame->stack_count; ++i)
        if (!valid_kind(frame->stack[i].kind))
            return TURBOJS_IR_INVALID_ARGUMENT;
    return TURBOJS_IR_OK;
}

TurboJSIRStatus TurboJS_OSRFrameCapture(TurboJSOSRFrame *frame,
                                        const TurboJSOSRValue *locals,
                                        uint32_t local_count,
                                        const TurboJSOSRValue *stack,
                                        uint32_t stack_count,
                                        uint32_t bytecode_offset,
                                        uint32_t loop_header) {
    if (!frame || local_count != frame->local_count || stack_count != frame->stack_count ||
        (local_count && !locals) || (stack_count && !stack))
        return TURBOJS_IR_INVALID_ARGUMENT;
    if (local_count)
        memcpy(frame->locals, locals, local_count * sizeof(*locals));
    if (stack_count)
        memcpy(frame->stack, stack, stack_count * sizeof(*stack));
    frame->bytecode_offset = bytecode_offset;
    frame->loop_header = loop_header;
    return TurboJS_OSRFrameValidate(frame);
}

TurboJSIRStatus TurboJS_OSRFrameRestore(const TurboJSOSRFrame *frame,
                                        TurboJSOSRValue *locals,
                                        uint32_t local_capacity,
                                        TurboJSOSRValue *stack,
                                        uint32_t stack_capacity) {
    TurboJSIRStatus status = TurboJS_OSRFrameValidate(frame);
    if (status != TURBOJS_IR_OK)
        return status;
    if (local_capacity < frame->local_count || stack_capacity < frame->stack_count ||
        (frame->local_count && !locals) || (frame->stack_count && !stack))
        return TURBOJS_IR_INVALID_ARGUMENT;
    if (frame->local_count)
        memcpy(locals, frame->locals, frame->local_count * sizeof(*locals));
    if (frame->stack_count)
        memcpy(stack, frame->stack, frame->stack_count * sizeof(*stack));
    return TURBOJS_IR_OK;
}
