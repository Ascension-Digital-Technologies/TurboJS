#include <stdint.h>
#include <stdio.h>
#include "jit.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)

static int emit_method(TurboJSIRFunction *f)
{
    uint16_t receiver, a, b, t, r;
    TurboJS_IRFunctionInit(f, 3);
    receiver=TurboJS_IRAllocateRegister(f); a=TurboJS_IRAllocateRegister(f);
    b=TurboJS_IRAllocateRegister(f); t=TurboJS_IRAllocateRegister(f);
    r=TurboJS_IRAllocateRegister(f);
    return r!=TURBOJS_IR_NO_REGISTER &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,receiver,0,0,0,0,0})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,1,0,1})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,b,0,0,2,0,2})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ADD_I64,t,receiver,a,0,0,3})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ADD_I64,r,t,b,0,0,4})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,r,0,0,0,5})==TURBOJS_IR_OK;
}

int main(void)
{
    TurboJSCodeCache *cache=TurboJS_CodeCacheCreate(16,1u<<20);
    TurboJSIRFunction callee_ir, caller_ir;
    TurboJSIRDiagnostic diagnostic;
    TurboJSNativeEntryHandle handle;
    const TurboJSNativeFunction *callee=NULL,*caller=NULL;
    TurboJSClutchCallSite *site;
    uint16_t receiver,a,b,result_reg;
    int callee_key=1,caller_key=2;
    int64_t args[3]={10,20,12}, result=0;
    CHECK(cache != NULL);
    TurboJS_NativeEntryHandleInit(&handle);
    CHECK(emit_method(&callee_ir));
    CHECK(TurboJS_CodeCacheCompile(cache,&callee_key,&callee_ir,&callee,&diagnostic)==TURBOJS_IR_OK);
    TurboJS_IRFunctionDestroy(&callee_ir);
    CHECK(TurboJS_CodeCacheAttachEntryHandle(cache,&callee_key,&handle,TURBOJS_NATIVE_ENTRY_INT32,3)==TURBOJS_IR_OK);

    TurboJS_IRFunctionInit(&caller_ir,3);
    receiver=TurboJS_IRAllocateRegister(&caller_ir); a=TurboJS_IRAllocateRegister(&caller_ir);
    b=TurboJS_IRAllocateRegister(&caller_ir); result_reg=TurboJS_IRAllocateRegister(&caller_ir);
    site=TurboJS_IRAllocateClutchCallSite(&caller_ir);
    CHECK(site && result_reg!=TURBOJS_IR_NO_REGISTER);
    TurboJS_ClutchCallSiteInit(site,&handle,handle.generation,TURBOJS_NATIVE_ENTRY_INT32,2);
    CHECK(TurboJS_ClutchCallSiteSetArgument(site,0,a)==TURBOJS_IR_OK);
    CHECK(TurboJS_ClutchCallSiteSetArgument(site,1,b)==TURBOJS_IR_OK);
    CHECK(TurboJS_ClutchCallSiteSetReceiver(site,receiver)==TURBOJS_IR_OK);
    CHECK(site->argument_count==3 && site->argument_registers[0]==receiver &&
          site->argument_registers[1]==a && site->argument_registers[2]==b);
    CHECK(TurboJS_IREmit(&caller_ir,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,receiver,0,0,0,0,0})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller_ir,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,1,0,1})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller_ir,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,b,0,0,2,0,2})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller_ir,(TurboJSIRInstruction){TURBOJS_IR_CALL_NATIVE_I64,result_reg,0,0,(int64_t)(uintptr_t)site,0,3})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller_ir,(TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,result_reg,0,0,0,4})==TURBOJS_IR_OK);
    CHECK(TurboJS_CodeCacheCompile(cache,&caller_key,&caller_ir,&caller,&diagnostic)==TURBOJS_IR_OK);
    TurboJS_IRFunctionDestroy(&caller_ir);
    CHECK(TurboJS_NativeInvoke(caller,args,3,&result)==TURBOJS_IR_OK && result==42);
    TurboJS_CodeCacheDestroy(cache);
    puts("receiver-aware Clutch call passed");
    return 0;
}
