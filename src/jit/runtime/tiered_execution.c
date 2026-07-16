#include <string.h>
#include "jit.h"

static uint32_t default_opt_threshold(uint32_t baseline)
{
    uint32_t threshold = baseline ? baseline * 4u : 1000u;
    return threshold < 1000u ? 1000u : threshold;
}

void TurboJS_TieredFunctionInitAdvanced(TurboJSTieredFunction *function,
                                        const void *cache_key,
                                        uint32_t baseline_threshold,
                                        uint32_t optimization_threshold,
                                        const TurboJSOptimizationPolicy *policy)
{
    if (!function) return;
    memset(function, 0, sizeof(*function));
    function->cache_key = cache_key;
    function->compile_threshold = baseline_threshold ? baseline_threshold : 100u;
    function->optimization_threshold = optimization_threshold ?
        optimization_threshold : default_opt_threshold(function->compile_threshold);
    function->optimization_policy = policy ? *policy : TurboJS_OptimizationPolicyDefault();
    function->optimization_policy.minimum_executions = function->optimization_threshold;
    TurboJS_FeedbackVectorInit(&function->feedback, 0);
}

void TurboJS_TieredFunctionInit(TurboJSTieredFunction *function,
                                const void *cache_key,
                                uint32_t compile_threshold)
{
    TurboJS_TieredFunctionInitAdvanced(function, cache_key, compile_threshold, 0, NULL);
}

void TurboJS_TieredFunctionInvalidateOptimized(TurboJSTieredFunction *function)
{
    if (!function || !function->optimized) return;
    TurboJS_OptimizedFunctionDestroy(function->optimized);
    function->optimized = NULL;
    function->stats.optimized_invalidations++;
}

void TurboJS_TieredFunctionDestroy(TurboJSTieredFunction *function)
{
    if (!function) return;
    TurboJS_TieredFunctionInvalidateOptimized(function);
    memset(function, 0, sizeof(*function));
}

TurboJSTieredStats TurboJS_TieredFunctionGetStats(const TurboJSTieredFunction *function)
{
    TurboJSTieredStats stats;
    memset(&stats, 0, sizeof(stats));
    if (function) stats = function->stats;
    return stats;
}

static void observe_success(TurboJSTieredFunction *function, int64_t result)
{
    TurboJS_FeedbackObserveResult(&function->feedback, result);
}

static TurboJSIRStatus fallback_interpreter(TurboJSTieredFunction *function,
                                             const TurboJSIRFunction *ir,
                                             const int64_t *arguments,
                                             size_t argument_count,
                                             int64_t *result,
                                             TurboJSTieredResult *route)
{
    TurboJSIRStatus status = TurboJS_IRExecute(ir, arguments, argument_count, result);
    if (status == TURBOJS_IR_OK) {
        observe_success(function, *result);
        function->stats.interpreted_calls++;
        if (route) *route = TURBOJS_TIERED_INTERPRETED;
    } else if (status == TURBOJS_IR_EXCEPTION) {
        TurboJS_FeedbackObserveException(&function->feedback);
    }
    return status;
}

