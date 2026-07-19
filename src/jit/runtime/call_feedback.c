#include <limits.h>
#include <string.h>
#include "jit.h"

static uint32_t saturating_increment_u32(uint32_t value)
{
    return value == UINT32_MAX ? value : value + 1u;
}

static uint64_t saturating_increment_u64(uint64_t value)
{
    return value == UINT64_MAX ? value : value + 1u;
}

void TurboJS_CallFeedbackInit(TurboJSCallFeedbackSlot *slot)
{
    if (!slot)
        return;
    memset(slot, 0, sizeof(*slot));
    slot->state = TURBOJS_CALL_FEEDBACK_UNINITIALIZED;
}

TurboJSCallFeedbackState TurboJS_CallFeedbackObserve(
    TurboJSCallFeedbackSlot *slot, uint64_t target_identity)
{
    uint32_t i;
    TurboJSCallFeedbackState old_state;
    if (!slot)
        return TURBOJS_CALL_FEEDBACK_UNINITIALIZED;

    slot->observations = saturating_increment_u64(slot->observations);
    if (target_identity == 0) {
        slot->misses = saturating_increment_u32(slot->misses);
        return (TurboJSCallFeedbackState)slot->state;
    }

    old_state = (TurboJSCallFeedbackState)slot->state;
    for (i = 0; i < slot->target_count; ++i) {
        if (slot->targets[i].target_identity == target_identity) {
            slot->targets[i].hits = saturating_increment_u32(slot->targets[i].hits);
            return (TurboJSCallFeedbackState)slot->state;
        }
    }

    slot->misses = saturating_increment_u32(slot->misses);
    if (slot->state == TURBOJS_CALL_FEEDBACK_MEGAMORPHIC)
        return TURBOJS_CALL_FEEDBACK_MEGAMORPHIC;

    if (slot->target_count < TURBOJS_CALL_FEEDBACK_MAX_TARGETS) {
        TurboJSCallFeedbackTarget *target = &slot->targets[slot->target_count++];
        target->target_identity = target_identity;
        target->hits = 1u;
        slot->state = slot->target_count == 1u
            ? TURBOJS_CALL_FEEDBACK_MONOMORPHIC
            : TURBOJS_CALL_FEEDBACK_POLYMORPHIC;
    } else {
        slot->state = TURBOJS_CALL_FEEDBACK_MEGAMORPHIC;
    }

    if ((TurboJSCallFeedbackState)slot->state != old_state)
        slot->generation = saturating_increment_u32(slot->generation);
    return (TurboJSCallFeedbackState)slot->state;
}

TurboJSCallFeedbackState TurboJS_CallFeedbackGetState(
    const TurboJSCallFeedbackSlot *slot)
{
    if (!slot)
        return TURBOJS_CALL_FEEDBACK_UNINITIALIZED;
    return (TurboJSCallFeedbackState)slot->state;
}

const char *TurboJS_CallFeedbackStateName(TurboJSCallFeedbackState state)
{
    switch (state) {
    case TURBOJS_CALL_FEEDBACK_UNINITIALIZED: return "uninitialized";
    case TURBOJS_CALL_FEEDBACK_MONOMORPHIC: return "monomorphic";
    case TURBOJS_CALL_FEEDBACK_POLYMORPHIC: return "polymorphic";
    case TURBOJS_CALL_FEEDBACK_MEGAMORPHIC: return "megamorphic";
    default: return "unknown";
    }
}

int TurboJS_CallFeedbackMonomorphicTarget(
    const TurboJSCallFeedbackSlot *slot,
    uint32_t minimum_hits,
    uint64_t *target_identity)
{
    if (!slot || !target_identity ||
        slot->state != TURBOJS_CALL_FEEDBACK_MONOMORPHIC ||
        slot->target_count != 1u ||
        slot->targets[0].hits < minimum_hits)
        return 0;
    *target_identity = slot->targets[0].target_identity;
    return 1;
}
