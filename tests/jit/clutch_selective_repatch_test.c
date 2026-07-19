#include <stdint.h>
#include <stdio.h>
#include "jit.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)

static int emit_add(TurboJSIRFunction *f, int bias) {
    uint16_t a,b,r,c;
    TurboJS_IRFunctionInit(f,2);
    a=TurboJS_IRAllocateRegister(f); b=TurboJS_IRAllocateRegister(f);
    r=TurboJS_IRAllocateRegister(f); c=TurboJS_IRAllocateRegister(f);
    return c!=TURBOJS_IR_NO_REGISTER &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,0,0,0})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,b,0,0,1,0,0})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ADD_I64,r,a,b,0,0,1})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_CONSTANT_I64,c,0,0,bias,0,2})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ADD_I64,r,r,c,0,0,3})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,r,0,0,0,4})==TURBOJS_IR_OK;
}

static int emit_caller(TurboJSIRFunction *f, const TurboJSNativeEntryHandle *h,
                       uint64_t identity) {
    TurboJSClutchCallSite *site; uint16_t a,b,r;
    TurboJS_IRFunctionInit(f,2);
    a=TurboJS_IRAllocateRegister(f); b=TurboJS_IRAllocateRegister(f); r=TurboJS_IRAllocateRegister(f);
    site=TurboJS_IRAllocateClutchCallSite(f);
    if(!site||r==TURBOJS_IR_NO_REGISTER) return 0;
    TurboJS_ClutchCallSiteInit(site,h,h->generation,TURBOJS_NATIVE_ENTRY_INT32,2);
    TurboJS_ClutchCallSiteSetTargetIdentity(site,identity);
    return TurboJS_ClutchCallSiteSetArgument(site,0,a)==TURBOJS_IR_OK &&
      TurboJS_ClutchCallSiteSetArgument(site,1,b)==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,0,0,0})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,b,0,0,1,0,0})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_CALL_NATIVE_I64,r,a,b,(int64_t)(uintptr_t)site,0,2})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,r,0,0,0,3})==TURBOJS_IR_OK;
}

int main(void) {
    const uint64_t identity=UINT64_C(0x51EC71A7);
    TurboJSCodeCache *cache=TurboJS_CodeCacheCreate(64,4u<<20);
    TurboJSIRFunction ir; TurboJSIRDiagnostic d; TurboJSNativeEntryHandle h;
    const TurboJSNativeFunction *callee=NULL,*caller=NULL; int callee_key=1, caller_key=2;
    int64_t args[2]={19,23}, result=0; TurboJSCodeCacheStats before,after; int unrelated_keys[24]; size_t i;
    CHECK(cache);
    TurboJS_NativeEntryHandleInit(&h);
    CHECK(emit_add(&ir,0)); CHECK(TurboJS_CodeCacheCompile(cache,&callee_key,&ir,&callee,&d)==TURBOJS_IR_OK); TurboJS_IRFunctionDestroy(&ir);
    CHECK(TurboJS_CodeCacheAttachEntryHandleIdentity(cache,&callee_key,&h,TURBOJS_NATIVE_ENTRY_INT32,2,identity)==TURBOJS_IR_OK);
    CHECK(emit_caller(&ir,&h,identity)); CHECK(TurboJS_CodeCacheCompile(cache,&caller_key,&ir,&caller,&d)==TURBOJS_IR_OK); TurboJS_IRFunctionDestroy(&ir);
    CHECK(TurboJS_NativeInvoke(caller,args,2,&result)==TURBOJS_IR_OK && result==42);
    for (i=0;i<24;i++) {
        const TurboJSNativeFunction *unrelated=NULL; unrelated_keys[i]=(int)(100+i);
        CHECK(emit_add(&ir,(int)i));
        CHECK(TurboJS_CodeCacheCompile(cache,&unrelated_keys[i],&ir,&unrelated,&d)==TURBOJS_IR_OK);
        TurboJS_IRFunctionDestroy(&ir);
    }
    TurboJS_CodeCacheInvalidate(cache,&callee_key);
    CHECK(TurboJS_NativeInvoke(caller,args,2,&result)==TURBOJS_IR_BAILOUT);
    before=TurboJS_CodeCacheGetStats(cache);
    CHECK(emit_add(&ir,5)); CHECK(TurboJS_CodeCacheCompile(cache,&callee_key,&ir,&callee,&d)==TURBOJS_IR_OK); TurboJS_IRFunctionDestroy(&ir);
    CHECK(TurboJS_CodeCacheAttachEntryHandleIdentity(cache,&callee_key,&h,TURBOJS_NATIVE_ENTRY_INT32,2,identity)==TURBOJS_IR_OK);
    after=TurboJS_CodeCacheGetStats(cache);
    CHECK(after.clutch_repatch_successes==before.clutch_repatch_successes+1);
    CHECK(after.clutch_call_sites_repatched==before.clutch_call_sites_repatched+1);
    CHECK(after.clutch_repatch_attempts==before.clutch_repatch_attempts+1);
    CHECK(after.repatch_identity_lookups==before.repatch_identity_lookups+1);
    CHECK(after.repatch_identity_nodes_visited==before.repatch_identity_nodes_visited+1);
    CHECK(TurboJS_NativeInvoke(caller,args,2,&result)==TURBOJS_IR_OK && result==47);
    TurboJS_CodeCacheDestroy(cache);
    puts("Clutch selective repatch passed"); return 0;
}
