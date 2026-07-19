#include <string.h>
#include "jit.h"

void TurboJS_CallableReferenceInit(
    TurboJSCallableReference *reference, uint64_t target_identity,
    const TurboJSNativeEntryHandle *target, uint64_t expected_generation,
    TurboJSNativeEntryKind expected_kind, uint16_t argument_count,
    const void *closure_environment)
{
    if (!reference)
        return;
    memset(reference, 0, sizeof(*reference));
    reference->target_identity = target_identity;
    reference->target = target;
    reference->expected_generation = expected_generation;
    reference->closure_environment = closure_environment;
    reference->argument_count = argument_count;
    reference->expected_kind = (uint8_t)expected_kind;
}

TurboJSIRStatus TurboJS_CallableReferenceInitRooted(
    TurboJSCallableReference *reference, uint64_t target_identity,
    const TurboJSNativeEntryHandle *target, uint64_t expected_generation,
    TurboJSNativeEntryKind expected_kind, uint16_t argument_count,
    void *closure_environment, const TurboJSRootingHooks *rooting)
{
    void *rooted = closure_environment;
    if (!reference || !rooting || !rooting->retain || !rooting->release)
        return TURBOJS_IR_INVALID_ARGUMENT;
    memset(reference, 0, sizeof(*reference));
    if (closure_environment) {
        rooted = rooting->retain(rooting->opaque, closure_environment);
        if (!rooted)
            return TURBOJS_IR_OUT_OF_MEMORY;
    }
    reference->target_identity = target_identity;
    reference->target = target;
    reference->expected_generation = expected_generation;
    reference->closure_environment = rooted;
    reference->environment_rooting = *rooting;
    reference->argument_count = argument_count;
    reference->expected_kind = (uint8_t)expected_kind;
    reference->owns_environment = closure_environment != NULL;
    return TURBOJS_IR_OK;
}

void TurboJS_CallableReferenceDestroy(TurboJSCallableReference *reference)
{
    if (!reference)
        return;
    if (reference->owns_environment && reference->closure_environment &&
        reference->environment_rooting.release)
        reference->environment_rooting.release(
            reference->environment_rooting.opaque,
            (void *)reference->closure_environment);
    memset(reference, 0, sizeof(*reference));
}

int TurboJS_CallableReferenceIsLive(const TurboJSCallableReference *reference)
{
    if (!reference || !reference->target_identity || !reference->target)
        return 0;
    if (reference->argument_count != reference->target->argument_count)
        return 0;
    return TurboJS_NativeEntryHandleIsLive(
        reference->target, reference->expected_generation,
        (TurboJSNativeEntryKind)reference->expected_kind);
}

TurboJSIRStatus TurboJS_CallableReferenceInvokeI64(
    const TurboJSCallableReference *reference, const int64_t *arguments,
    size_t argument_count, int64_t *result)
{
    if (!reference || reference->expected_kind != TURBOJS_NATIVE_ENTRY_INT32)
        return TURBOJS_IR_INVALID_ARGUMENT;
    if (!TurboJS_CallableReferenceIsLive(reference))
        return TURBOJS_IR_UNSUPPORTED;
    return TurboJS_NativeEntryInvokeI64(
        reference->target, reference->expected_generation, arguments,
        argument_count, result);
}

TurboJSIRStatus TurboJS_CallableReferenceInvokeF64(
    const TurboJSCallableReference *reference, const double *arguments,
    size_t argument_count, double *result)
{
    if (!reference || reference->expected_kind != TURBOJS_NATIVE_ENTRY_FLOAT64)
        return TURBOJS_IR_INVALID_ARGUMENT;
    if (!TurboJS_CallableReferenceIsLive(reference))
        return TURBOJS_IR_UNSUPPORTED;
    return TurboJS_NativeEntryInvokeF64(
        reference->target, reference->expected_generation, arguments,
        argument_count, result);
}
