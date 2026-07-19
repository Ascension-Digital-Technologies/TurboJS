#ifndef TURBOJS_JIT_ARM64_LOWERING_H
#define TURBOJS_JIT_ARM64_LOWERING_H

#include "jit.h"
#include "src/jit/backend/arm64/arm64_encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TurboJSArm64DeoptSite {
    size_t instruction_index;
    size_t native_offset;
    uint32_t bytecode_offset;
    TurboJSBailoutReason reason;
    uint64_t live_register_mask;
    uint64_t live_local_mask;
} TurboJSArm64DeoptSite;

typedef struct TurboJSArm64Compilation {
    TurboJSArm64Buffer code;
    TurboJSStackMap *stack_maps;
    size_t stack_map_count;
    TurboJSArm64DeoptSite *deopt_sites;
    size_t deopt_site_count;
    uint16_t frame_size;
    uint16_t register_count;
    uint16_t local_count;
    uint8_t uses_runtime_helpers;
    uint8_t uses_safepoints;
} TurboJSArm64Compilation;

void TurboJS_Arm64CompilationInit(TurboJSArm64Compilation *compilation);
void TurboJS_Arm64CompilationDestroy(TurboJSArm64Compilation *compilation);

/* Lower portable numeric/control-flow IR to AArch64 machine words and emit
 * stack-map/deoptimization metadata. The generated function follows AAPCS64:
 *   x0 = const int64_t *arguments
 *   x1 = int64_t *result
 *   x2 = runtime-helper dispatcher (reserved for helper-enabled code)
 *   x3 = TurboJSSafepointController *
 *   x4 = deoptimization storage
 *   w0 = TurboJSIRStatus
 *
 * The extended result is architecture-neutral metadata suitable for GC root
 * tracing and interpreter reconstruction before native execution is enabled.
 */
TurboJSIRStatus TurboJS_Arm64LowerIREx(const TurboJSIRFunction *function,
                                       TurboJSArm64Compilation *compilation,
                                       TurboJSIRDiagnostic *diagnostic);

/* Compatibility wrapper returning only machine words. */

/* Relocate precise heap references in a materialized ARM64 frame using a
 * stack map emitted by TurboJS_Arm64LowerIREx. frame_slots contains register
 * slots followed by local slots, matching the generated frame layout. */
TurboJSIRStatus TurboJS_Arm64RelocateFrameReferences(
    const TurboJSArm64Compilation *compilation, size_t stack_map_index,
    int64_t *frame_slots, size_t frame_slot_count,
    TurboJSGCRelocateCallback relocate, void *opaque);

TurboJSIRStatus TurboJS_Arm64LowerIR(const TurboJSIRFunction *function,
                                     TurboJSArm64Buffer *code,
                                     TurboJSIRDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif
#endif
