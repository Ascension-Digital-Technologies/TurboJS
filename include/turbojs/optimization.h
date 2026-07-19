#ifndef TURBOJS_OPTIMIZATION_H
#define TURBOJS_OPTIMIZATION_H

#include <stddef.h>
#include <stdint.h>

#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum TurboJSOptimizationTier {
    TURBOJS_TIER_INTERPRETER = 0,
    TURBOJS_TIER_BASELINE = 1,
    TURBOJS_TIER_OPTIMIZING = 2,
    TURBOJS_TIER_AOT = 3
} TurboJSOptimizationTier;

/*
 * Stable public identities for the TurboJS execution pipeline. Generic tier
 * names remain available for tooling; codenames provide concise profiler and
 * diagnostic labels without tying callers to source-directory names.
 */
typedef enum TurboJSPipelineComponent {
    TURBOJS_PIPELINE_ROTOR_FRONTEND = 0,
    TURBOJS_PIPELINE_PULSE_INTERPRETER,
    TURBOJS_PIPELINE_SPOOL_BASELINE,
    TURBOJS_PIPELINE_REDLINE_OPTIMIZER,
    TURBOJS_PIPELINE_FORGE_AOT,
    TURBOJS_PIPELINE_TELEMETRY_FEEDBACK,
    TURBOJS_PIPELINE_RELAY_INLINE_CACHE,
    TURBOJS_PIPELINE_CLUTCH_CALL_ABI,
    TURBOJS_PIPELINE_BEACON_IDENTITY_REGISTRY,
    TURBOJS_PIPELINE_SLIPSTREAM_OSR,
    TURBOJS_PIPELINE_REWIND_DEOPT,
    TURBOJS_PIPELINE_GEARBOX_BACKEND,
    TURBOJS_PIPELINE_VAULT_CODE_CACHE,
    TURBOJS_PIPELINE_COMPONENT_COUNT
} TurboJSPipelineComponent;

typedef struct TurboJSPipelineIdentity {
    TurboJSPipelineComponent component;
    const char *codename;
    const char *role;
    const char *description;
} TurboJSPipelineIdentity;

typedef struct TurboJSOptimizationConfig {
    uint32_t baseline_threshold;
    uint32_t optimizing_threshold;
    uint32_t osr_threshold;
    uint8_t enable_profiling;
    uint8_t enable_jit;
    uint8_t enable_optimizing_jit;
    uint8_t enable_osr;
    uint8_t enable_aot;
    uint8_t reserved[3];
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
    uint64_t baseline_compile_requests;
    uint64_t baseline_compilations;
    uint64_t baseline_compile_failures;
    uint64_t optimizing_compile_requests;
    uint64_t optimizing_compilations;
    uint64_t optimizing_compile_failures;
    uint64_t tier_up_requests;
    uint64_t tier_up_successes;
    uint64_t deoptimizations;
    uint64_t region_compilations;
    uint64_t region_native_calls;
    uint64_t region_compile_failures;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t compilations;
    uint64_t evictions;
    size_t cache_entries;
    size_t native_code_bytes;
    uint64_t property_ic_hits;
    uint64_t property_ic_misses;
    uint64_t property_ic_fills;
    uint64_t call_feedback_observations;
    uint64_t call_feedback_monomorphic;
    uint64_t call_feedback_polymorphic;
    uint64_t call_feedback_megamorphic;
    uint64_t call_feedback_transitions;
    uint64_t relay_call_hits;
    uint64_t relay_call_misses;
    uint64_t relay_call_installs;
    uint64_t relay_call_invalidations;
    uint64_t relay_spool_hits;
    uint64_t relay_spool_misses;
    uint64_t relay_spool_installs;
    uint64_t relay_spool_invalidations;
    uint64_t relay_spool_feedback_installs;
    uint64_t relay_spool_feedback_rejections;
    uint64_t relay_spool_stale_bailouts;
    uint64_t relay_spool_callee_bailouts;
    uint64_t spool_call_lowering_resolved;
    uint64_t spool_call_lowering_rejected;
    uint64_t dense_array_load_hits;
    uint64_t dense_array_store_hits;
    uint64_t dense_array_slow_paths;
    uint64_t dense_array_osr_entries;
    uint64_t dense_array_osr_elements;
    uint64_t dense_array_osr_unrolled_blocks;
    uint64_t dense_array_osr_multi_lane_blocks;
    uint64_t dense_array_osr_float_promotions;
    uint64_t dense_array_transform_osr_entries;
    uint64_t dense_array_transform_osr_elements;
    uint64_t dense_array_inplace_osr_entries;
    uint64_t dense_array_binary_osr_entries;
    uint64_t dense_array_copy_osr_entries;
    uint64_t dense_array_fill_osr_entries;
    uint64_t typed_array_osr_entries;
    uint64_t typed_array_osr_elements;
    uint64_t typed_array_simd_elements;
    uint64_t typed_array_simd_sse2_entries;
    uint64_t typed_array_simd_avx2_entries;
    uint64_t holey_array_osr_entries;
    uint64_t holey_array_osr_elements;
    uint64_t typed_array_affine_sum_osr_entries;
    uint64_t typed_array_affine_sum_osr_elements;
    uint64_t object_array_osr_entries;
    uint64_t object_array_osr_elements;
    uint64_t object_array_polymorphic_osr_entries;
    uint64_t object_array_update_osr_entries;
    uint64_t object_array_grouped_osr_entries;
    uint64_t object_array_grouped_osr_elements;
    uint64_t osr_backedges;
    uint64_t osr_compile_requests;
    uint64_t osr_compilations;
    uint64_t osr_compile_failures;
    uint64_t osr_frame_captures;
    uint64_t osr_entries;
    uint64_t osr_bailouts;
    uint64_t osr_negative_cache_hits;
    uint64_t osr_rejections_unsupported;
    uint64_t osr_rejections_allocation;
    uint64_t osr_rejections_backend;
    uint64_t osr_leaf_call_entries;
    uint64_t osr_leaf_call_iterations;
    uint64_t osr_int32_mix_entries;
    uint64_t osr_int32_mix_iterations;
    uint64_t osr_polymorphic_leaf_entries;
    uint64_t osr_polymorphic_leaf_iterations;
    uint64_t osr_closure_call_entries;
    uint64_t osr_closure_call_iterations;
    uint64_t osr_recursive_call_entries;
    uint64_t osr_recursive_call_iterations;
    uint64_t osr_coupled_float_entries;
    uint64_t osr_coupled_float_iterations;
} TurboJSRuntimeJITStats;

