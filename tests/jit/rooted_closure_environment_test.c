#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "jit.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

typedef struct RootCounter { int retains; int releases; int live; } RootCounter;
static void *retain_root(void *opaque, void *reference)
{
    RootCounter *counter = (RootCounter *)opaque;
    counter->retains++;
    counter->live++;
    return reference;
}
static void release_root(void *opaque, void *reference)
{
    RootCounter *counter = (RootCounter *)opaque;
    (void)reference;
    counter->releases++;
    counter->live--;
}

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

int main(void)
{
    TurboJSCodeCache *cache = TurboJS_CodeCacheCreate(8, 1u << 20);
    TurboJSIRFunction callee, caller;
    TurboJSIRDiagnostic diagnostic;
    TurboJSNativeEntryHandle handle;
    TurboJSCallableReference reference;
    TurboJSRootingHooks hooks;
    RootCounter roots = {0,0,0};
    const TurboJSNativeFunction *native = NULL, *wrapper = NULL;
    int key = 1, wrapper_key = 2, environment = 77;
    int64_t result = 0;
    uint16_t callable, a, b, returned;

    CHECK(cache != NULL);
    TurboJS_NativeEntryHandleInit(&handle);
    CHECK(emit_add(&callee));
    CHECK(TurboJS_CodeCacheCompile(cache, &key, &callee, &native, &diagnostic) == TURBOJS_IR_OK);
    TurboJS_IRFunctionDestroy(&callee);
    CHECK(TurboJS_CodeCacheAttachEntryHandleIdentity(cache, &key, &handle,
        TURBOJS_NATIVE_ENTRY_INT32, 2, UINT64_C(0xC105ED)) == TURBOJS_IR_OK);

    hooks.opaque = &roots;
    hooks.retain = retain_root;
    hooks.release = release_root;
    CHECK(TurboJS_CallableReferenceInitRooted(&reference, UINT64_C(0xC105ED),
        &handle, handle.generation, TURBOJS_NATIVE_ENTRY_INT32, 2,
        &environment, &hooks) == TURBOJS_IR_OK);
    CHECK(roots.live == 1);

    TurboJS_IRFunctionInit(&caller, 0);
    callable = TurboJS_IRAllocateRegister(&caller);
    a = TurboJS_IRAllocateRegister(&caller);
    b = TurboJS_IRAllocateRegister(&caller);
    returned = TurboJS_IRAllocateRegister(&caller);
    CHECK(returned != TURBOJS_IR_NO_REGISTER);
    CHECK(TurboJS_IREmit(&caller, (TurboJSIRInstruction){TURBOJS_IR_VALUE_CALLABLE_CONSTANT,callable,0,0,(int64_t)(uintptr_t)&reference,0,0}) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller, (TurboJSIRInstruction){TURBOJS_IR_VALUE_CONSTANT_I32,a,0,0,20,0,0}) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller, (TurboJSIRInstruction){TURBOJS_IR_VALUE_CONSTANT_I32,b,0,0,22,0,0}) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller, (TurboJSIRInstruction){TURBOJS_IR_VALUE_CALL_I64,returned,callable,a,2,0,0}) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&caller, (TurboJSIRInstruction){TURBOJS_IR_VALUE_RETURN,0,returned,0,0,0,0}) == TURBOJS_IR_OK);

    CHECK(TurboJS_CodeCacheCompile(cache, &wrapper_key, &caller, &wrapper, &diagnostic) == TURBOJS_IR_OK);
    TurboJS_IRFunctionDestroy(&caller);
    TurboJS_CallableReferenceDestroy(&reference);
    CHECK(roots.live == 1);
    CHECK(TurboJS_NativeInvoke(wrapper, NULL, 0, &result) == TURBOJS_IR_OK);
    CHECK(result == 42);

    TurboJS_CodeCacheDestroy(cache);
    CHECK(roots.live == 0);
    CHECK(roots.retains == roots.releases);
    puts("Rooted closure environment passed");
    return 0;
}
