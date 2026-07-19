#include <stdint.h>
#include <stdio.h>
#include "jit.h"

#define CHECK(x) do { \
    if (!(x)) { \
        fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); \
        return 1; \
    } \
} while (0)

static int build_add(TurboJSIRFunction *ir)
{
    TurboJSIRInstruction ins = {0};
    uint16_t a, b, sum;
    TurboJS_IRFunctionInit(ir, 2);
    a = TurboJS_IRAllocateRegister(ir);
    b = TurboJS_IRAllocateRegister(ir);
    sum = TurboJS_IRAllocateRegister(ir);
    ins.opcode = TURBOJS_IR_ARGUMENT;
    ins.destination = a;
    ins.immediate = 0;
    CHECK(TurboJS_IREmit(ir, ins) == TURBOJS_IR_OK);
    ins.opcode = TURBOJS_IR_ARGUMENT;
    ins.destination = b;
    ins.immediate = 1;
    CHECK(TurboJS_IREmit(ir, ins) == TURBOJS_IR_OK);
    ins.opcode = TURBOJS_IR_ADD_I64;
    ins.destination = sum;
    ins.left = a;
    ins.right = b;
    CHECK(TurboJS_IREmit(ir, ins) == TURBOJS_IR_OK);
    ins.opcode = TURBOJS_IR_RETURN_I64;
    ins.left = sum;
    CHECK(TurboJS_IREmit(ir, ins) == TURBOJS_IR_OK);
    return 0;
}

static int build_add_f64(TurboJSIRFunction *ir)
{
    TurboJSIRInstruction ins = {0};
    uint16_t a, b, sum;
    TurboJS_IRFunctionInit(ir, 2);
    a = TurboJS_IRAllocateRegister(ir);
    b = TurboJS_IRAllocateRegister(ir);
    sum = TurboJS_IRAllocateRegister(ir);
    ins.opcode = TURBOJS_IR_ARGUMENT;
    ins.destination = a;
    ins.immediate = 0;
    CHECK(TurboJS_IREmit(ir, ins) == TURBOJS_IR_OK);
    ins.opcode = TURBOJS_IR_ARGUMENT;
    ins.destination = b;
    ins.immediate = 1;
    CHECK(TurboJS_IREmit(ir, ins) == TURBOJS_IR_OK);
    ins.opcode = TURBOJS_IR_ADD_F64;
    ins.destination = sum;
    ins.left = a;
    ins.right = b;
    CHECK(TurboJS_IREmit(ir, ins) == TURBOJS_IR_OK);
    ins.opcode = TURBOJS_IR_RETURN_F64;
    ins.left = sum;
    CHECK(TurboJS_IREmit(ir, ins) == TURBOJS_IR_OK);
    return 0;
}

int main(void)
{
    TurboJSIRFunction ir;
    TurboJSIRDiagnostic diagnostic = {0};
    TurboJSCodeCache *cache;
    TurboJSNativeEntryHandle handle;
    const TurboJSNativeFunction *native = NULL;
    int key_a = 0, key_b = 0;
    int64_t args[2] = {20, 22};
    int64_t result = 0;
    uint64_t generation_a, generation_b;
    double f64_args[2] = {1.5, 2.25};
    double f64_result = 0.0;

    CHECK(build_add(&ir) == 0);
    cache = TurboJS_CodeCacheCreate(1, 4096);
    CHECK(cache != NULL);
    TurboJS_NativeEntryHandleInit(&handle);

    CHECK(TurboJS_CodeCacheCompile(cache, &key_a, &ir, &native,
                                   &diagnostic) == TURBOJS_IR_OK);
    CHECK(TurboJS_CodeCacheAttachEntryHandle(
              cache, &key_a, &handle, TURBOJS_NATIVE_ENTRY_INT32,
              2) == TURBOJS_IR_OK);
    generation_a = handle.generation;
    CHECK(generation_a != 0);
    CHECK(TurboJS_NativeEntryHandleIsLive(
              &handle, generation_a,
              TURBOJS_NATIVE_ENTRY_INT32));
    CHECK(TurboJS_NativeEntryInvokeI64(
              &handle, generation_a, args, 2,
              &result) == TURBOJS_IR_OK);
    CHECK(result == 42);

    native = NULL;
    CHECK(TurboJS_CodeCacheCompile(cache, &key_b, &ir, &native,
                                   &diagnostic) == TURBOJS_IR_OK);
    CHECK(!TurboJS_NativeEntryHandleIsLive(
              &handle, generation_a,
              TURBOJS_NATIVE_ENTRY_INT32));
    CHECK(handle.function == NULL);
    CHECK(handle.generation != generation_a);
    CHECK(TurboJS_NativeEntryInvokeI64(
              &handle, generation_a, args, 2,
              &result) == TURBOJS_IR_UNSUPPORTED);

    native = NULL;
    CHECK(TurboJS_CodeCacheCompile(cache, &key_a, &ir, &native,
                                   &diagnostic) == TURBOJS_IR_OK);
    CHECK(TurboJS_CodeCacheAttachEntryHandle(
              cache, &key_a, &handle, TURBOJS_NATIVE_ENTRY_INT32,
              2) == TURBOJS_IR_OK);
    generation_b = handle.generation;
    CHECK(generation_b != generation_a);
    CHECK(TurboJS_NativeEntryInvokeI64(
              &handle, generation_b, args, 2,
              &result) == TURBOJS_IR_OK);
    CHECK(result == 42);

    TurboJS_CodeCacheDestroy(cache);
    CHECK(handle.function == NULL);
    CHECK(handle.generation != generation_b);
    TurboJS_IRFunctionDestroy(&ir);

    CHECK(build_add_f64(&ir) == 0);
    cache = TurboJS_CodeCacheCreate(1, 4096);
    CHECK(cache != NULL);
    TurboJS_NativeEntryHandleInit(&handle);
    native = NULL;
    CHECK(TurboJS_CodeCacheCompile(cache, &key_a, &ir, &native,
                                   &diagnostic) == TURBOJS_IR_OK);
    CHECK(TurboJS_NativeResultKind(native) == TURBOJS_VALUE_F64);
    CHECK(TurboJS_CodeCacheAttachEntryHandle(
              cache, &key_a, &handle, TURBOJS_NATIVE_ENTRY_FLOAT64,
              2) == TURBOJS_IR_OK);
    generation_a = handle.generation;
    CHECK(TurboJS_NativeEntryInvokeF64(
              &handle, generation_a, f64_args, 2,
              &f64_result) == TURBOJS_IR_OK);
    CHECK(f64_result == 3.75);
    CHECK(TurboJS_NativeEntryInvokeI64(
              &handle, generation_a, args, 2,
              &result) == TURBOJS_IR_UNSUPPORTED);
    TurboJS_CodeCacheDestroy(cache);
    CHECK(handle.function == NULL);
    TurboJS_IRFunctionDestroy(&ir);
    puts("TurboJS Clutch native entry handle passed");
    return 0;
}