struct JSRuntime;
JS_EXTERN void TurboJS_SetRuntimeJITThreshold(struct JSRuntime *rt, uint32_t threshold);
JS_EXTERN void TurboJS_SetRuntimeOptimizationConfig(
    struct JSRuntime *rt, const TurboJSOptimizationConfig *config);
JS_EXTERN TurboJSOptimizationConfig TurboJS_GetRuntimeOptimizationConfig(
    const struct JSRuntime *rt);
JS_EXTERN TurboJSRuntimeJITStats TurboJS_GetRuntimeJITStats(const struct JSRuntime *rt);
JS_EXTERN void TurboJS_ResetRuntimeJITStats(struct JSRuntime *rt);
JS_EXTERN void TurboJS_ClearRuntimeJITCache(struct JSRuntime *rt);

JS_EXTERN TurboJSOptimizationConfig TurboJS_DefaultOptimizationConfig(void);
JS_EXTERN TurboJSOptimizationCapabilities TurboJS_GetOptimizationCapabilities(void);
JS_EXTERN const char *TurboJS_OptimizationTierName(TurboJSOptimizationTier tier);
JS_EXTERN const char *TurboJS_OptimizationTierCodename(TurboJSOptimizationTier tier);
JS_EXTERN const char *TurboJS_PipelineComponentCodename(TurboJSPipelineComponent component);
JS_EXTERN const char *TurboJS_PipelineComponentRole(TurboJSPipelineComponent component);
JS_EXTERN const TurboJSPipelineIdentity *TurboJS_PipelineIdentity(TurboJSPipelineComponent component);
JS_EXTERN size_t TurboJS_PipelineComponentCount(void);

JS_EXTERN TurboJSProfiler *TurboJS_ProfilerCreate(const TurboJSOptimizationConfig *config);
JS_EXTERN void TurboJS_ProfilerDestroy(TurboJSProfiler *profiler);
JS_EXTERN TurboJSOptimizationTier TurboJS_ProfilerRecordCall(TurboJSProfiler *profiler,
                                                     uint64_t function_id);
JS_EXTERN int TurboJS_ProfilerQuery(const TurboJSProfiler *profiler,
                          uint64_t function_id,
                          TurboJSHotFunction *out);
JS_EXTERN size_t TurboJS_ProfilerFunctionCount(const TurboJSProfiler *profiler);

#ifdef __cplusplus
}
#endif

#endif
