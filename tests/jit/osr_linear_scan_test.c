#include "jit.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed: %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)
int main(void) {
    TurboJSOSRState osr;
    TurboJSSSAGraph graph;
    TurboJSLinearScanResult allocation;
    TurboJSIRFunction ir;
    TurboJSIRDiagnostic diagnostic;
    uint16_t a,b,c,d;
    TurboJS_OSRStateInit(&osr, 7, 3);
    CHECK(TurboJS_OSRObserveBackedge(&osr)==TURBOJS_OSR_CONTINUE_INTERPRETING);
    CHECK(TurboJS_OSRObserveBackedge(&osr)==TURBOJS_OSR_CONTINUE_INTERPRETING);
    CHECK(TurboJS_OSRObserveBackedge(&osr)==TURBOJS_OSR_REQUEST_COMPILE);
    TurboJS_OSRMarkCodeReady(&osr);
    CHECK(TurboJS_OSRObserveBackedge(&osr)==TURBOJS_OSR_ENTER_READY_CODE);
    TurboJS_OSRRecordEntry(&osr); CHECK(osr.entry_count==1);
    TurboJS_OSRRecordBailout(&osr,2); CHECK(!osr.disabled);
    TurboJS_OSRMarkCodeReady(&osr); TurboJS_OSRRecordBailout(&osr,2); CHECK(osr.disabled);

    TurboJS_IRFunctionInit(&ir, 2);
    a=TurboJS_IRAllocateRegister(&ir); b=TurboJS_IRAllocateRegister(&ir);
    c=TurboJS_IRAllocateRegister(&ir); d=TurboJS_IRAllocateRegister(&ir);
    CHECK(a!=UINT16_MAX&&b!=UINT16_MAX&&c!=UINT16_MAX&&d!=UINT16_MAX);
    CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,0,0})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,b,0,0,0,1})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){TURBOJS_IR_ADD_I64,c,a,b,0,0})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){TURBOJS_IR_MUL_I64,d,c,b,0,0})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,d,0,0,0})==TURBOJS_IR_OK);
    TurboJS_SSAGraphInit(&graph); memset(&diagnostic,0,sizeof(diagnostic));
    CHECK(TurboJS_SSABuildFromIR(&ir,&graph,&diagnostic)==TURBOJS_IR_OK);
    memset(&allocation,0,sizeof(allocation));
    CHECK(TurboJS_LinearScanAllocate(&graph,1,1,&allocation)==TURBOJS_IR_OK);
    CHECK(allocation.interval_count==graph.value_count);
    CHECK(allocation.fragment_count>=allocation.interval_count);
    CHECK(allocation.value_position_count==graph.value_count);
    CHECK(allocation.block_position_count==graph.block_count);
    CHECK(allocation.spill_slot_count>=1);
    TurboJS_LinearScanResultDestroy(&allocation);
    TurboJS_SSAGraphDestroy(&graph); TurboJS_IRFunctionDestroy(&ir);
    puts("OSR state and linear-scan allocation passed");
    return 0;
}
