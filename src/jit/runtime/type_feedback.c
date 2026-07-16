#include <limits.h>
#include <string.h>
#include "jit.h"

static void observe(TurboJSFeedbackSlot *slot, uint32_t type)
{
    uint32_t before;
    if (!slot || type == TURBOJS_FEEDBACK_NONE) return;
    before = slot->observed_types;
    slot->observed_types |= type;
    if (slot->observations != UINT32_MAX) slot->observations++;
    if (before != 0 && before != slot->observed_types)
        if (slot->transitions != UINT32_MAX) slot->transitions++;
}

uint32_t TurboJS_FeedbackClassifyInteger(int64_t value)
{
    return value >= INT32_MIN && value <= INT32_MAX
        ? TURBOJS_FEEDBACK_INT32
        : TURBOJS_FEEDBACK_INT64;
}

void TurboJS_FeedbackVectorInit(TurboJSFeedbackVector *vector, uint16_t argument_count)
{
    if (!vector) return;
    memset(vector, 0, sizeof(*vector));
    vector->argument_count = argument_count > TURBOJS_FEEDBACK_MAX_ARGUMENTS
        ? TURBOJS_FEEDBACK_MAX_ARGUMENTS : argument_count;
}

void TurboJS_FeedbackObserveCall(TurboJSFeedbackVector *vector,
                                 const int64_t *arguments,
                                 size_t argument_count)
{
    size_t i, count;
    if (!vector) return;
    if (vector->execution_count != UINT32_MAX) vector->execution_count++;
    if (!arguments) return;
    count = argument_count < vector->argument_count ? argument_count : vector->argument_count;
    for (i = 0; i < count; i++)
        observe(&vector->arguments[i], TurboJS_FeedbackClassifyInteger(arguments[i]));
}

void TurboJS_FeedbackObserveResult(TurboJSFeedbackVector *vector, int64_t result)
{
    if (!vector) return;
    observe(&vector->result, TurboJS_FeedbackClassifyInteger(result));
}

void TurboJS_FeedbackObserveBailout(TurboJSFeedbackVector *vector)
{
    if (vector && vector->bailout_count != UINT32_MAX) vector->bailout_count++;
}

void TurboJS_FeedbackObserveException(TurboJSFeedbackVector *vector)
{
    if (vector && vector->exception_count != UINT32_MAX) vector->exception_count++;
}

int TurboJS_FeedbackSlotIsStable(const TurboJSFeedbackSlot *slot)
{
    uint32_t types;
    if (!slot || slot->observations == 0) return 0;
    types = slot->observed_types;
    return (types & (types - 1u)) == 0;
}

TurboJSOptimizationPolicy TurboJS_OptimizationPolicyDefault(void)
{
    TurboJSOptimizationPolicy p;
    p.minimum_executions = 1000;
    p.maximum_bailouts = 3;
    p.maximum_exceptions = 1;
    p.require_stable_arguments = 1;
    p.require_stable_result = 1;
    return p;
}

TurboJSOptimizationReport TurboJS_EvaluateOptimization(
    const TurboJSFeedbackVector *vector,
    const TurboJSOptimizationPolicy *policy)
{
    TurboJSOptimizationPolicy fallback;
    TurboJSOptimizationReport r;
    uint16_t i;
    memset(&r, 0, sizeof(r));
    r.decision = TURBOJS_OPTIMIZATION_ELIGIBLE;
    r.unstable_argument = UINT16_MAX;
    if (!vector) {
        r.decision = TURBOJS_OPTIMIZATION_TOO_COLD;
        return r;
    }
    fallback = TurboJS_OptimizationPolicyDefault();
    if (!policy) policy = &fallback;
    r.executions = vector->execution_count;
    r.bailouts = vector->bailout_count;
    r.exceptions = vector->exception_count;
    if (vector->argument_count > TURBOJS_FEEDBACK_MAX_ARGUMENTS) {
        r.decision = TURBOJS_OPTIMIZATION_ARGUMENT_LIMIT;
    } else if (vector->execution_count < policy->minimum_executions) {
        r.decision = TURBOJS_OPTIMIZATION_TOO_COLD;
    } else if (vector->bailout_count > policy->maximum_bailouts) {
        r.decision = TURBOJS_OPTIMIZATION_TOO_MANY_BAILOUTS;
    } else if (vector->exception_count > policy->maximum_exceptions) {
        r.decision = TURBOJS_OPTIMIZATION_TOO_MANY_EXCEPTIONS;
    } else if (policy->require_stable_arguments) {
        for (i = 0; i < vector->argument_count; i++) {
            if (!TurboJS_FeedbackSlotIsStable(&vector->arguments[i])) {
                r.decision = TURBOJS_OPTIMIZATION_UNSTABLE_ARGUMENTS;
                r.unstable_argument = i;
                break;
            }
        }
    }
    if (r.decision == TURBOJS_OPTIMIZATION_ELIGIBLE &&
        policy->require_stable_result &&
        !TurboJS_FeedbackSlotIsStable(&vector->result))
        r.decision = TURBOJS_OPTIMIZATION_UNSTABLE_RESULT;
    return r;
}

const char *TurboJS_OptimizationDecisionName(TurboJSOptimizationDecision decision)
{
    switch (decision) {
    case TURBOJS_OPTIMIZATION_ELIGIBLE: return "eligible";
    case TURBOJS_OPTIMIZATION_TOO_COLD: return "too-cold";
    case TURBOJS_OPTIMIZATION_UNSTABLE_ARGUMENTS: return "unstable-arguments";
    case TURBOJS_OPTIMIZATION_UNSTABLE_RESULT: return "unstable-result";
    case TURBOJS_OPTIMIZATION_TOO_MANY_BAILOUTS: return "too-many-bailouts";
    case TURBOJS_OPTIMIZATION_TOO_MANY_EXCEPTIONS: return "too-many-exceptions";
    case TURBOJS_OPTIMIZATION_ARGUMENT_LIMIT: return "argument-limit";
    default: return "unknown";
    }
}
