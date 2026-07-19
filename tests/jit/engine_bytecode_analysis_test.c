#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "jit.h"
typedef enum TestOpcode {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) OP_##id,
#define def(id, size, n_pop, n_push, f)
#include "internal/bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
} TestOpcode;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"CHECK failed: %s:%d: %s\n",__FILE__,__LINE__,#x);return 1;} } while(0)
static void put_i32(uint8_t *p,int32_t v){uint32_t u=(uint32_t)v;p[0]=(uint8_t)u;p[1]=(uint8_t)(u>>8);p[2]=(uint8_t)(u>>16);p[3]=(uint8_t)(u>>24);}
static size_t u8(uint8_t*b,size_t p,uint8_t v){b[p++]=v;return p;}
static size_t i32(uint8_t*b,size_t p,int32_t v){put_i32(b+p,v);return p+4;}
int main(void) {
    uint8_t code[128]; size_t p=0,loop,if_operand,goto_operand,end;
    TurboJSEngineBytecodeInfo bc; TurboJSBytecodeAnalysis a; TurboJSBytecodeCFG cfg; TurboJSBytecodeRegionPlan plan; TurboJSBytecodeRegionStateGraph state; TurboJSSSAGraph ssa; TurboJSLinearScanResult allocation; TurboJSRegionNativeFunction *native=NULL; TurboJSRegionNativeStats native_stats; TurboJSIRDiagnostic d={0};
    p=u8(code,p,OP_get_arg0);p=u8(code,p,OP_put_loc0);
    p=u8(code,p,OP_push_i32);p=i32(code,p,0);p=u8(code,p,OP_put_loc1);
    loop=p;
    p=u8(code,p,OP_push_i32);p=i32(code,p,0);p=u8(code,p,OP_get_loc0);p=u8(code,p,OP_lt);
    p=u8(code,p,OP_if_false);if_operand=p;p=i32(code,p,0);
    p=u8(code,p,OP_get_loc1);p=u8(code,p,OP_get_loc0);p=u8(code,p,OP_add);p=u8(code,p,OP_put_loc1);
    p=u8(code,p,OP_get_loc0);p=u8(code,p,OP_push_i32);p=i32(code,p,1);p=u8(code,p,OP_sub);p=u8(code,p,OP_put_loc0);
    p=u8(code,p,OP_goto);goto_operand=p;p=i32(code,p,0);
    end=p;p=u8(code,p,OP_get_loc1);p=u8(code,p,OP_return);
    put_i32(code+if_operand,(int32_t)end-(int32_t)if_operand);
    put_i32(code+goto_operand,(int32_t)loop-(int32_t)goto_operand);
    memset(&bc,0,sizeof(bc));bc.bytecode=code;bc.bytecode_length=p;bc.argument_count=1;bc.local_count=2;bc.stack_size=3;
    CHECK(TurboJS_EngineBytecodeAnalyze(&bc,&a,&d)==TURBOJS_IR_OK);
    CHECK(a.instruction_count==19);CHECK(a.basic_block_count==4);CHECK(a.branch_count==2);CHECK(a.backedge_count==1);
    CHECK(a.direct_instruction_count==a.instruction_count);CHECK(a.helper_instruction_count==0);CHECK(a.maximum_stack_depth==2);
    memset(&cfg,0,sizeof(cfg));CHECK(TurboJS_EngineBytecodeBuildCFG(&bc,&cfg,&d)==TURBOJS_IR_OK);
    CHECK(cfg.block_count==4);CHECK(cfg.blocks[1].flags&TURBOJS_BYTECODE_BLOCK_LOOP_HEADER);
    CHECK(cfg.blocks[2].flags&TURBOJS_BYTECODE_BLOCK_HAS_BACKEDGE);CHECK(cfg.blocks[1].successor_count==2);
    CHECK(cfg.blocks[2].successor_count==1);CHECK(cfg.blocks[3].successor_count==0);CHECK(cfg.maximum_stack_depth==2);
    CHECK(cfg.blocks[1].predecessor_count==2);CHECK(cfg.blocks[0].predecessor_count==0);
    TurboJS_EngineBytecodeCFGDestroy(&cfg);
    memset(&plan,0,sizeof(plan));CHECK(TurboJS_EngineBytecodeBuildRegionPlan(&bc,&plan,&d)==TURBOJS_IR_OK);
    CHECK(plan.block_count==4);CHECK(plan.reachable_block_count==4);CHECK(plan.loop_header_count==1);
    CHECK(plan.merge_block_count==1);CHECK(plan.estimated_local_phis==2);CHECK(plan.estimated_stack_phis==0);
    CHECK(plan.blocks[1].flags&TURBOJS_BYTECODE_REGION_MERGE);
    TurboJS_EngineBytecodeRegionPlanDestroy(&plan);
    memset(&state,0,sizeof(state));CHECK(TurboJS_EngineBytecodeBuildRegionStateGraph(&bc,&state,&d)==TURBOJS_IR_OK);
    CHECK(state.active_phi_count==2);CHECK(state.blocks[1].active_local_phis==2);CHECK(state.phi_input_count==4);
    { size_t vi; uint32_t phis=0; for(vi=0;vi<state.value_count;vi++){
        if(state.values[vi].kind==TURBOJS_REGION_VALUE_PHI){
            CHECK(state.values[vi].block==1);CHECK(state.values[vi].phi_input_count==2);phis++;
        }
      } CHECK(phis==2); }
    TurboJS_EngineBytecodeRegionStateGraphDestroy(&state);
    TurboJS_SSAGraphInit(&ssa);CHECK(TurboJS_EngineBytecodeRegionBuildSSA(&bc,&ssa,&d)==TURBOJS_IR_OK);
    CHECK(ssa.block_count==4);CHECK(ssa.entry_block==0);CHECK(TurboJS_SSAVerify(&ssa));
    { size_t svi; uint32_t phis=0,branches=0,returns=0; for(svi=0;svi<ssa.value_count;svi++){
        if(ssa.values[svi].opcode==TURBOJS_SSA_PHI)phis++;
        if(ssa.values[svi].opcode==TURBOJS_SSA_BRANCH_FALSE)branches++;
        if(ssa.values[svi].opcode==TURBOJS_SSA_RETURN)returns++;
      } CHECK(phis==2);CHECK(branches==1);CHECK(returns==1); }
    memset(&allocation,0,sizeof(allocation));CHECK(TurboJS_LinearScanAllocate(&ssa,6,8,&allocation)==TURBOJS_IR_OK);
    CHECK(allocation.interval_count>0);TurboJS_LinearScanResultDestroy(&allocation);
    memset(&native_stats,0,sizeof(native_stats));CHECK(TurboJS_RegionNativeCompile(&ssa,&native,&native_stats,&d)==TURBOJS_IR_OK);
    CHECK(native!=NULL);CHECK(native_stats.block_count==4);CHECK(native_stats.phi_count==2);CHECK(native_stats.edge_move_count>=1);CHECK(native_stats.register_values>0);CHECK(native_stats.frame_bytes<native_stats.allocated_intervals*8u);CHECK(native_stats.native_code_bytes>0);CHECK(native_stats.compile_time_ns>0);CHECK(native_stats.phi_coalesce_candidates>=1);CHECK(native_stats.phi_coalesce_successes>=1);CHECK(native_stats.phi_coalesce_successes+native_stats.phi_coalesce_rejected==native_stats.phi_coalesce_candidates);CHECK(native_stats.fragment_count>0);CHECK(native_stats.split_count==0||native_stats.fragment_count>=native_stats.allocated_intervals);CHECK(TurboJS_RegionNativeWriteCode(native,"phase69-region.bin"));CHECK(TurboJS_RegionNativeWriteAllocation(native,"phase69-allocation.txt"));remove("phase69-region.bin");remove("phase69-allocation.txt");
    { int64_t args[]={1000000},result=-1; CHECK(TurboJS_RegionNativeInvoke(native,args,1,&result)==TURBOJS_IR_OK); CHECK(result==500000500000LL); }
    TurboJS_RegionNativeFunctionDestroy(native);native=NULL;
    TurboJS_SSAGraphDestroy(&ssa);
    /* A target inside push_i32 must be rejected instead of becoming a corrupt block. */
    put_i32(code+if_operand,(int32_t)(loop+1)-(int32_t)if_operand);
    CHECK(TurboJS_EngineBytecodeBuildCFG(&bc,&cfg,&d)==TURBOJS_IR_INVALID_TARGET);
    {
        uint8_t bad_merge[] = {
            OP_push_true, OP_if_false8, 8,
            OP_push_i32, 1, 0, 0, 0,
            OP_goto8, 1, OP_return_undef
        };
        TurboJSEngineBytecodeInfo bad={bad_merge,sizeof(bad_merge),0,0,2,TURBOJS_ENGINE_NUMERIC_INT32};
        CHECK(TurboJS_EngineBytecodeBuildCFG(&bad,&cfg,&d)==TURBOJS_IR_INVALID_OPCODE);
    }
    puts("Engine bytecode CFG, symbolic state, SSA, and native multi-block execution passed");
    return 0;
}
