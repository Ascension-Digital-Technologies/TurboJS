#include <stdio.h>
#include <string.h>
#include "jit.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed: %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)

static int build_add(TurboJSIRFunction *ir)
{
    TurboJSIRInstruction in;
    uint16_t a, b, sum;
    TurboJS_IRFunctionInit(ir, 2);
    a=TurboJS_IRAllocateRegister(ir); b=TurboJS_IRAllocateRegister(ir); sum=TurboJS_IRAllocateRegister(ir);
    memset(&in,0,sizeof(in)); in.destination=in.left=in.right=TURBOJS_IR_NO_REGISTER;
    in.opcode=TURBOJS_IR_ARGUMENT; in.destination=a; in.immediate=0; CHECK(TurboJS_IREmit(ir,in)==TURBOJS_IR_OK);
    in.destination=b; in.immediate=1; CHECK(TurboJS_IREmit(ir,in)==TURBOJS_IR_OK);
    in.opcode=TURBOJS_IR_ADD_I64; in.destination=sum; in.left=a; in.right=b; CHECK(TurboJS_IREmit(ir,in)==TURBOJS_IR_OK);
    in.opcode=TURBOJS_IR_RETURN_I64; in.destination=TURBOJS_IR_NO_REGISTER; in.left=sum; in.right=TURBOJS_IR_NO_REGISTER; CHECK(TurboJS_IREmit(ir,in)==TURBOJS_IR_OK);
    return 0;
}

int main(void)
{
    TurboJSIRFunction ir;
    TurboJSCodeCache *cache;
    TurboJSTieredFunction tiered;
    TurboJSOptimizationPolicy policy=TurboJS_OptimizationPolicyDefault();
    TurboJSTieredResult route=TURBOJS_TIERED_INTERPRETED;
    TurboJSIRDiagnostic diagnostic;
    TurboJSTieredStats stats;
    int key, i; int64_t args[2], result=0;
    CHECK(build_add(&ir)==0);
    cache=TurboJS_CodeCacheCreate(8,65536); CHECK(cache!=NULL);
    policy.minimum_executions=5; policy.maximum_bailouts=2; policy.maximum_exceptions=1;
    TurboJS_TieredFunctionInitAdvanced(&tiered,&key,2,5,&policy);
    for(i=0;i<8;i++) {
        args[0]=i; args[1]=2;
        CHECK(TurboJS_TieredInvoke(&tiered,cache,&ir,args,2,&result,&route,&diagnostic)==TURBOJS_IR_OK);
        CHECK(result==i+2);
    }
    stats=TurboJS_TieredFunctionGetStats(&tiered);
    CHECK(stats.baseline_compilations==1);
    CHECK(stats.optimized_compilations==1);
    CHECK(stats.optimized_calls>=1);
    CHECK(route==TURBOJS_TIERED_OPTIMIZED_NATIVE || route==TURBOJS_TIERED_OPTIMIZED_COMPILED);
    TurboJS_TieredFunctionInvalidateOptimized(&tiered);
    CHECK(TurboJS_TieredFunctionGetStats(&tiered).optimized_invalidations==1);
    TurboJS_TieredFunctionDestroy(&tiered);
    TurboJS_CodeCacheDestroy(cache);
    TurboJS_IRFunctionDestroy(&ir);
    puts("automatic baseline-to-optimizing tier promotion passed");
    return 0;
}
