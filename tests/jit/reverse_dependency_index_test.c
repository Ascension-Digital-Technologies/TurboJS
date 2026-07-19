#include <stdint.h>
#include <stdio.h>

#include "jit.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, \
                    #condition);                                             \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int emit_add(TurboJSIRFunction *function)
{
    uint16_t left, right, result;
    TurboJS_IRFunctionInit(function, 2);
    left = TurboJS_IRAllocateRegister(function);
    right = TurboJS_IRAllocateRegister(function);
    result = TurboJS_IRAllocateRegister(function);
    return result != TURBOJS_IR_NO_REGISTER &&
           TurboJS_IREmit(function, (TurboJSIRInstruction){
               TURBOJS_IR_ARGUMENT, left, 0, 0, 0, 0, 0}) == TURBOJS_IR_OK &&
           TurboJS_IREmit(function, (TurboJSIRInstruction){
               TURBOJS_IR_ARGUMENT, right, 0, 0, 1, 0, 0}) == TURBOJS_IR_OK &&
           TurboJS_IREmit(function, (TurboJSIRInstruction){
               TURBOJS_IR_ADD_I64, result, left, right, 0, 0, 1}) ==
               TURBOJS_IR_OK &&
           TurboJS_IREmit(function, (TurboJSIRInstruction){
               TURBOJS_IR_RETURN_I64, 0, result, 0, 0, 0, 2}) ==
               TURBOJS_IR_OK;
}

static int emit_caller(TurboJSIRFunction *function,
                       const TurboJSNativeEntryHandle *handle)
{
    TurboJSClutchCallSite *site;
    uint16_t left, right, result;
    TurboJS_IRFunctionInit(function, 2);
    left = TurboJS_IRAllocateRegister(function);
    right = TurboJS_IRAllocateRegister(function);
    result = TurboJS_IRAllocateRegister(function);
    site = TurboJS_IRAllocateClutchCallSite(function);
    if (!site || result == TURBOJS_IR_NO_REGISTER)
        return 0;
    TurboJS_ClutchCallSiteInit(site, handle, handle->generation,
                              TURBOJS_NATIVE_ENTRY_INT32, 2);
    return TurboJS_ClutchCallSiteSetArgument(site, 0, left) == TURBOJS_IR_OK &&
           TurboJS_ClutchCallSiteSetArgument(site, 1, right) == TURBOJS_IR_OK &&
           TurboJS_IREmit(function, (TurboJSIRInstruction){
               TURBOJS_IR_ARGUMENT, left, 0, 0, 0, 0, 0}) == TURBOJS_IR_OK &&
           TurboJS_IREmit(function, (TurboJSIRInstruction){
               TURBOJS_IR_ARGUMENT, right, 0, 0, 1, 0, 0}) == TURBOJS_IR_OK &&
           TurboJS_IREmit(function, (TurboJSIRInstruction){
               TURBOJS_IR_CALL_NATIVE_I64, result, left, right,
               (int64_t)(uintptr_t)site, 0, 2}) == TURBOJS_IR_OK &&
           TurboJS_IREmit(function, (TurboJSIRInstruction){
               TURBOJS_IR_RETURN_I64, 0, result, 0, 0, 0, 3}) ==
               TURBOJS_IR_OK;
}

int main(void)
{
    enum { unrelated_count = 32 };
    TurboJSCodeCache *cache = TurboJS_CodeCacheCreate(64, 8u << 20);
    TurboJSIRDiagnostic diagnostic;
    TurboJSIRFunction ir;
    TurboJSNativeEntryHandle handle;
    const TurboJSNativeFunction *native = NULL;
    TurboJSCodeCacheStats before, after;
    int keys[1 + unrelated_count + 2];
    int64_t arguments[2] = {19, 23};
    int64_t result = 0;
    size_t i;

    CHECK(cache != NULL);
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
        keys[i] = (int)(1000 + i);

    CHECK(emit_add(&ir));
    CHECK(TurboJS_CodeCacheCompile(cache, &keys[0], &ir, &native,
                                   &diagnostic) == TURBOJS_IR_OK);
    TurboJS_IRFunctionDestroy(&ir);
    TurboJS_NativeEntryHandleInit(&handle);
    CHECK(TurboJS_CodeCacheAttachEntryHandle(
              cache, &keys[0], &handle, TURBOJS_NATIVE_ENTRY_INT32, 2) ==
          TURBOJS_IR_OK);

    for (i = 0; i < unrelated_count; ++i) {
        CHECK(emit_add(&ir));
        CHECK(TurboJS_CodeCacheCompile(cache, &keys[1 + i], &ir, &native,
                                       &diagnostic) == TURBOJS_IR_OK);
        TurboJS_IRFunctionDestroy(&ir);
    }

    for (i = 0; i < 2; ++i) {
        CHECK(emit_caller(&ir, &handle));
        CHECK(TurboJS_CodeCacheCompile(
                  cache, &keys[1 + unrelated_count + i], &ir, &native,
                  &diagnostic) == TURBOJS_IR_OK);
        TurboJS_IRFunctionDestroy(&ir);
        CHECK(TurboJS_NativeInvoke(native, arguments, 2, &result) ==
              TURBOJS_IR_OK);
        CHECK(result == 42);
    }

    before = TurboJS_CodeCacheGetStats(cache);
    CHECK(before.reverse_dependency_registrations == 2);
    TurboJS_CodeCacheInvalidate(cache, &keys[0]);
    after = TurboJS_CodeCacheGetStats(cache);

    CHECK(after.dependent_call_sites_invalidated == 2);
    CHECK(after.reverse_dependency_lookups ==
          before.reverse_dependency_lookups + 1);
    CHECK(after.reverse_dependency_nodes_visited ==
          before.reverse_dependency_nodes_visited + 2);
    CHECK(after.reverse_dependency_nodes_visited < unrelated_count);

    TurboJS_CodeCacheDestroy(cache);
    puts("Vault reverse dependency index passed");
    return 0;
}
