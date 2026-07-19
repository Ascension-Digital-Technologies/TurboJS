#include "jit.h"
#include <stdlib.h>
#include <string.h>

struct TurboJSOptimizedFunction {
    TurboJSNativeFunction *native;
    TurboJSSSAGraph graph;
    TurboJSLinearScanResult allocation;
};

static TurboJSIRStatus fail(TurboJSIRDiagnostic *d, TurboJSIRStatus s,
                            size_t i, const char *m) {
    if (d) { d->status=s; d->instruction_index=i; d->message=m; }
    return s;
}

/* Lower the currently supported straight-line optimized SSA graph back into
 * compact IR. This deliberately rejects CFGs that still require phi moves;
 * those stay on the baseline tier instead of being miscompiled. */
static TurboJSIRStatus lower_graph(const TurboJSSSAGraph *g,
                                   uint16_t argument_count,
                                   TurboJSIRFunction *out,
                                   TurboJSIRDiagnostic *d) {
    uint16_t map[TURBOJS_IR_MAX_REGISTERS];
    size_t i;
    if (!g || !out || g->block_count != 1)
        return fail(d, TURBOJS_IR_UNSUPPORTED, 0,
                    "optimizing native lowering currently requires one reachable block");
    for (i=0;i<TURBOJS_IR_MAX_REGISTERS;i++) map[i]=TURBOJS_IR_NO_REGISTER;
    TurboJS_IRFunctionInit(out, argument_count);
    for (i=0;i<g->value_count;i++) {
        const TurboJSSSAValue *v=&g->values[i];
        TurboJSIRInstruction in;
        uint16_t dst;
        if (v->removed || v->opcode==TURBOJS_SSA_NOP ||
            v->opcode==TURBOJS_SSA_GUARD_INT32) continue;
        memset(&in,0,sizeof(in));
        in.destination=in.left=in.right=TURBOJS_IR_NO_REGISTER;
        in.bytecode_offset=v->source_instruction;
        switch(v->opcode) {
        case TURBOJS_SSA_ARGUMENT:
            dst=TurboJS_IRAllocateRegister(out); if(dst==TURBOJS_IR_NO_REGISTER) goto oom;
            in.opcode=TURBOJS_IR_ARGUMENT; in.destination=dst; in.immediate=v->immediate; map[v->id]=dst; break;
        case TURBOJS_SSA_CONSTANT_I64:
            dst=TurboJS_IRAllocateRegister(out); if(dst==TURBOJS_IR_NO_REGISTER) goto oom;
            in.opcode=TURBOJS_IR_CONSTANT_I64; in.destination=dst; in.immediate=v->immediate; map[v->id]=dst; break;
        case TURBOJS_SSA_ADD_I64: case TURBOJS_SSA_SUB_I64: case TURBOJS_SSA_MUL_I64:
        case TURBOJS_SSA_LESS_THAN_I64:
            if(v->left>=TURBOJS_IR_MAX_REGISTERS||v->right>=TURBOJS_IR_MAX_REGISTERS||
               map[v->left]==TURBOJS_IR_NO_REGISTER||map[v->right]==TURBOJS_IR_NO_REGISTER)
                goto unsupported;
            dst=TurboJS_IRAllocateRegister(out); if(dst==TURBOJS_IR_NO_REGISTER) goto oom;
            in.opcode=v->opcode==TURBOJS_SSA_ADD_I64?TURBOJS_IR_ADD_I64:
                      v->opcode==TURBOJS_SSA_SUB_I64?TURBOJS_IR_SUB_I64:
                      v->opcode==TURBOJS_SSA_MUL_I64?TURBOJS_IR_MUL_I64:TURBOJS_IR_LESS_THAN_I64;
            in.destination=dst; in.left=map[v->left]; in.right=map[v->right]; map[v->id]=dst; break;
        case TURBOJS_SSA_RETURN:
            if(v->left>=TURBOJS_IR_MAX_REGISTERS||map[v->left]==TURBOJS_IR_NO_REGISTER) goto unsupported;
            in.opcode=TURBOJS_IR_RETURN_I64; in.left=map[v->left]; break;
        default: goto unsupported;
        }
        if(TurboJS_IREmit(out,in)!=TURBOJS_IR_OK) goto oom;
    }
    return TurboJS_IRVerify(out,d);
oom:
    TurboJS_IRFunctionDestroy(out); return fail(d,TURBOJS_IR_OUT_OF_MEMORY,i,"optimized lowering allocation failed");
unsupported:
    TurboJS_IRFunctionDestroy(out); return fail(d,TURBOJS_IR_UNSUPPORTED,i,"SSA operation requires future optimized CFG lowering");
}

