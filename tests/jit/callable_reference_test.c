#include <stdint.h>
#include <stdio.h>
#include "jit.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)
static int emit_add(TurboJSIRFunction *f) {
    uint16_t a,b,r; TurboJS_IRFunctionInit(f,2);
    a=TurboJS_IRAllocateRegister(f); b=TurboJS_IRAllocateRegister(f); r=TurboJS_IRAllocateRegister(f);
    return r!=TURBOJS_IR_NO_REGISTER &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,0,0,0})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,b,0,0,1,0,0})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ADD_I64,r,a,b,0,0,1})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,r,0,0,0,2})==TURBOJS_IR_OK;
}
int main(void) {
    TurboJSCodeCache *cache=TurboJS_CodeCacheCreate(8,1u<<20);
    TurboJSIRFunction ir; TurboJSIRDiagnostic d; TurboJSNativeEntryHandle h;
    TurboJSCallableReference ref; const TurboJSNativeFunction *native=NULL;
    int key=1, environment=77; int64_t args[2]={20,22}, result=0;
    CHECK(cache); TurboJS_NativeEntryHandleInit(&h); CHECK(emit_add(&ir));
    CHECK(TurboJS_CodeCacheCompile(cache,&key,&ir,&native,&d)==TURBOJS_IR_OK);
    TurboJS_IRFunctionDestroy(&ir);
    CHECK(TurboJS_CodeCacheAttachEntryHandleIdentity(cache,&key,&h,TURBOJS_NATIVE_ENTRY_INT32,2,UINT64_C(0xCA11AB1E))==TURBOJS_IR_OK);
    TurboJS_CallableReferenceInit(&ref,UINT64_C(0xCA11AB1E),&h,h.generation,TURBOJS_NATIVE_ENTRY_INT32,2,&environment);
    CHECK(ref.closure_environment==&environment);
    CHECK(TurboJS_CallableReferenceIsLive(&ref));
    CHECK(TurboJS_CallableReferenceInvokeI64(&ref,args,2,&result)==TURBOJS_IR_OK && result==42);
    TurboJS_CodeCacheInvalidate(cache,&key);
    CHECK(!TurboJS_CallableReferenceIsLive(&ref));
    CHECK(TurboJS_CallableReferenceInvokeI64(&ref,args,2,&result)==TURBOJS_IR_UNSUPPORTED);
    TurboJS_CodeCacheDestroy(cache); puts("Callable reference passed"); return 0;
}