TurboJSIRStatus TurboJS_TieredInvoke(TurboJSTieredFunction *function,
                                     TurboJSCodeCache *cache,
                                     const TurboJSIRFunction *ir,
                                     const int64_t *arguments,
                                     size_t argument_count,
                                     int64_t *result,
                                     TurboJSTieredResult *execution_result,
                                     TurboJSIRDiagnostic *diagnostic)
{
    const TurboJSNativeFunction *native;
    TurboJSIRStatus status;
    TurboJSOptimizationReport report;
    if (!function || !cache || !ir || !result || !function->cache_key)
        return TURBOJS_IR_INVALID_ARGUMENT;

    function->call_count++;
    if (function->feedback.argument_count == 0)
        TurboJS_FeedbackVectorInit(&function->feedback, (uint16_t)argument_count);
    TurboJS_FeedbackObserveCall(&function->feedback, arguments, argument_count);

    if (function->optimized) {
        status = TurboJS_OptimizedInvoke(function->optimized, arguments, argument_count, result);
        if (status == TURBOJS_IR_OK) {
            observe_success(function, *result);
            function->stats.optimized_calls++;
            if (execution_result) *execution_result = TURBOJS_TIERED_OPTIMIZED_NATIVE;
            return status;
        }
        if (status == TURBOJS_IR_EXCEPTION) {
            TurboJS_FeedbackObserveException(&function->feedback);
            return status;
        }
        TurboJS_FeedbackObserveBailout(&function->feedback);
        TurboJS_TieredFunctionInvalidateOptimized(function);
    }

    report = TurboJS_EvaluateOptimization(&function->feedback, &function->optimization_policy);
    if (!function->optimization_attempted && report.decision == TURBOJS_OPTIMIZATION_ELIGIBLE) {
        TurboJSOptimizingStats stats;
        function->optimization_attempted = 1;
        status = TurboJS_OptimizingCompile(ir, &function->feedback,
                                           &function->optimized, &stats, diagnostic);
        if (status == TURBOJS_IR_OK) {
            function->stats.optimized_compilations++;
            status = TurboJS_OptimizedInvoke(function->optimized, arguments, argument_count, result);
            if (status == TURBOJS_IR_OK) {
                observe_success(function, *result);
                function->stats.optimized_calls++;
                if (execution_result) *execution_result = TURBOJS_TIERED_OPTIMIZED_COMPILED;
                return status;
            }
            if (status == TURBOJS_IR_EXCEPTION) {
                TurboJS_FeedbackObserveException(&function->feedback);
                return status;
            }
            TurboJS_FeedbackObserveBailout(&function->feedback);
            TurboJS_TieredFunctionInvalidateOptimized(function);
        } else if (status != TURBOJS_IR_UNSUPPORTED) {
            return status;
        }
    }

    native = TurboJS_CodeCacheLookup(cache, function->cache_key);
    if (native) {
        status = TurboJS_NativeInvoke(native, arguments, argument_count, result);
        if (status == TURBOJS_IR_OK) {
            observe_success(function, *result);
            function->stats.baseline_calls++;
            if (execution_result) *execution_result = TURBOJS_TIERED_BASELINE_NATIVE;
            return status;
        }
        if (status != TURBOJS_IR_BAILOUT) {
            if (status == TURBOJS_IR_EXCEPTION)
                TurboJS_FeedbackObserveException(&function->feedback);
            return status;
        }
        TurboJS_FeedbackObserveBailout(&function->feedback);
        return fallback_interpreter(function, ir, arguments, argument_count,
                                    result, execution_result);
    }

    if (!function->compilation_attempted && function->call_count >= function->compile_threshold) {
        function->compilation_attempted = 1;
        status = TurboJS_CodeCacheCompile(cache, function->cache_key, ir, &native, diagnostic);
        if (status == TURBOJS_IR_OK) {
            function->stats.baseline_compilations++;
            status = TurboJS_NativeInvoke(native, arguments, argument_count, result);
            if (status == TURBOJS_IR_OK) {
                observe_success(function, *result);
                function->stats.baseline_calls++;
                if (execution_result) *execution_result = TURBOJS_TIERED_BASELINE_COMPILED;
                return status;
            }
            if (status != TURBOJS_IR_BAILOUT) {
                if (status == TURBOJS_IR_EXCEPTION)
                    TurboJS_FeedbackObserveException(&function->feedback);
                return status;
            }
            TurboJS_FeedbackObserveBailout(&function->feedback);
            return fallback_interpreter(function, ir, arguments, argument_count,
                                        result, execution_result);
        }
        if (status != TURBOJS_IR_UNSUPPORTED)
            return status;
    }
    return fallback_interpreter(function, ir, arguments, argument_count,
                                result, execution_result);
}
