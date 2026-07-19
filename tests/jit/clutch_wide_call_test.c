#include <stdint.h>
#include <stdio.h>
#include "jit.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static int emit_sum4(TurboJSIRFunction *f)
{
    uint16_t a,b,c,d,t0,t1,out;
    TurboJS_IRFunctionInit(f,4);
    a=TurboJS_IRAllocateRegister(f); b=TurboJS_IRAllocateRegister(f);
    c=TurboJS_IRAllocateRegister(f); d=TurboJS_IRAllocateRegister(f);
    t0=TurboJS_IRAllocateRegister(f); t1=TurboJS_IRAllocateRegister(f);
    out=TurboJS_IRAllocateRegister(f);
    return out!=TURBOJS_IR_NO_REGISTER &&
        TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,0,0,0})==TURBOJS_IR_OK &&
        TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,b,0,0,1,0,1})==TURBOJS_IR_OK &&
        TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,c,0,0,2,0,2})==TURBOJS_IR_OK &&
        TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,d,0,0,3,0,3})==TURBOJS_IR_OK &&
        TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ADD_I64,t0,a,b,0,0,4})==TURBOJS_IR_OK &&
        TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ADD_I64,t1,c,d,0,0,5})==TURBOJS_IR_OK &&
        TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ADD_I64,out,t0,t1,0,0,6})==TURBOJS_IR_OK &&
        TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,out,0,0,0,7})==TURBOJS_IR_OK;
}

int main(void)
{
    TurboJSIRFunction callee,caller;
    TurboJSCodeCache *cache;
    const TurboJSNativeFunction *native_callee=NULL;
    TurboJSNativeFunction *native_caller=NULL;
    TurboJSNativeEntryHandle handle;
    TurboJSClutchCallSite site;
    TurboJSIRDiagnostic diag;
    uint16_t a,b,c,d,r;
    int key = 0;
    int64_t args[4]={1,2,4,8}, result=0;

    CHECK(emit_sum4(&callee));
    cache=TurboJS_CodeCacheCreate(8,1u<<20); CHECK(cache);
    CHECK(TurboJS_CodeCacheCompile(cache,&key,&callee,&native_callee,&diag)==TURBOJS_IR_OK);
    TurboJS_NativeEntryHandleInit(&handle);
    CHECK(TurboJS_CodeCacheAttachEntryHandle(cache,&key,&handle,TURBOJS_NATIVE_ENTRY_INT32,4)==TURBOJS_IR_OK);
    TurboJS_ClutchCallSiteInit(&site,&handle,handle.generation,TURBOJS_NATIVE_ENTRY_INT32,4);

    TurboJS_IRFunctionInit(&caller,4);
    a=TurboJS_IRAllocateRegister(&caller); b=TurboJS_IRAllocateRegister(&caller);
    c=TurboJS_IRAllocateRegister(&caller); d=TurboJS_IRAllocateRegister(&caller);
    r=TurboJS_IRAllocateRegister(&caller);
    CHECK(r!=TURBOJS_IR_NO_REGISTER);
    CHECK(TurboJS_ClutchCallSiteSetArgument(&site,0,d)==TURBOJS_IR_OK);
    CHECK(TurboJS_ClutchCallSiteSetArgument(&site,1,c)==TURBOJS_IR_OK);
    CHECK(TurboJS_ClutchCallSiteSetArgument(&site,2,b)==TURBOJS_IR_OK);
    CHECK(TurboJS_ClutchCallSiteSetArgument(&site,3,a)==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,0,0,0})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,b,0,0,1,0,1})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,c,0,0,2,0,2})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,d,0,0,3,0,3})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller,(TurboJSIRInstruction){TURBOJS_IR_CALL_NATIVE_I64,r,0,0,(int64_t)(uintptr_t)&site,0,4})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller,(TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,r,0,0,0,5})==TURBOJS_IR_OK);
    CHECK(TurboJS_BaselineCompile(&caller,&native_caller,&diag)==TURBOJS_IR_OK);
    CHECK(TurboJS_NativeInvoke(native_caller,args,4,&result)==TURBOJS_IR_OK);
    CHECK(result==15);
    CHECK(TurboJS_NativeStackMapAt(native_caller,0)->kind==TURBOJS_SAFEPOINT_CLUTCH_CALL);

    TurboJS_CodeCacheClear(cache);
    CHECK(TurboJS_NativeInvoke(native_caller,args,4,&result)==TURBOJS_IR_BAILOUT);

    TurboJS_NativeFunctionDestroy(native_caller);
    TurboJS_IRFunctionDestroy(&caller);
    TurboJS_CodeCacheDestroy(cache);
    TurboJS_IRFunctionDestroy(&callee);
    puts("TurboJS Clutch wide call passed");
    return 0;
}
