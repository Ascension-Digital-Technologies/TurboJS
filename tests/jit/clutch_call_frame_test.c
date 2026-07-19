#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "jit.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed: %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)

static int build_add(TurboJSIRFunction *ir) {
    TurboJSIRInstruction ins = {0};
    uint16_t a, b, sum;
    TurboJS_IRFunctionInit(ir, 2);
    a = TurboJS_IRAllocateRegister(ir);
    b = TurboJS_IRAllocateRegister(ir);
    sum = TurboJS_IRAllocateRegister(ir);
    ins.opcode = TURBOJS_IR_ARGUMENT; ins.destination = a; ins.immediate = 0; CHECK(TurboJS_IREmit(ir, ins) == TURBOJS_IR_OK);
    ins.opcode = TURBOJS_IR_ARGUMENT; ins.destination = b; ins.immediate = 1; CHECK(TurboJS_IREmit(ir, ins) == TURBOJS_IR_OK);
    ins.opcode = TURBOJS_IR_ADD_I64; ins.destination = sum; ins.left = a; ins.right = b; CHECK(TurboJS_IREmit(ir, ins) == TURBOJS_IR_OK);
    ins.opcode = TURBOJS_IR_RETURN_I64; ins.left = sum; CHECK(TurboJS_IREmit(ir, ins) == TURBOJS_IR_OK);
    return 0;
}

int main(void) {
    TurboJSIRFunction ir;
    TurboJSIRDiagnostic diagnostic = {0};
    TurboJSCodeCache *cache;
    TurboJSNativeEntryHandle handle;
    TurboJSClutchCallFrame frame;
    const TurboJSNativeFunction *native = NULL;
    int key = 0;
    int64_t args[2] = {20, 22};
    int64_t result = 0;
    uint64_t generation;

    CHECK(build_add(&ir) == 0);
    cache = TurboJS_CodeCacheCreate(1, 4096);
    CHECK(cache != NULL);
    TurboJS_NativeEntryHandleInit(&handle);
    CHECK(TurboJS_CodeCacheCompile(cache, &key, &ir, &native, &diagnostic) == TURBOJS_IR_OK);
    CHECK(TurboJS_CodeCacheAttachEntryHandle(cache, &key, &handle, TURBOJS_NATIVE_ENTRY_INT32, 2) == TURBOJS_IR_OK);
    generation = handle.generation;

    TurboJS_ClutchCallFrameInit(&frame);
    frame.target = &handle;
    frame.expected_generation = generation;
    frame.argument_count = 2;
    frame.caller_saved_gpr_mask = UINT64_C(0x3f);
    frame.caller_saved_fpr_mask = UINT64_C(0xff);
    frame.flags = TURBOJS_CLUTCH_CALL_RECURSIVE;
    CHECK(TurboJS_ClutchCallI64(&frame, args, &result) == TURBOJS_IR_OK);
    CHECK(result == 42);
    CHECK(frame.caller_saved_gpr_mask == UINT64_C(0x3f));
    CHECK(frame.flags & TURBOJS_CLUTCH_CALL_RECURSIVE);

    TurboJS_NativeEntryHandleInvalidate(&handle);
    CHECK(TurboJS_ClutchCallI64(&frame, args, &result) == TURBOJS_IR_UNSUPPORTED);
    TurboJS_CodeCacheDestroy(cache);
    TurboJS_IRFunctionDestroy(&ir);
    return 0;
}
