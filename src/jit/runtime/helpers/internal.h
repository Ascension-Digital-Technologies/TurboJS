#ifndef TURBOJS_JIT_RUNTIME_HELPERS_INTERNAL_H
#define TURBOJS_JIT_RUNTIME_HELPERS_INTERNAL_H

#include "jit.h"

static inline uint64_t TurboJS_RuntimeHelperMaterializedMask(size_t count)
{
    if (count >= 64u)
        return UINT64_MAX;
    return count ? ((UINT64_C(1) << count) - UINT64_C(1)) : UINT64_C(0);
}

TurboJSIRStatus TurboJS_RuntimeHelperInvokeAt(
    const TurboJSIRFunction *function,
    TurboJSRuntimeHelperTable *helpers,
    size_t instruction_index,
    int64_t *registers,
    int64_t *locals,
    const TurboJSValueKind *register_kinds,
    const TurboJSValueKind *local_kinds,
    int64_t *helper_value);

void TurboJS_RuntimeContinuationCacheDestroy(TurboJSRuntimeHelperTable *helpers);

TurboJSIRStatus TurboJS_RuntimeContinuationCacheAcquire(
    TurboJSRuntimeHelperTable *helpers, const TurboJSIRFunction *source,
    size_t start_instruction, const int64_t *registers, const int64_t *locals,
    const TurboJSNativeFunction **out_native, int64_t **out_arguments,
    size_t *out_prologue_count);

TurboJSIRStatus TurboJS_RuntimeCompileContinuationSegment(
    const TurboJSIRFunction *source, size_t start_instruction,
    const int64_t *registers, const int64_t *locals,
    TurboJSNativeFunction **out_native, TurboJSIRFunction *out_ir,
    int64_t **out_arguments, size_t *out_prologue_count);

#endif