TurboJSIRStatus TurboJS_OptimizingCompile(const TurboJSIRFunction *function,
                                          const TurboJSFeedbackVector *feedback,
                                          TurboJSOptimizedFunction **out_function,
                                          TurboJSOptimizingStats *stats,
                                          TurboJSIRDiagnostic *diagnostic) {
    TurboJSOptimizedFunction *o;
    TurboJSIRFunction lowered;
    TurboJSIRStatus st;
    if(!function||!out_function) return fail(diagnostic,TURBOJS_IR_INVALID_ARGUMENT,0,"invalid optimizing compile arguments");
    *out_function=NULL;
    o=(TurboJSOptimizedFunction*)calloc(1,sizeof(*o));
    if(!o) return fail(diagnostic,TURBOJS_IR_OUT_OF_MEMORY,0,"optimized function allocation failed");
    TurboJS_SSAGraphInit(&o->graph);
    st=TurboJS_SSABuildFromIR(function,&o->graph,diagnostic); if(st!=TURBOJS_IR_OK) goto bad;
    if(stats){memset(stats,0,sizeof(*stats));stats->input_values=(uint32_t)o->graph.value_count;}
    if(feedback){st=TurboJS_SSASpecializeFromFeedback(&o->graph,feedback,stats?&stats->ssa:NULL);if(st!=TURBOJS_IR_OK)goto bad;}
    { TurboJSSSAOptimizationStats s=TurboJS_SSAOptimize(&o->graph);
      if(stats){stats->ssa.constants_folded+=s.constants_folded;stats->ssa.values_removed+=s.values_removed;stats->ssa.branches_folded+=s.branches_folded;stats->ssa.blocks_removed+=s.blocks_removed;stats->ssa.phis_inserted+=s.phis_inserted;stats->ssa.guards_inserted+=s.guards_inserted;}}
    if(!TurboJS_SSAVerify(&o->graph)){st=fail(diagnostic,TURBOJS_IR_INVALID_OPCODE,0,"optimized SSA verification failed");goto bad;}
    st=TurboJS_LinearScanAllocate(&o->graph,6,8,&o->allocation);if(st!=TURBOJS_IR_OK)goto bad;
    if(stats){
        size_t ai;
        stats->allocated_intervals=(uint32_t)o->allocation.interval_count;
        stats->spill_slots=o->allocation.spill_slot_count;
        for(ai=0;ai<o->allocation.interval_count;ai++)
            if(o->allocation.intervals[ai].spill_slot>=0) stats->spilled_intervals++;
    }
    st=lower_graph(&o->graph,function->argument_count,&lowered,diagnostic);if(st!=TURBOJS_IR_OK)goto bad;
    st=TurboJS_BaselineCompile(&lowered,&o->native,diagnostic);TurboJS_IRFunctionDestroy(&lowered);if(st!=TURBOJS_IR_OK)goto bad;
    if(stats){stats->output_values=(uint32_t)o->graph.value_count-stats->ssa.values_removed;stats->deopt_exits=o->graph.deopt_exit_count;stats->native_code_bytes=TurboJS_NativeCodeSize(o->native);}
    *out_function=o;return TURBOJS_IR_OK;
bad:
    TurboJS_LinearScanResultDestroy(&o->allocation);TurboJS_SSAGraphDestroy(&o->graph);free(o);return st;
}
TurboJSIRStatus TurboJS_OptimizedInvoke(const TurboJSOptimizedFunction *f,const int64_t*a,size_t n,int64_t*r){return f&&f->native?TurboJS_NativeInvoke(f->native,a,n,r):TURBOJS_IR_INVALID_ARGUMENT;}
void TurboJS_OptimizedFunctionDestroy(TurboJSOptimizedFunction *f){if(!f)return;TurboJS_NativeFunctionDestroy(f->native);TurboJS_LinearScanResultDestroy(&f->allocation);TurboJS_SSAGraphDestroy(&f->graph);free(f);}
size_t TurboJS_OptimizedCodeSize(const TurboJSOptimizedFunction *f){return f&&f->native?TurboJS_NativeCodeSize(f->native):0;}
const TurboJSLinearScanResult *TurboJS_OptimizedAllocation(const TurboJSOptimizedFunction *f){return f?&f->allocation:NULL;}
