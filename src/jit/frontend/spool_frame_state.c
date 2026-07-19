#include <stdlib.h>
#include <string.h>

#include "jit.h"

static TurboJSBoxedValue undefined_value(void)
{
    TurboJSBoxedValue value;
    memset(&value, 0, sizeof(value));
    value.tag = TURBOJS_BOXED_UNDEFINED;
    return value;
}

TurboJSIRStatus TurboJS_SpoolFrameStateInit(TurboJSSpoolFrameState *state,
                                             uint32_t local_count,
                                             uint32_t stack_capacity)
{
    uint32_t i;
    if (!state)
        return TURBOJS_IR_INVALID_ARGUMENT;
    memset(state, 0, sizeof(*state));
    if (local_count) {
        state->locals = (TurboJSBoxedValue *)calloc(local_count, sizeof(*state->locals));
        if (!state->locals)
            return TURBOJS_IR_OUT_OF_MEMORY;
        for (i = 0; i < local_count; ++i)
            state->locals[i] = undefined_value();
    }
    if (stack_capacity) {
        state->stack = (TurboJSBoxedValue *)calloc(stack_capacity, sizeof(*state->stack));
        if (!state->stack) {
            free(state->locals);
            memset(state, 0, sizeof(*state));
            return TURBOJS_IR_OUT_OF_MEMORY;
        }
        for (i = 0; i < stack_capacity; ++i)
            state->stack[i] = undefined_value();
    }
    state->local_count = local_count;
    state->stack_capacity = stack_capacity;
    return TURBOJS_IR_OK;
}

void TurboJS_SpoolFrameStateDestroy(TurboJSSpoolFrameState *state)
{
    if (!state)
        return;
    free(state->locals);
    free(state->stack);
    memset(state, 0, sizeof(*state));
}

TurboJSIRStatus TurboJS_SpoolFrameStateClone(const TurboJSSpoolFrameState *source,
                                              TurboJSSpoolFrameState *destination)
{
    TurboJSIRStatus status;
    if (!source || !destination)
        return TURBOJS_IR_INVALID_ARGUMENT;
    status = TurboJS_SpoolFrameStateInit(destination, source->local_count,
                                         source->stack_capacity);
    if (status != TURBOJS_IR_OK)
        return status;
    if (source->local_count)
        memcpy(destination->locals, source->locals,
               source->local_count * sizeof(*source->locals));
    if (source->stack_count)
        memcpy(destination->stack, source->stack,
               source->stack_count * sizeof(*source->stack));
    destination->stack_count = source->stack_count;
    destination->bytecode_offset = source->bytecode_offset;
    return TURBOJS_IR_OK;
}

TurboJSIRStatus TurboJS_SpoolFrameStatePush(TurboJSSpoolFrameState *state,
                                             TurboJSBoxedValue value)
{
    TurboJSBoxedValue *stack;
    uint32_t capacity;
    if (!state)
        return TURBOJS_IR_INVALID_ARGUMENT;
    if (state->stack_count == state->stack_capacity) {
        capacity = state->stack_capacity ? state->stack_capacity * 2u : 8u;
        if (capacity < state->stack_capacity)
            return TURBOJS_IR_OUT_OF_MEMORY;
        stack = (TurboJSBoxedValue *)realloc(state->stack,
                                              capacity * sizeof(*stack));
        if (!stack)
            return TURBOJS_IR_OUT_OF_MEMORY;
        state->stack = stack;
        state->stack_capacity = capacity;
    }
    state->stack[state->stack_count++] = value;
    return TURBOJS_IR_OK;
}

TurboJSIRStatus TurboJS_SpoolFrameStatePop(TurboJSSpoolFrameState *state,
                                            TurboJSBoxedValue *value)
{
    if (!state || !value)
        return TURBOJS_IR_INVALID_ARGUMENT;
    if (!state->stack_count)
        return TURBOJS_IR_INVALID_REGISTER;
    *value = state->stack[--state->stack_count];
    state->stack[state->stack_count] = undefined_value();
    return TURBOJS_IR_OK;
}

TurboJSIRStatus TurboJS_SpoolFrameStateSetLocal(TurboJSSpoolFrameState *state,
                                                 uint32_t index,
                                                 TurboJSBoxedValue value)
{
    if (!state || index >= state->local_count)
        return TURBOJS_IR_INVALID_REGISTER;
    state->locals[index] = value;
    return TURBOJS_IR_OK;
}

TurboJSIRStatus TurboJS_SpoolFrameStateGetLocal(const TurboJSSpoolFrameState *state,
                                                 uint32_t index,
                                                 TurboJSBoxedValue *value)
{
    if (!state || !value || index >= state->local_count)
        return TURBOJS_IR_INVALID_REGISTER;
    *value = state->locals[index];
    return TURBOJS_IR_OK;
}

int TurboJS_SpoolFrameStateShapeCompatible(const TurboJSSpoolFrameState *left,
                                            const TurboJSSpoolFrameState *right)
{
    return left && right && left->local_count == right->local_count &&
           left->stack_count == right->stack_count;
}
