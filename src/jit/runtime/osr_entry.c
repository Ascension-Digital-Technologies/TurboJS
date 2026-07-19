#include "jit.h"
#include <stdlib.h>
#include <string.h>

static TurboJSIRStatus clone_frame(const TurboJSOSRFrame *src, TurboJSOSRFrame *dst) {
    TurboJSIRStatus st;
    memset(dst, 0, sizeof(*dst));
    st = TurboJS_OSRFrameValidate(src);
    if (st != TURBOJS_IR_OK) return st;
    st = TurboJS_OSRFrameInit(dst, src->local_count, src->stack_count);
    if (st != TURBOJS_IR_OK) return st;
    st = TurboJS_OSRFrameCapture(dst, src->locals, src->local_count,
                                 src->stack, src->stack_count,
                                 src->bytecode_offset, src->loop_header);
    if (st != TURBOJS_IR_OK) TurboJS_OSRFrameDestroy(dst);
    return st;
}

TurboJSIRStatus TurboJS_OSRExecuteEntry(TurboJSOSRState *state,
                                         const TurboJSOSREntry *entry,
                                         TurboJSOSRFrame *live_frame,
                                         TurboJSOSRExecutionResult *result) {
    TurboJSOSRFrame backup;
    TurboJSOSRExitKind exit_kind;
    uint32_t resume;
    TurboJSIRStatus st;
    if (!state || !entry || !entry->callback || !live_frame || !result)
        return TURBOJS_IR_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));
    st = TurboJS_OSRFrameValidate(live_frame);
    if (st != TURBOJS_IR_OK) return st;
    if (state->disabled || !state->code_ready ||
        live_frame->loop_header != entry->loop_header ||
        state->loop_header != entry->loop_header)
        return TURBOJS_IR_UNSUPPORTED;
    st = clone_frame(live_frame, &backup);
    if (st != TURBOJS_IR_OK) return st;
    resume = live_frame->bytecode_offset;
    exit_kind = entry->callback(live_frame, entry->opaque, &resume);
    result->exit_kind = exit_kind;
    result->resume_bytecode_offset = resume;
    if (exit_kind == TURBOJS_OSR_EXIT_COMPLETED) {
        TurboJS_OSRRecordEntry(state);
        live_frame->bytecode_offset = resume;
    } else {
        memcpy(live_frame->locals, backup.locals,
               (size_t)backup.local_count * sizeof(*backup.locals));
        memcpy(live_frame->stack, backup.stack,
               (size_t)backup.stack_count * sizeof(*backup.stack));
        live_frame->bytecode_offset = backup.bytecode_offset;
        live_frame->loop_header = backup.loop_header;
        result->resume_bytecode_offset = backup.bytecode_offset;
        result->restored_original_frame = 1;
        TurboJS_OSRRecordBailout(state, entry->bailout_limit);
    }
    TurboJS_OSRFrameDestroy(&backup);
    return exit_kind == TURBOJS_OSR_EXIT_ERROR ? TURBOJS_IR_EXCEPTION : TURBOJS_IR_OK;
}
