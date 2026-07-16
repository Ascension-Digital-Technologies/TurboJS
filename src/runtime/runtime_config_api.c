/*
 * Public runtime configuration facade.
 *
 * These stable API entry points compile independently from the engine core.
 * Stateful implementation remains behind a private opaque bridge while the
 * runtime structure is progressively extracted from the unity translation unit.
 */

#include <turbojs.h>
#include "internal/runtime_bridge.h"

void *JS_GetRuntimeOpaque(JSRuntime *rt)
{
    return turbojs_internal_get_runtime_opaque(rt);
}

void JS_SetRuntimeOpaque(JSRuntime *rt, void *opaque)
{
    turbojs_internal_set_runtime_opaque(rt, opaque);
}

void JS_SetMemoryLimit(JSRuntime *rt, size_t limit)
{
    turbojs_internal_set_memory_limit(rt, limit);
}

void JS_SetDumpFlags(JSRuntime *rt, uint64_t flags)
{
    turbojs_internal_set_dump_flags(rt, flags);
}

uint64_t JS_GetDumpFlags(JSRuntime *rt)
{
    return turbojs_internal_get_dump_flags(rt);
}

size_t JS_GetGCThreshold(JSRuntime *rt)
{
    return turbojs_internal_get_gc_threshold(rt);
}

void JS_SetGCThreshold(JSRuntime *rt, size_t gc_threshold)
{
    turbojs_internal_set_gc_threshold(rt, gc_threshold);
}

void JS_SetInterruptHandler(JSRuntime *rt, JSInterruptHandler *cb, void *opaque)
{
    turbojs_internal_set_interrupt_handler(rt, cb, opaque);
}

void JS_SetCanBlock(JSRuntime *rt, bool can_block)
{
    turbojs_internal_set_can_block(rt, can_block);
}

void JS_SetSharedArrayBufferFunctions(JSRuntime *rt,
                                      const JSSharedArrayBufferFunctions *sf)
{
    turbojs_internal_set_shared_array_buffer_functions(rt, sf);
}

void JS_SetMaxStackSize(JSRuntime *rt, size_t stack_size)
{
    turbojs_internal_set_max_stack_size(rt, stack_size);
}
