#include <stddef.h>
#include <string.h>
#include "jit.h"

TurboJSIRStatus TurboJS_SSAApplyPropertyFeedback(
    TurboJSSSAGraph *graph, const TurboJSPropertyFeedback *feedback,
    size_t feedback_count, uint32_t *applied_count)
{
    size_t i, j;
    uint32_t applied = 0;
    if (applied_count)
        *applied_count = 0;
    if (!graph || (!feedback && feedback_count != 0))
        return TURBOJS_IR_INVALID_ARGUMENT;
    for (i = 0; i < graph->value_count; ++i) {
        TurboJSSSAValue *value = &graph->values[i];
        uint16_t required;
        uint8_t case_count = 0;
        if (value->opcode == TURBOJS_SSA_PROPERTY_LOAD)
            required = TURBOJS_PROPERTY_FEEDBACK_LOAD;
        else if (value->opcode == TURBOJS_SSA_PROPERTY_STORE)
            required = TURBOJS_PROPERTY_FEEDBACK_STORE;
        else
            continue;
        value->property_case_count = 0;
        memset(value->property_shapes, 0, sizeof(value->property_shapes));
        memset(value->property_indices, 0, sizeof(value->property_indices));
        memset(value->property_generations, 0, sizeof(value->property_generations));
        memset(value->property_case_flags, 0, sizeof(value->property_case_flags));
        for (j = 0; j < feedback_count && case_count < TURBOJS_PROPERTY_PIC_MAX_CASES; ++j) {
            const TurboJSPropertyFeedback *entry = &feedback[j];
            uint8_t duplicate = 0, k;
            if (entry->source_instruction != value->source_instruction ||
                entry->atom != value->metadata ||
                (entry->flags & required) == 0 ||
                (entry->flags & TURBOJS_PROPERTY_FEEDBACK_OWN_DATA) == 0 ||
                entry->shape_identity == (uintptr_t)0)
                continue;
            if (value->opcode == TURBOJS_SSA_PROPERTY_STORE &&
                (entry->flags & TURBOJS_PROPERTY_FEEDBACK_WRITABLE) == 0)
                continue;
            for (k = 0; k < case_count; ++k) {
                if (value->property_shapes[k] == entry->shape_identity) {
                    duplicate = 1;
                    break;
                }
            }
            if (duplicate)
                continue;
            value->property_shapes[case_count] = entry->shape_identity;
            value->property_indices[case_count] = entry->property_index;
            value->property_generations[case_count] = entry->generation;
            value->property_case_flags[case_count] = entry->flags;
            ++case_count;
        }
        if (case_count) {
            value->property_case_count = case_count;
            value->guard_shape = value->property_shapes[0];
            value->property_index = value->property_indices[0];
            value->property_feedback_generation = value->property_generations[0];
            value->property_flags = value->property_case_flags[0];
            value->feedback_slot = 0;
            ++applied;
        }
    }
    if (applied_count)
        *applied_count = applied;
    return TURBOJS_IR_OK;
}
