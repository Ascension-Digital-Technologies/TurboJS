/*
 * TurboJS optimization policy and profiling foundation.
 *
 * This module deliberately contains no machine-code emitter. It provides the
 * stable policy boundary that the interpreter, baseline compiler, optimizing
 * compiler, and AOT pipeline can share without coupling public APIs to a
 * particular backend.
 */
#include <stdlib.h>
#include <string.h>

#include "optimization.h"

typedef struct TurboJSProfileEntry {
    uint64_t function_id;
    uint64_t call_count;
    TurboJSOptimizationTier requested_tier;
} TurboJSProfileEntry;

struct TurboJSProfiler {
    TurboJSOptimizationConfig config;
    TurboJSProfileEntry *entries;
    size_t count;
    size_t capacity;
};

TurboJSOptimizationConfig TurboJS_DefaultOptimizationConfig(void)
{
    TurboJSOptimizationConfig config;
    config.baseline_threshold = 100;
    config.optimizing_threshold = 1000;
    config.enable_profiling = 1;
#if defined(__x86_64__) || defined(_M_X64)
    config.enable_jit = 1;
#else
    config.enable_jit = 0;
#endif
    config.enable_aot = 1;
    config.reserved = 0;
    return config;
}

TurboJSOptimizationCapabilities TurboJS_GetOptimizationCapabilities(void)
{
    TurboJSOptimizationCapabilities capabilities = { 1, 1, 0, 0 };
#if defined(TURBOJS_ENABLE_BASELINE_JIT) || defined(__x86_64__) || defined(_M_X64)
    capabilities.baseline_jit = 1;
#endif
#ifdef TURBOJS_ENABLE_OPTIMIZING_JIT
    capabilities.optimizing_jit = 1;
#endif
    return capabilities;
}

const char *TurboJS_OptimizationTierName(TurboJSOptimizationTier tier)
{
    switch (tier) {
    case TURBOJS_TIER_INTERPRETER: return "interpreter";
    case TURBOJS_TIER_BASELINE: return "baseline";
    case TURBOJS_TIER_OPTIMIZING: return "optimizing";
    case TURBOJS_TIER_AOT: return "aot";
    default: return "unknown";
    }
}

TurboJSProfiler *TurboJS_ProfilerCreate(const TurboJSOptimizationConfig *config)
{
    TurboJSProfiler *profiler = calloc(1, sizeof(*profiler));
    if (!profiler)
        return NULL;
    profiler->config = config ? *config : TurboJS_DefaultOptimizationConfig();
    if (profiler->config.optimizing_threshold < profiler->config.baseline_threshold)
        profiler->config.optimizing_threshold = profiler->config.baseline_threshold;
    return profiler;
}

void TurboJS_ProfilerDestroy(TurboJSProfiler *profiler)
{
    if (!profiler)
        return;
    free(profiler->entries);
    free(profiler);
}

static TurboJSProfileEntry *find_entry(TurboJSProfiler *profiler, uint64_t function_id)
{
    size_t i;
    for (i = 0; i < profiler->count; ++i) {
        if (profiler->entries[i].function_id == function_id)
            return &profiler->entries[i];
    }
    return NULL;
}

static TurboJSProfileEntry *append_entry(TurboJSProfiler *profiler, uint64_t function_id)
{
    TurboJSProfileEntry *entries;
    size_t capacity;
    if (profiler->count == profiler->capacity) {
        capacity = profiler->capacity ? profiler->capacity * 2 : 32;
        entries = realloc(profiler->entries, capacity * sizeof(*entries));
        if (!entries)
            return NULL;
        profiler->entries = entries;
        profiler->capacity = capacity;
    }
    profiler->entries[profiler->count] = (TurboJSProfileEntry) {
        function_id, 0, TURBOJS_TIER_INTERPRETER
    };
    return &profiler->entries[profiler->count++];
}

TurboJSOptimizationTier TurboJS_ProfilerRecordCall(TurboJSProfiler *profiler,
                                                     uint64_t function_id)
{
    TurboJSProfileEntry *entry;
    if (!profiler || !profiler->config.enable_profiling)
        return TURBOJS_TIER_INTERPRETER;
    entry = find_entry(profiler, function_id);
    if (!entry)
        entry = append_entry(profiler, function_id);
    if (!entry)
        return TURBOJS_TIER_INTERPRETER;
    ++entry->call_count;
    if (profiler->config.enable_jit &&
        entry->call_count >= profiler->config.optimizing_threshold)
        entry->requested_tier = TURBOJS_TIER_OPTIMIZING;
    else if (profiler->config.enable_jit &&
             entry->call_count >= profiler->config.baseline_threshold)
        entry->requested_tier = TURBOJS_TIER_BASELINE;
    return entry->requested_tier;
}

int TurboJS_ProfilerQuery(const TurboJSProfiler *profiler,
                          uint64_t function_id,
                          TurboJSHotFunction *out)
{
    size_t i;
    if (!profiler || !out)
        return 0;
    for (i = 0; i < profiler->count; ++i) {
        if (profiler->entries[i].function_id == function_id) {
            out->function_id = function_id;
            out->call_count = profiler->entries[i].call_count;
            out->requested_tier = profiler->entries[i].requested_tier;
            return 1;
        }
    }
    return 0;
}

size_t TurboJS_ProfilerFunctionCount(const TurboJSProfiler *profiler)
{
    return profiler ? profiler->count : 0;
}

/* Runtime-owned JIT controls are implemented by private engine bridge calls. */
#include "internal/runtime_bridge.h"

void TurboJS_SetRuntimeJITThreshold(JSRuntime *rt, uint32_t threshold)
{
    turbojs_internal_set_jit_threshold(rt, threshold);
}

TurboJSRuntimeJITStats TurboJS_GetRuntimeJITStats(const JSRuntime *rt)
{
    return turbojs_internal_get_jit_stats(rt);
}

void TurboJS_ClearRuntimeJITCache(JSRuntime *rt)
{
    turbojs_internal_clear_jit_cache(rt);
}
