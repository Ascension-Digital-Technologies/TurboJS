#ifndef TURBOJS_INTERNAL_RUNTIME_BRIDGE_H
#define TURBOJS_INTERNAL_RUNTIME_BRIDGE_H

/*
 * Private bridge for the independently compiled runtime configuration facade.
 * Only public opaque TurboJS API types cross this boundary.
 */

#include <turbojs.h>

void *turbojs_internal_get_runtime_opaque(JSRuntime *rt);
void turbojs_internal_set_runtime_opaque(JSRuntime *rt, void *opaque);
void turbojs_internal_set_memory_limit(JSRuntime *rt, size_t limit);
void turbojs_internal_set_dump_flags(JSRuntime *rt, uint64_t flags);
uint64_t turbojs_internal_get_dump_flags(JSRuntime *rt);
size_t turbojs_internal_get_gc_threshold(JSRuntime *rt);
void turbojs_internal_set_gc_threshold(JSRuntime *rt, size_t gc_threshold);
void turbojs_internal_set_interrupt_handler(JSRuntime *rt,
                                        JSInterruptHandler *cb,
                                        void *opaque);
void turbojs_internal_set_can_block(JSRuntime *rt, bool can_block);
void turbojs_internal_set_shared_array_buffer_functions(
    JSRuntime *rt,
    const JSSharedArrayBufferFunctions *sf);
void turbojs_internal_set_max_stack_size(JSRuntime *rt, size_t stack_size);

/* Baseline JIT runtime controls. */
#include "optimization.h"
void turbojs_internal_set_jit_threshold(JSRuntime *rt, uint32_t threshold);
void turbojs_internal_set_optimization_config(
    JSRuntime *rt, const TurboJSOptimizationConfig *config);
TurboJSOptimizationConfig turbojs_internal_get_optimization_config(const JSRuntime *rt);
TurboJSRuntimeJITStats turbojs_internal_get_jit_stats(const JSRuntime *rt);
void turbojs_internal_reset_jit_stats(JSRuntime *rt);
void turbojs_internal_clear_jit_cache(JSRuntime *rt);

#endif
