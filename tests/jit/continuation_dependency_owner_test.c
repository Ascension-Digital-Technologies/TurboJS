#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "jit.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed: %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)

static TurboJSIRStatus helper(void *opaque, const TurboJSBoxedDeoptFrame *frame,
                              const TurboJSIRInstruction *instruction,
                              TurboJSBoxedValue *result)
{
    (void)opaque;
    result->tag = TURBOJS_BOXED_INT64;
    result->as.integer = frame->registers[instruction->left].as.integer;
    return TURBOJS_IR_OK;
}

static int emit_add(TurboJSIRFunction *f)
{
    uint16_t a,b,r;
    TurboJS_IRFunctionInit(f,2);
    a=TurboJS_IRAllocateRegister(f); b=TurboJS_IRAllocateRegister(f); r=TurboJS_IRAllocateRegister(f);
    return r!=TURBOJS_IR_NO_REGISTER &&
        TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,0,0,0})==TURBOJS_IR_OK &&
        TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,b,0,0,1,0,0})==TURBOJS_IR_OK &&
        TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ADD_I64,r,a,b,0,0,1})==TURBOJS_IR_OK &&
        TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,r,0,0,0,2})==TURBOJS_IR_OK;
}

int main(void)
{
    TurboJSCodeCache *vault=TurboJS_CodeCacheCreate(16,1u<<20);
    TurboJSIRFunction callee, source;
    TurboJSIRDiagnostic d;
    const TurboJSNativeFunction *callee_native=NULL;
    TurboJSNativeFunction *source_native=NULL;
    TurboJSNativeEntryHandle handle;
    TurboJSClutchCallSite *site;
    TurboJSRuntimeHelperTable helpers;
    int callee_key=1;
    int64_t args[2]={19,23}, result=0;
    uint16_t a,b,hv,call;
    CHECK(vault && emit_add(&callee));
    CHECK(TurboJS_CodeCacheCompile(vault,&callee_key,&callee,&callee_native,&d)==TURBOJS_IR_OK);
    TurboJS_NativeEntryHandleInit(&handle);
    CHECK(TurboJS_CodeCacheAttachEntryHandle(vault,&callee_key,&handle,TURBOJS_NATIVE_ENTRY_INT32,2)==TURBOJS_IR_OK);

    TurboJS_IRFunctionInit(&source,2);
    a=TurboJS_IRAllocateRegister(&source); b=TurboJS_IRAllocateRegister(&source);
    hv=TurboJS_IRAllocateRegister(&source); call=TurboJS_IRAllocateRegister(&source);
    site=TurboJS_IRAllocateClutchCallSite(&source);
    CHECK(site && call!=TURBOJS_IR_NO_REGISTER);
    TurboJS_ClutchCallSiteInit(site,&handle,handle.generation,TURBOJS_NATIVE_ENTRY_INT32,2);
    CHECK(TurboJS_ClutchCallSiteSetArgument(site,0,a)==TURBOJS_IR_OK);
    CHECK(TurboJS_ClutchCallSiteSetArgument(site,1,b)==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&source,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,0,0,0})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&source,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,b,0,0,1,0,0})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&source,(TurboJSIRInstruction){TURBOJS_IR_RUNTIME_HELPER,hv,a,0,1,0,2})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&source,(TurboJSIRInstruction){TURBOJS_IR_CALL_NATIVE_I64,call,a,b,(int64_t)(uintptr_t)site,0,3})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&source,(TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,call,0,0,0,4})==TURBOJS_IR_OK);
    CHECK(TurboJS_BaselineCompile(&source,&source_native,&d)==TURBOJS_IR_OK);

    TurboJS_RuntimeHelperTableInit(&helpers,NULL);
    TurboJS_RuntimeHelperAttachContinuationVault(&helpers,vault);
    CHECK(TurboJS_RuntimeHelperRegister(&helpers,1,helper,NULL)==TURBOJS_IR_OK);
    CHECK(TurboJS_NativeInvokeWithRuntime(source_native,&source,&helpers,args,2,&result)==TURBOJS_IR_OK);
    CHECK(result==42);
    CHECK(TurboJS_CodeCacheGetStats(vault).continuation_entry_count==1);

    TurboJS_CodeCacheInvalidate(vault,&callee_key);
    CHECK(TurboJS_CodeCacheGetStats(vault).continuation_dependency_invalidations==1);
    result=0;
    CHECK(TurboJS_NativeInvokeWithRuntime(source_native,&source,&helpers,args,2,&result)!=TURBOJS_IR_OK);

    TurboJS_RuntimeHelperTableDestroy(&helpers);
    TurboJS_NativeFunctionDestroy(source_native);
    TurboJS_IRFunctionDestroy(&source);
    TurboJS_IRFunctionDestroy(&callee);
    TurboJS_CodeCacheDestroy(vault);
    puts("continuation dependency ownership passed");
    return 0;
}
