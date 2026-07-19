#include <stdlib.h>
#include <string.h>
#include "jit.h"

static TurboJSIRStatus region_fail(TurboJSIRDiagnostic *d, TurboJSIRStatus s,
                                   size_t i, const char *m) {
    if (d) { d->status = s; d->instruction_index = i; d->message = m; }
    return s;
}

void TurboJS_EngineBytecodeRegionPlanDestroy(TurboJSBytecodeRegionPlan *plan) {
    if (!plan) return;
    TurboJS_EngineBytecodeCFGDestroy(&plan->cfg);
    free(plan->blocks);
    memset(plan, 0, sizeof(*plan));
}

TurboJSIRStatus TurboJS_EngineBytecodeBuildRegionPlan(
    const TurboJSEngineBytecodeInfo *bytecode,
    TurboJSBytecodeRegionPlan *plan,
    TurboJSIRDiagnostic *diagnostic) {
    TurboJSIRStatus status;
    size_t i;
    if (!bytecode || !plan)
        return region_fail(diagnostic, TURBOJS_IR_INVALID_ARGUMENT, 0,
                           "invalid bytecode region input");
    memset(plan, 0, sizeof(*plan));
    status = TurboJS_EngineBytecodeBuildCFG(bytecode, &plan->cfg, diagnostic);
    if (status != TURBOJS_IR_OK) return status;
    plan->block_count = plan->cfg.block_count;
    plan->blocks = (TurboJSBytecodeRegionBlock *)calloc(
        plan->block_count, sizeof(*plan->blocks));
    if (!plan->blocks) {
        TurboJS_EngineBytecodeRegionPlanDestroy(plan);
        return region_fail(diagnostic, TURBOJS_IR_OUT_OF_MEMORY, 0,
                           "unable to allocate bytecode region plan");
    }
    for (i = 0; i < plan->block_count; ++i) {
        const TurboJSBytecodeBlock *cfg_block = &plan->cfg.blocks[i];
        TurboJSBytecodeRegionBlock *region = &plan->blocks[i];
        region->cfg_block = (uint32_t)i;
        region->entry_stack_depth = cfg_block->entry_stack_depth;
        region->predecessor_count = cfg_block->predecessor_count;
        region->successor_count = cfg_block->successor_count;
        if (cfg_block->flags & TURBOJS_BYTECODE_BLOCK_REACHABLE) {
            region->flags |= TURBOJS_BYTECODE_REGION_REACHABLE;
            plan->reachable_block_count++;
        }
        if (cfg_block->flags & TURBOJS_BYTECODE_BLOCK_LOOP_HEADER) {
            region->flags |= TURBOJS_BYTECODE_REGION_LOOP_HEADER;
            plan->loop_header_count++;
        }
        if (cfg_block->flags & TURBOJS_BYTECODE_BLOCK_HELPER_EXIT) {
            region->flags |= TURBOJS_BYTECODE_REGION_HELPER_EXIT;
            plan->helper_exit_block_count++;
        }
        if ((cfg_block->flags & TURBOJS_BYTECODE_BLOCK_REACHABLE) &&
            cfg_block->predecessor_count > 1) {
            region->flags |= TURBOJS_BYTECODE_REGION_MERGE;
            region->local_phi_count = bytecode->local_count;
            region->stack_phi_count = cfg_block->entry_stack_depth;
            plan->merge_block_count++;
            plan->estimated_local_phis += region->local_phi_count;
            plan->estimated_stack_phis += region->stack_phi_count;
        }
    }
    if (diagnostic) {
        diagnostic->status = TURBOJS_IR_OK;
        diagnostic->instruction_index = 0;
        diagnostic->message = NULL;
    }
    return TURBOJS_IR_OK;
}
