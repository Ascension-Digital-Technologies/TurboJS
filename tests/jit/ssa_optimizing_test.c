#include "jit.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed: %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while(0)
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
 TurboJS_SSAGraphDestroy(&g); TurboJS_IRFunctionDestroy(&ir); puts("SSA graph and optimization passes passed"); return 0;
}
