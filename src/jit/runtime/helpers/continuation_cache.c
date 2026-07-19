#include <stdlib.h>
#include <string.h>

#include "internal.h"

#define TURBOJS_CONTINUATION_CACHE_CAPACITY 16u

typedef struct TurboJSContinuationCacheEntry {
    uint64_t function_id;
    uint64_t function_revision;
    size_t start_instruction;
    size_t prologue_count;
    uint64_t last_use;
    TurboJSNativeFunction *native;
} TurboJSContinuationCacheEntry;

typedef struct TurboJSContinuationCache {
    TurboJSContinuationCacheEntry entries[TURBOJS_CONTINUATION_CACHE_CAPACITY];
    uint64_t clock;
} TurboJSContinuationCache;

static TurboJSContinuationCache *get_cache(TurboJSRuntimeHelperTable *helpers)
{
    TurboJSContinuationCache *cache;
    if (!helpers)
        return NULL;
    cache = (TurboJSContinuationCache *)helpers->native_continuation_cache;
    if (cache)
        return cache;
    cache = (TurboJSContinuationCache *)calloc(1, sizeof(*cache));
    if (!cache)
        return NULL;
    helpers->native_continuation_cache = cache;
    return cache;
}

static int64_t *build_arguments(const TurboJSIRFunction *source,
                                const int64_t *registers,
                                const int64_t *locals)
{
    size_t count = (size_t)source->register_count + source->local_count;
    int64_t *arguments = (int64_t *)calloc(count ? count : 1u, sizeof(*arguments));
    if (!arguments)
        return NULL;
    memcpy(arguments, registers, source->register_count * sizeof(*arguments));
    memcpy(arguments + source->register_count, locals,
           source->local_count * sizeof(*arguments));
    return arguments;
}

void TurboJS_RuntimeContinuationCacheDestroy(TurboJSRuntimeHelperTable *helpers)
{
    TurboJSContinuationCache *cache;
    if (!helpers)
        return;
    cache = (TurboJSContinuationCache *)helpers->native_continuation_cache;
    if (!cache)
        return;
    for (size_t i = 0; i < TURBOJS_CONTINUATION_CACHE_CAPACITY; ++i)
        TurboJS_NativeFunctionDestroy(cache->entries[i].native);
    free(cache);
    helpers->native_continuation_cache = NULL;
}

TurboJSIRStatus TurboJS_RuntimeContinuationCacheAcquire(
    TurboJSRuntimeHelperTable *helpers, const TurboJSIRFunction *source,
    size_t start_instruction, const int64_t *registers, const int64_t *locals,
    const TurboJSNativeFunction **out_native, int64_t **out_arguments,
    size_t *out_prologue_count)
{
    TurboJSContinuationCache *cache;
    TurboJSContinuationCacheEntry *entry = NULL;
    TurboJSContinuationCacheEntry *victim = NULL;
    TurboJSNativeFunction *native = NULL;
    TurboJSIRFunction segment;
    int64_t *compiled_arguments = NULL;
    int64_t *arguments = NULL;
    size_t prologue_count = 0;
    TurboJSIRStatus status;

    if (!helpers || !source || !registers || !locals || !out_native ||
        !out_arguments || !out_prologue_count)
        return TURBOJS_IR_INVALID_ARGUMENT;

    arguments = build_arguments(source, registers, locals);
    if (!arguments)
        return TURBOJS_IR_OUT_OF_MEMORY;

    if (helpers->continuation_vault) {
        const TurboJSNativeFunction *cached = TurboJS_CodeCacheLookupContinuation(
            helpers->continuation_vault, source->instance_id, source->revision,
            start_instruction, &prologue_count);
        if (cached) {
            helpers->native_continuation_cache_hits++;
            *out_native = cached;
            *out_arguments = arguments;
            *out_prologue_count = prologue_count;
            return TURBOJS_IR_OK;
        }

        memset(&segment, 0, sizeof(segment));
        status = TurboJS_RuntimeCompileContinuationSegment(
            source, start_instruction, registers, locals, &native, &segment,
            &compiled_arguments, &prologue_count);
        free(compiled_arguments);
        if (status != TURBOJS_IR_OK) {
            free(arguments);
            return status;
        }
        helpers->native_continuation_cache_misses++;
        helpers->native_continuation_compiles++;
        {
            TurboJSCodeCacheStats before =
                TurboJS_CodeCacheGetStats(helpers->continuation_vault);
            status = TurboJS_CodeCacheStoreContinuation(
                helpers->continuation_vault, source->instance_id, source->revision,
                start_instruction, prologue_count, native, out_native);
            if (status == TURBOJS_IR_OK) {
                TurboJSCodeCacheStats after =
                    TurboJS_CodeCacheGetStats(helpers->continuation_vault);
                helpers->native_continuation_cache_evictions +=
                    after.continuation_evictions - before.continuation_evictions;
            }
        }
        TurboJS_IRFunctionDestroy(&segment);
        if (status != TURBOJS_IR_OK) {
            free(arguments);
            return status;
        }
        *out_arguments = arguments;
        *out_prologue_count = prologue_count;
        return TURBOJS_IR_OK;
    }

    cache = get_cache(helpers);
    if (!cache)
        return TURBOJS_IR_OUT_OF_MEMORY;

    cache->clock++;
    for (size_t i = 0; i < TURBOJS_CONTINUATION_CACHE_CAPACITY; ++i) {
        TurboJSContinuationCacheEntry *candidate = &cache->entries[i];
        if (candidate->native && candidate->function_id == source->instance_id &&
            candidate->function_revision == source->revision &&
            candidate->start_instruction == start_instruction) {
            entry = candidate;
            break;
        }
        if (!victim || !candidate->native || candidate->last_use < victim->last_use)
            victim = candidate;
    }

    if (entry) {
        entry->last_use = cache->clock;
        helpers->native_continuation_cache_hits++;
        *out_native = entry->native;
        *out_arguments = arguments;
        *out_prologue_count = entry->prologue_count;
        return TURBOJS_IR_OK;
    }

    memset(&segment, 0, sizeof(segment));
    status = TurboJS_RuntimeCompileContinuationSegment(
        source, start_instruction, registers, locals, &native, &segment,
        &compiled_arguments, &prologue_count);
    free(compiled_arguments);
    if (status != TURBOJS_IR_OK) {
        free(arguments);
        return status;
    }

    helpers->native_continuation_cache_misses++;
    helpers->native_continuation_compiles++;
    if (victim->native) {
        TurboJS_NativeFunctionDestroy(victim->native);
        helpers->native_continuation_cache_evictions++;
    }
    victim->function_id = source->instance_id;
    victim->function_revision = source->revision;
    victim->start_instruction = start_instruction;
    victim->prologue_count = prologue_count;
    victim->last_use = cache->clock;
    victim->native = native;
    TurboJS_IRFunctionDestroy(&segment);

    *out_native = native;
    *out_arguments = arguments;
    *out_prologue_count = prologue_count;
    return TURBOJS_IR_OK;
}
