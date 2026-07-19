#include <stdio.h>
#include "jit.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed: %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)

static int emit_identity(TurboJSIRFunction *f)
{
    uint16_t a;
    TurboJS_IRFunctionInit(f,1);
    a=TurboJS_IRAllocateRegister(f);
    return a!=TURBOJS_IR_NO_REGISTER &&
        TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,0,0,0})==TURBOJS_IR_OK &&
        TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,a,0,0,0,1})==TURBOJS_IR_OK;
}

int main(void)
{
    TurboJSCodeCache *vault=TurboJS_CodeCacheCreate(3,1u<<20);
    TurboJSIRFunction a_ir,b_ir,d_ir,c_ir;
    TurboJSIRDiagnostic diag;
    const TurboJSNativeFunction *a=NULL,*b=NULL,*d=NULL,*c_lookup=NULL;
    TurboJSNativeFunction *c_native=NULL;
    int ka=1,kb=2,kd=3;
    size_t prologue=0;
    CHECK(vault && emit_identity(&a_ir) && emit_identity(&b_ir) && emit_identity(&d_ir) && emit_identity(&c_ir));
    CHECK(TurboJS_CodeCacheCompile(vault,&ka,&a_ir,&a,&diag)==TURBOJS_IR_OK);
    CHECK(TurboJS_BaselineCompile(&c_ir,&c_native,&diag)==TURBOJS_IR_OK);
    CHECK(TurboJS_CodeCacheStoreContinuation(vault,77,1,1,0,c_native,&c_lookup)==TURBOJS_IR_OK);
    CHECK(TurboJS_CodeCacheCompile(vault,&kb,&b_ir,&b,&diag)==TURBOJS_IR_OK);
    for(int i=0;i<8;i++) CHECK(TurboJS_CodeCacheLookupContinuation(vault,77,1,1,&prologue)==c_lookup);
    CHECK(TurboJS_CodeCacheCompile(vault,&kd,&d_ir,&d,&diag)==TURBOJS_IR_OK);
    CHECK(TurboJS_CodeCacheLookup(vault,&ka)==NULL);
    CHECK(TurboJS_CodeCacheLookupContinuation(vault,77,1,1,&prologue)==c_lookup);
    CHECK(TurboJS_CodeCacheGetStats(vault).weighted_function_evictions==1);
    CHECK(TurboJS_CodeCacheGetStats(vault).weighted_continuation_evictions==0);
    TurboJS_CodeCacheDestroy(vault);
    TurboJS_IRFunctionDestroy(&a_ir); TurboJS_IRFunctionDestroy(&b_ir);
    TurboJS_IRFunctionDestroy(&d_ir); TurboJS_IRFunctionDestroy(&c_ir);
    puts("Vault weighted aging passed");
    return 0;
}
