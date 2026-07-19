#include "jit.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed: %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while(0)

static int test_overflow_not_folded(void)
{
    TurboJSIRFunction ir;
    TurboJSSSAGraph graph;
    TurboJSIRDiagnostic diagnostic = {0};
    TurboJSSSAOptimizationStats stats;
    TurboJS_IRFunctionInit(&ir, 0);
    TurboJS_SSAGraphInit(&graph);
    CHECK(TurboJS_IRAllocateRegister(&ir) != TURBOJS_IR_NO_REGISTER);
    CHECK(TurboJS_IRAllocateRegister(&ir) != TURBOJS_IR_NO_REGISTER);
    CHECK(TurboJS_IRAllocateRegister(&ir) != TURBOJS_IR_NO_REGISTER);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){.opcode=TURBOJS_IR_CONSTANT_I64,.destination=0,.immediate=INT64_MAX}) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){.opcode=TURBOJS_IR_CONSTANT_I64,.destination=1,.immediate=1}) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){.opcode=TURBOJS_IR_ADD_I64,.destination=2,.left=0,.right=1}) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){.opcode=TURBOJS_IR_RETURN_I64,.left=2}) == TURBOJS_IR_OK);
    CHECK(TurboJS_SSABuildFromIR(&ir, &graph, &diagnostic) == TURBOJS_IR_OK);
    stats = TurboJS_SSAOptimize(&graph);
    CHECK(stats.constants_folded == 0);
    CHECK(graph.values[2].opcode == TURBOJS_SSA_ADD_I64);
    TurboJS_SSAGraphDestroy(&graph);
    TurboJS_IRFunctionDestroy(&ir);
    return 0;
}


static int test_type_inference_and_cse(void)
{
    TurboJSIRFunction ir;
    TurboJSSSAGraph graph;
    TurboJSIRDiagnostic diagnostic = {0};
    TurboJSSSAOptimizationStats stats;
    TurboJS_IRFunctionInit(&ir, 0);
    TurboJS_SSAGraphInit(&graph);
    for (int i = 0; i < 5; ++i)
        CHECK(TurboJS_IRAllocateRegister(&ir) != TURBOJS_IR_NO_REGISTER);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){.opcode=TURBOJS_IR_ARGUMENT,.destination=0,.immediate=0}) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){.opcode=TURBOJS_IR_CONSTANT_I64,.destination=1,.immediate=7}) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){.opcode=TURBOJS_IR_ADD_I64,.destination=2,.left=0,.right=1}) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){.opcode=TURBOJS_IR_ADD_I64,.destination=3,.left=0,.right=1}) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){.opcode=TURBOJS_IR_ADD_I64,.destination=4,.left=2,.right=3}) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){.opcode=TURBOJS_IR_RETURN_I64,.left=4}) == TURBOJS_IR_OK);
    CHECK(TurboJS_SSABuildFromIR(&ir, &graph, &diagnostic) == TURBOJS_IR_OK);
    graph.values[0].type = TURBOJS_SSA_TYPE_INT32;
    stats = TurboJS_SSAOptimize(&graph);
    CHECK(stats.types_inferred >= 3);
    CHECK(stats.expressions_eliminated == 1);
    CHECK(graph.values[1].type == TURBOJS_SSA_TYPE_INT32);
    CHECK(graph.values[2].type == TURBOJS_SSA_TYPE_INT32);
    CHECK(graph.values[3].removed);
    CHECK(graph.values[4].left == 2 && graph.values[4].right == 2);
    CHECK(TurboJS_SSAVerify(&graph));
    TurboJS_SSAGraphDestroy(&graph);
    TurboJS_IRFunctionDestroy(&ir);
    return 0;
}

int main(void){
 TurboJSIRFunction ir; TurboJSSSAGraph g; TurboJSIRDiagnostic d={0}; TurboJSSSAOptimizationStats st; uint32_t phi;
 TurboJS_IRFunctionInit(&ir, 0); TurboJS_SSAGraphInit(&g);
 for (int i=0;i<4;i++) CHECK(TurboJS_IRAllocateRegister(&ir) != TURBOJS_IR_NO_REGISTER);
 CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){.opcode=TURBOJS_IR_CONSTANT_I64,.destination=0,.immediate=20})==TURBOJS_IR_OK);
 CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){.opcode=TURBOJS_IR_CONSTANT_I64,.destination=1,.immediate=22})==TURBOJS_IR_OK);
 CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){.opcode=TURBOJS_IR_ADD_I64,.destination=2,.left=0,.right=1})==TURBOJS_IR_OK);
 CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){.opcode=TURBOJS_IR_CONSTANT_I64,.destination=3,.immediate=99})==TURBOJS_IR_OK);
 CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){.opcode=TURBOJS_IR_RETURN_I64,.left=2})==TURBOJS_IR_OK);
 CHECK(TurboJS_SSABuildFromIR(&ir,&g,&d)==TURBOJS_IR_OK); CHECK(TurboJS_SSAVerify(&g)); CHECK(g.block_count==1); CHECK(g.value_count==5);
 st=TurboJS_SSAOptimize(&g); CHECK(st.constants_folded==1); CHECK(st.values_removed>=1); CHECK(g.values[2].opcode==TURBOJS_SSA_CONSTANT_I64); CHECK(g.values[2].immediate==42); CHECK(g.values[3].removed);
 CHECK(TurboJS_SSAAddPhi(&g,0,0,1,TURBOJS_SSA_TYPE_INT64,&phi)==TURBOJS_IR_OK); CHECK(g.values[phi].opcode==TURBOJS_SSA_PHI); CHECK(TurboJS_SSAVerify(&g));
 TurboJS_SSAGraphDestroy(&g); TurboJS_IRFunctionDestroy(&ir); CHECK(test_overflow_not_folded()==0); CHECK(test_type_inference_and_cse()==0); puts("SSA graph and optimization passes passed"); return 0;
}
