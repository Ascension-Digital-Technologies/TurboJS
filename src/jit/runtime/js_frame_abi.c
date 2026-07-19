#include <stdint.h>
#include "jit.h"

static size_t slot_offset(size_t slot)
{
    if (slot > SIZE_MAX / TURBOJS_JS_FRAME_SLOT_SIZE)
        return SIZE_MAX;
    return slot * TURBOJS_JS_FRAME_SLOT_SIZE;
}

TurboJSIRStatus TurboJS_JSFrameLayoutInit(
    TurboJSJSFrameLayout *layout,
    uint32_t argument_count,
    uint32_t local_count,
    uint32_t stack_capacity)
{
    uint64_t total_slots;
    uint64_t frame_size;
    if (!layout)
        return TURBOJS_IR_INVALID_ARGUMENT;

    total_slots = (uint64_t)TURBOJS_JS_FRAME_FIXED_SLOT_COUNT +
                  argument_count + local_count + stack_capacity;
    frame_size = total_slots * TURBOJS_JS_FRAME_SLOT_SIZE;
    if (total_slots > UINT32_MAX || frame_size > UINT32_MAX)
        return TURBOJS_IR_INVALID_ARGUMENT;

    layout->abi_version = TURBOJS_JS_FRAME_ABI_VERSION;
    layout->slot_size = TURBOJS_JS_FRAME_SLOT_SIZE;
    layout->fixed_slot_count = TURBOJS_JS_FRAME_FIXED_SLOT_COUNT;
    layout->argument_count = argument_count;
    layout->local_count = local_count;
    layout->stack_capacity = stack_capacity;
    layout->total_slots = (uint32_t)total_slots;
    layout->frame_size_bytes = (uint32_t)frame_size;
    return TURBOJS_IR_OK;
}

size_t TurboJS_JSFrameFixedSlotOffset(TurboJSJSFrameFixedSlot slot)
{
    if ((uint32_t)slot >= TURBOJS_JS_FRAME_FIXED_SLOT_COUNT)
        return SIZE_MAX;
    return slot_offset((size_t)slot);
}

size_t TurboJS_JSFrameArgumentOffset(
    const TurboJSJSFrameLayout *layout, uint32_t index)
{
    if (!layout || layout->abi_version != TURBOJS_JS_FRAME_ABI_VERSION ||
        index >= layout->argument_count)
        return SIZE_MAX;
    return slot_offset((size_t)layout->fixed_slot_count + index);
}

size_t TurboJS_JSFrameLocalOffset(
    const TurboJSJSFrameLayout *layout, uint32_t index)
{
    size_t slot;
    if (!layout || layout->abi_version != TURBOJS_JS_FRAME_ABI_VERSION ||
        index >= layout->local_count)
        return SIZE_MAX;
    slot = (size_t)layout->fixed_slot_count + layout->argument_count + index;
    return slot_offset(slot);
}

size_t TurboJS_JSFrameStackOffset(
    const TurboJSJSFrameLayout *layout, uint32_t index)
{
    size_t slot;
    if (!layout || layout->abi_version != TURBOJS_JS_FRAME_ABI_VERSION ||
        index >= layout->stack_capacity)
        return SIZE_MAX;
    slot = (size_t)layout->fixed_slot_count + layout->argument_count +
           layout->local_count + index;
    return slot_offset(slot);
}
