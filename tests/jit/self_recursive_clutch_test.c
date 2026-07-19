#include <stdint.h>
#include <stdio.h>
#include "jit.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)

int main(void)
{
    const uint64_t identity = UINT64_C(0x5E1FCA11);
    TurboJSCodeCache *cache = TurboJS_CodeCacheCreate(16, 1u << 20);
    TurboJSIRFunction ir;
    TurboJSIRDiagnostic diagnostic;
    TurboJSNativeEntryHandle handle;
    TurboJSClutchCallSite *site;
    const TurboJSNativeFunction *native = NULL;
    uint16_t n, one, cond, dec, recursive, sum, zero;
    int key = 1;
    int64_t argument = 7, result = -1;
    TurboJSCodeCacheStats before, after;

    CHECK(cache != NULL);
    TurboJS_NativeEntryHandleInit(&handle);
    TurboJS_IRFunctionInit(&ir, 1);
    n = TurboJS_IRAllocateRegister(&ir);
    one = TurboJS_IRAllocateRegister(&ir);
    cond = TurboJS_IRAllocateRegister(&ir);
    dec = TurboJS_IRAllocateRegister(&ir);
    recursive = TurboJS_IRAllocateRegister(&ir);
    sum = TurboJS_IRAllocateRegister(&ir);
    zero = TurboJS_IRAllocateRegister(&ir);
    CHECK(zero != TURBOJS_IR_NO_REGISTER);

    site = TurboJS_IRAllocateClutchCallSite(&ir);
    CHECK(site != NULL);
    TurboJS_ClutchCallSiteInit(site, NULL, 0, TURBOJS_NATIVE_ENTRY_INT32, 1);
    TurboJS_ClutchCallSiteSetTargetIdentity(site, identity);
    site->flags |= TURBOJS_CLUTCH_CALL_RECURSIVE;
    CHECK(TurboJS_ClutchCallSiteSetArgument(site, 0, dec) == TURBOJS_IR_OK);

    CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,n,0,0,0,0,0})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){TURBOJS_IR_CONSTANT_I64,one,0,0,1,0,1})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){TURBOJS_IR_LESS_THAN_I64,cond,n,one,0,0,2})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){TURBOJS_IR_BRANCH_TRUE,0,cond,0,0,8,3})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){TURBOJS_IR_SUB_I64,dec,n,one,0,0,4})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){TURBOJS_IR_CALL_NATIVE_I64,recursive,0,0,(int64_t)(uintptr_t)site,0,5})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){TURBOJS_IR_ADD_I64,sum,recursive,one,0,0,6})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,sum,0,0,0,7})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){TURBOJS_IR_CONSTANT_I64,zero,0,0,0,0,8})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,zero,0,0,0,9})==TURBOJS_IR_OK);

    CHECK(TurboJS_CodeCacheCompile(cache, &key, &ir, &native, &diagnostic) == TURBOJS_IR_OK);
    TurboJS_IRFunctionDestroy(&ir);
    CHECK(TurboJS_NativeInvoke(native, &argument, 1, &result) == TURBOJS_IR_BAILOUT);

    before = TurboJS_CodeCacheGetStats(cache);
    CHECK(TurboJS_CodeCacheAttachEntryHandleIdentity(cache, &key, &handle,
          TURBOJS_NATIVE_ENTRY_INT32, 1, identity) == TURBOJS_IR_OK);
    after = TurboJS_CodeCacheGetStats(cache);
    CHECK(after.clutch_repatch_successes == before.clutch_repatch_successes + 1);
    CHECK(after.clutch_call_sites_repatched == before.clutch_call_sites_repatched + 1);
    CHECK(TurboJS_NativeInvoke(native, &argument, 1, &result) == TURBOJS_IR_OK);
    CHECK(result == 7);

    TurboJS_CodeCacheDestroy(cache);
    puts("self-recursive Clutch publication passed");
    return 0;
}
