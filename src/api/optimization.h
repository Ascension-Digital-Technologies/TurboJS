#ifndef TURBOJS_OPTIMIZATION_H
#define TURBOJS_OPTIMIZATION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum TurboJSOptimizationTier {
    TURBOJS_TIER_INTERPRETER = 0,
    TURBOJS_TIER_BASELINE = 1,
    TURBOJS_TIER_OPTIMIZING = 2,
    TURBOJS_TIER_AOT = 3
} TurboJSOptimizationTier;

typedef struct TurboJSOptimizationConfig {
    uint32_t baseline_threshold;
    uint32_t optimizing_threshold;
    uint8_t enable_profiling;
    uint8_t enable_jit;
    uint8_t enable_aot;
    uint8_t reserved;
} TurboJSOptimizationConfig;

typedef struct TurboJSOptimizationCapabilities {
    uint8_t profiling;
    uint8_t portable_bytecode_aot;
    uint8_t baseline_jit;
    uint8_t optimizing_jit;
} TurboJSOptimizationCapabilities;

typedef struct TurboJSHotFunction {
    uint64_t function_id;
    uint64_t call_count;
    TurboJSOptimizationTier requested_tier;
} TurboJSHotFunction;

typedef struct TurboJSProfiler TurboJSProfiler;

typedef struct TurboJSRuntimeJITStats {
    uint64_t interpreted_calls;
    uint64_t native_calls;
    uint64_t guard_failures;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t compilations;
    uint64_t evictions;
    size_t cache_entries;
    size_t native_code_bytes;
} TurboJSRuntimeJITStats;

struct JSRuntime;
void TurboJS_SetRuntimeJITThreshold(struct JSRuntime *rt, uint32_t threshold);
TurboJSRuntimeJITStats TurboJS_GetRuntimeJITStats(const struct JSRuntime *rt);
void TurboJS_ClearRuntimeJITCache(struct JSRuntime *rt);

TurboJSOptimizationConfig TurboJS_DefaultOptimizationConfig(void);
TurboJSOptimizationCapabilities TurboJS_GetOptimizationCapabilities(void);
const char *TurboJS_OptimizationTierName(TurboJSOptimizationTier tier);

TurboJSProfiler *TurboJS_ProfilerCreate(const TurboJSOptimizationConfig *config);
void TurboJS_ProfilerDestroy(TurboJSProfiler *profiler);
TurboJSOptimizationTier TurboJS_ProfilerRecordCall(TurboJSProfiler *profiler,
                                                     uint64_t function_id);
int TurboJS_ProfilerQuery(const TurboJSProfiler *profiler,
                          uint64_t function_id,
                          TurboJSHotFunction *out);
size_t TurboJS_ProfilerFunctionCount(const TurboJSProfiler *profiler);

#ifdef __cplusplus
}
#endif

#endif
