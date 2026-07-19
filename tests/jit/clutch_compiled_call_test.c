#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "jit.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static int emit_binary_function(TurboJSIRFunction *f, TurboJSIROpcode op)
{
    uint16_t a, b, r;
    TurboJS_IRFunctionInit(f, 2);
    a = TurboJS_IRAllocateRegister(f);
    b = TurboJS_IRAllocateRegister(f);
    r = TurboJS_IRAllocateRegister(f);
    return a != TURBOJS_IR_NO_REGISTER && b != TURBOJS_IR_NO_REGISTER && r != TURBOJS_IR_NO_REGISTER &&
        TurboJS_IREmit(f, (TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,0,0,0}) == TURBOJS_IR_OK &&
        TurboJS_IREmit(f, (TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,b,0,0,1,0,0}) == TURBOJS_IR_OK &&
        TurboJS_IREmit(f, (TurboJSIRInstruction){op,r,a,b,0,0,1}) == TURBOJS_IR_OK &&
        TurboJS_IREmit(f, (TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,r,0,0,0,2}) == TURBOJS_IR_OK;
}

int main(void)
{
    TurboJSIRFunction callee, caller;
    TurboJSCodeCache *cache;
    const TurboJSNativeFunction *native_callee = NULL;
    TurboJSNativeFunction *native_caller = NULL;
    TurboJSNativeEntryHandle handle;
    TurboJSClutchCallSite site;
    TurboJSIRDiagnostic diag;
    int key = 0;
    int64_t args[2] = {20, 22}, result = 0;
    uint16_t a, b, call, two, out;

    CHECK(emit_binary_function(&callee, TURBOJS_IR_ADD_I64));
    cache = TurboJS_CodeCacheCreate(8, 1u << 20);
    CHECK(cache != NULL);
    CHECK(TurboJS_CodeCacheCompile(cache, &key, &callee, &native_callee, &diag) == TURBOJS_IR_OK);
    TurboJS_NativeEntryHandleInit(&handle);
    CHECK(TurboJS_CodeCacheAttachEntryHandle(cache, &key, &handle, TURBOJS_NATIVE_ENTRY_INT32, 2) == TURBOJS_IR_OK);
    TurboJS_ClutchCallSiteInit(&site, &handle, handle.generation, TURBOJS_NATIVE_ENTRY_INT32, 2);

    TurboJS_IRFunctionInit(&caller, 2);
    a = TurboJS_IRAllocateRegister(&caller);
    b = TurboJS_IRAllocateRegister(&caller);
    call = TurboJS_IRAllocateRegister(&caller);
    two = TurboJS_IRAllocateRegister(&caller);
    out = TurboJS_IRAllocateRegister(&caller);
    CHECK(out != TURBOJS_IR_NO_REGISTER);
    CHECK(TurboJS_ClutchCallSiteSetArgument(&site, 0, a) == TURBOJS_IR_OK);
    CHECK(TurboJS_ClutchCallSiteSetArgument(&site, 1, b) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,0,0,0})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,b,0,0,1,0,0})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller,(TurboJSIRInstruction){TURBOJS_IR_CALL_NATIVE_I64,call,a,b,(int64_t)(uintptr_t)&site,0,2})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller,(TurboJSIRInstruction){TURBOJS_IR_CONSTANT_I64,two,0,0,2,0,3})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller,(TurboJSIRInstruction){TURBOJS_IR_MUL_I64,out,call,two,0,0,4})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller,(TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,out,0,0,0,5})==TURBOJS_IR_OK);
    CHECK(TurboJS_BaselineCompile(&caller, &native_caller, &diag) == TURBOJS_IR_OK);
    CHECK(TurboJS_NativeInvoke(native_caller, args, 2, &result) == TURBOJS_IR_OK);
    CHECK(result == 84);
    CHECK(TurboJS_NativeStackMapCount(native_caller) >= 2);
    CHECK(TurboJS_NativeStackMapAt(native_caller, 0)->kind == TURBOJS_SAFEPOINT_CLUTCH_CALL);

    TurboJS_CodeCacheClear(cache);
    CHECK(TurboJS_NativeInvoke(native_caller, args, 2, &result) == TURBOJS_IR_BAILOUT);
    CHECK(TurboJS_NativeLastBailout(native_caller).reason == TURBOJS_BAILOUT_RUNTIME_HELPER);

    TurboJS_NativeFunctionDestroy(native_caller);
    TurboJS_IRFunctionDestroy(&caller);
    TurboJS_CodeCacheDestroy(cache);
    TurboJS_IRFunctionDestroy(&callee);
    puts("TurboJS Clutch compiled call passed");
    return 0;
}
