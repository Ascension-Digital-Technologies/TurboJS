#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "jit.h"
#include "internal/monotonic_clock.h"

#ifndef ITERATIONS
#define ITERATIONS 1000000u
#endif

static uint64_t now_ns(void) {
    return turbojs_monotonic_now_ns();
}

static int emit_add(TurboJSIRFunction *f) {
    uint16_t a,b,r;
    TurboJS_IRFunctionInit(f,2);
    a=TurboJS_IRAllocateRegister(f); b=TurboJS_IRAllocateRegister(f); r=TurboJS_IRAllocateRegister(f);
    return r!=TURBOJS_IR_NO_REGISTER &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,0,0,0})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,b,0,0,1,0,0})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ADD_I64,r,a,b,0,0,1})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,r,0,0,0,2})==TURBOJS_IR_OK;
}

int main(void) {
    TurboJSIRFunction callee,caller; TurboJSCodeCache *cache; TurboJSNativeFunction *native_caller=NULL;
    const TurboJSNativeFunction *native_callee=NULL; TurboJSNativeEntryHandle handle; TurboJSClutchCallSite site;
    TurboJSIRDiagnostic d; int key = 0; int64_t args[2]={20,22}, result=0, sum=0; uint16_t a,b,r; uint64_t t0,t1; unsigned i;
    if(!emit_add(&callee)) return 1;
    cache=TurboJS_CodeCacheCreate(8,1u<<20); if(!cache) return 2;
    if(TurboJS_CodeCacheCompile(cache,&key,&callee,&native_callee,&d)!=TURBOJS_IR_OK) return 3;
    TurboJS_NativeEntryHandleInit(&handle);
    if(TurboJS_CodeCacheAttachEntryHandle(cache,&key,&handle,TURBOJS_NATIVE_ENTRY_INT32,2)!=TURBOJS_IR_OK) return 4;
    TurboJS_ClutchCallSiteInit(&site,&handle,handle.generation,TURBOJS_NATIVE_ENTRY_INT32,2);
    TurboJS_IRFunctionInit(&caller,2); a=TurboJS_IRAllocateRegister(&caller); b=TurboJS_IRAllocateRegister(&caller); r=TurboJS_IRAllocateRegister(&caller);
    if(TurboJS_ClutchCallSiteSetArgument(&site,0,a)!=TURBOJS_IR_OK || TurboJS_ClutchCallSiteSetArgument(&site,1,b)!=TURBOJS_IR_OK)return 5;
    if(TurboJS_IREmit(&caller,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,0,0,0})!=TURBOJS_IR_OK ||
       TurboJS_IREmit(&caller,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,b,0,0,1,0,0})!=TURBOJS_IR_OK ||
       TurboJS_IREmit(&caller,(TurboJSIRInstruction){TURBOJS_IR_CALL_NATIVE_I64,r,a,b,(int64_t)(uintptr_t)&site,0,2})!=TURBOJS_IR_OK ||
       TurboJS_IREmit(&caller,(TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,r,0,0,0,3})!=TURBOJS_IR_OK) return 5;
    if(TurboJS_BaselineCompile(&caller,&native_caller,&d)!=TURBOJS_IR_OK) return 6;
    t0=now_ns(); for(i=0;i<ITERATIONS;i++){ if(TurboJS_ClutchCallSiteInvokeI64(&site,args,&result)!=TURBOJS_IR_OK)return 7; sum+=result; } t1=now_ns();
    printf("portable_clutch_ns_per_call=%.3f\n",(double)(t1-t0)/ITERATIONS);
    sum=0; t0=now_ns(); for(i=0;i<ITERATIONS;i++){ if(TurboJS_NativeInvoke(native_caller,args,2,&result)!=TURBOJS_IR_OK)return 8; sum+=result; } t1=now_ns();
    printf("gearbox_call_js_ns_per_call=%.3f\n",(double)(t1-t0)/ITERATIONS);
    printf("result=%" PRId64 " checksum=%" PRId64 " code_bytes=%zu stack_maps=%zu\n",result,sum,TurboJS_NativeCodeSize(native_caller),TurboJS_NativeStackMapCount(native_caller));
    TurboJS_NativeFunctionDestroy(native_caller); TurboJS_IRFunctionDestroy(&caller); TurboJS_CodeCacheDestroy(cache); TurboJS_IRFunctionDestroy(&callee);
    return result==42 && sum==(int64_t)ITERATIONS*42 ? 0 : 9;
}
