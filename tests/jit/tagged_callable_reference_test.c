#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "jit.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static int emit_add(TurboJSIRFunction *f)
{
    uint16_t a, b, r;
    TurboJS_IRFunctionInit(f, 2);
    a = TurboJS_IRAllocateRegister(f);
    b = TurboJS_IRAllocateRegister(f);
    r = TurboJS_IRAllocateRegister(f);
    return r != TURBOJS_IR_NO_REGISTER &&
        TurboJS_IREmit(f, (TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,0,0,0}) == TURBOJS_IR_OK &&
        TurboJS_IREmit(f, (TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,b,0,0,1,0,0}) == TURBOJS_IR_OK &&
        TurboJS_IREmit(f, (TurboJSIRInstruction){TURBOJS_IR_ADD_I64,r,a,b,0,0,1}) == TURBOJS_IR_OK &&
        TurboJS_IREmit(f, (TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,r,0,0,0,2}) == TURBOJS_IR_OK;
}

static int emit_add_five(TurboJSIRFunction *f)
{
    uint16_t a, b, sum, five, r;
    TurboJS_IRFunctionInit(f, 2);
    a = TurboJS_IRAllocateRegister(f);
    b = TurboJS_IRAllocateRegister(f);
    sum = TurboJS_IRAllocateRegister(f);
    five = TurboJS_IRAllocateRegister(f);
    r = TurboJS_IRAllocateRegister(f);
    return r != TURBOJS_IR_NO_REGISTER &&
        TurboJS_IREmit(f, (TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,0,0,0}) == TURBOJS_IR_OK &&
        TurboJS_IREmit(f, (TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,b,0,0,1,0,0}) == TURBOJS_IR_OK &&
        TurboJS_IREmit(f, (TurboJSIRInstruction){TURBOJS_IR_ADD_I64,sum,a,b,0,0,1}) == TURBOJS_IR_OK &&
        TurboJS_IREmit(f, (TurboJSIRInstruction){TURBOJS_IR_CONSTANT_I64,five,0,0,5,0,2}) == TURBOJS_IR_OK &&
        TurboJS_IREmit(f, (TurboJSIRInstruction){TURBOJS_IR_ADD_I64,r,sum,five,0,0,3}) == TURBOJS_IR_OK &&
        TurboJS_IREmit(f, (TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,r,0,0,0,4}) == TURBOJS_IR_OK;
}

int main(void)
{
    TurboJSCodeCache *cache = TurboJS_CodeCacheCreate(8, 1u << 20);
    TurboJSIRFunction callee, caller;
    TurboJSIRDiagnostic diagnostic;
    TurboJSNativeEntryHandle handle;
    TurboJSCallableReference reference;
    const TurboJSNativeFunction *native = NULL;
    TurboJSBoxedValue result;
    const TurboJSNativeFunction *wrapper_native = NULL;
    int wrapper_key = 2;
    int replacement_key = 3;
    int64_t native_result = 0;
    int key = 1, environment = 91;
    uint16_t callable, arg0, arg1, moved, returned;

    CHECK(cache != NULL);
    TurboJS_NativeEntryHandleInit(&handle);
    CHECK(emit_add(&callee));
    CHECK(TurboJS_CodeCacheCompile(cache, &key, &callee, &native, &diagnostic) == TURBOJS_IR_OK);
    TurboJS_IRFunctionDestroy(&callee);
    CHECK(TurboJS_CodeCacheAttachEntryHandleIdentity(cache, &key, &handle,
        TURBOJS_NATIVE_ENTRY_INT32, 2, UINT64_C(0xC011AB1E)) == TURBOJS_IR_OK);
    TurboJS_CallableReferenceInit(&reference, UINT64_C(0xC011AB1E), &handle,
        handle.generation, TURBOJS_NATIVE_ENTRY_INT32, 2, &environment);

    TurboJS_IRFunctionInit(&caller, 0);
    TurboJS_IRFunctionSetLocalCount(&caller, 1);
    callable = TurboJS_IRAllocateRegister(&caller);
    arg0 = TurboJS_IRAllocateRegister(&caller);
    arg1 = TurboJS_IRAllocateRegister(&caller);
    moved = TurboJS_IRAllocateRegister(&caller);
    returned = TurboJS_IRAllocateRegister(&caller);
    CHECK(returned != TURBOJS_IR_NO_REGISTER);
    CHECK(TurboJS_IREmit(&caller, (TurboJSIRInstruction){TURBOJS_IR_VALUE_CALLABLE_CONSTANT,callable,0,0,(int64_t)(uintptr_t)&reference,0,0}) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller, (TurboJSIRInstruction){TURBOJS_IR_VALUE_LOCAL_SET,0,callable,0,0,0,0}) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller, (TurboJSIRInstruction){TURBOJS_IR_VALUE_CONSTANT_I32,arg0,0,0,20,0,0}) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller, (TurboJSIRInstruction){TURBOJS_IR_VALUE_CONSTANT_I32,arg1,0,0,22,0,0}) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller, (TurboJSIRInstruction){TURBOJS_IR_VALUE_LOCAL_GET,moved,0,0,0,0,0}) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller, (TurboJSIRInstruction){TURBOJS_IR_VALUE_CALL_I64,returned,moved,arg0,2,0,0}) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller, (TurboJSIRInstruction){TURBOJS_IR_VALUE_RETURN,0,returned,0,0,0,0}) == TURBOJS_IR_OK);

    memset(&result, 0, sizeof(result));
    CHECK(TurboJS_IRExecuteTagged(&caller, NULL, 0, &result) == TURBOJS_IR_OK);
    CHECK(result.tag == TURBOJS_BOXED_INT64 && result.as.integer == 42);
    CHECK(reference.closure_environment == &environment);
    CHECK(TurboJS_CodeCacheCompile(cache, &wrapper_key, &caller,
        &wrapper_native, &diagnostic) == TURBOJS_IR_OK);
    CHECK(TurboJS_NativeInvoke(wrapper_native, NULL, 0, &native_result) == TURBOJS_IR_OK);
    CHECK(native_result == 42);
    CHECK(TurboJS_NativeClutchSiteCount(wrapper_native) == 1);

    TurboJS_CodeCacheInvalidate(cache, &key);
    CHECK(TurboJS_IRExecuteTagged(&caller, NULL, 0, &result) == TURBOJS_IR_UNSUPPORTED);
    CHECK(TurboJS_NativeInvoke(wrapper_native, NULL, 0, &native_result) == TURBOJS_IR_BAILOUT);

    CHECK(emit_add_five(&callee));
    CHECK(TurboJS_CodeCacheCompile(cache, &replacement_key, &callee, &native, &diagnostic) == TURBOJS_IR_OK);
    TurboJS_IRFunctionDestroy(&callee);
    CHECK(TurboJS_CodeCacheAttachEntryHandleIdentity(cache, &replacement_key, &handle,
        TURBOJS_NATIVE_ENTRY_INT32, 2, UINT64_C(0xC011AB1E)) == TURBOJS_IR_OK);
    CHECK(TurboJS_NativeInvoke(wrapper_native, NULL, 0, &native_result) == TURBOJS_IR_OK);
    CHECK(native_result == 47);

    TurboJS_IRFunctionDestroy(&caller);
    TurboJS_CodeCacheDestroy(cache);
    puts("Tagged callable reference passed");
    return 0;
}
