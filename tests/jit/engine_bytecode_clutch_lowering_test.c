#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "jit.h"

typedef enum EngineOpcode {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) OP_##id,
#define def(id, size, n_pop, n_push, f)
#include "internal/bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
} EngineOpcode;

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

typedef struct ResolverState {
    const TurboJSNativeEntryHandle *handle;
    uint32_t calls;
} ResolverState;

static TurboJSIRStatus resolve_call(void *opaque, uint32_t bytecode_offset,
                                    uint16_t argument_count,
                                    TurboJSEngineNumericMode numeric_mode,
                                    TurboJSClutchCallSite *out_site)
{
    ResolverState *state = (ResolverState *)opaque;
    if (!state || !state->handle || bytecode_offset != 7 ||
        argument_count != 2 || numeric_mode != TURBOJS_ENGINE_NUMERIC_INT32)
        return TURBOJS_IR_UNSUPPORTED;
    TurboJS_ClutchCallSiteInit(out_site, state->handle,
        state->handle->generation, TURBOJS_NATIVE_ENTRY_INT32, argument_count);
    state->calls++;
    return TURBOJS_IR_OK;
}

static int emit_add(TurboJSIRFunction *f)
{
    uint16_t a, b, r;
    TurboJS_IRFunctionInit(f, 2);
    a = TurboJS_IRAllocateRegister(f); b = TurboJS_IRAllocateRegister(f);
    r = TurboJS_IRAllocateRegister(f);
    return r != TURBOJS_IR_NO_REGISTER &&
        TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,0,0,0})==TURBOJS_IR_OK &&
        TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,b,0,0,1,0,0})==TURBOJS_IR_OK &&
        TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ADD_I64,r,a,b,0,0,1})==TURBOJS_IR_OK &&
        TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,r,0,0,0,2})==TURBOJS_IR_OK;
}

int main(void)
{
    TurboJSIRFunction callee, caller;
    TurboJSCodeCache *cache;
    const TurboJSNativeFunction *native_callee = NULL;
    TurboJSNativeFunction *native_caller = NULL;
    TurboJSNativeEntryHandle handle;
    TurboJSIRDiagnostic diag;
    ResolverState resolver;
    int key = 0;
    int64_t args[2] = {19, 23}, result = 0;
    /* push_i32 0 is a placeholder callee stack value; the resolver supplies
     * the stable Telemetry/Vault target for this bytecode offset. */
    uint8_t code[] = {
        OP_push_i32, 0, 0, 0, 0,
        OP_get_arg0,
        OP_get_arg1,
        OP_call2,
        OP_return
    };
    TurboJSEngineBytecodeInfo info;

    CHECK(emit_add(&callee));
    cache = TurboJS_CodeCacheCreate(8, 1u << 20);
    CHECK(cache != NULL);
    CHECK(TurboJS_CodeCacheCompile(cache, &key, &callee, &native_callee, &diag) == TURBOJS_IR_OK);
    TurboJS_NativeEntryHandleInit(&handle);
    CHECK(TurboJS_CodeCacheAttachEntryHandle(cache, &key, &handle,
        TURBOJS_NATIVE_ENTRY_INT32, 2) == TURBOJS_IR_OK);
    memset(&resolver, 0, sizeof(resolver)); resolver.handle = &handle;
    memset(&info, 0, sizeof(info));
    info.bytecode = code; info.bytecode_length = sizeof(code);
    info.argument_count = 2; info.stack_size = 3;
    info.numeric_mode = TURBOJS_ENGINE_NUMERIC_INT32;
    info.call_resolver = resolve_call; info.call_resolver_opaque = &resolver;

    CHECK(TurboJS_EngineBytecodeToIR(&info, &caller, &diag) == TURBOJS_IR_OK);
    CHECK(resolver.calls == 1);
    CHECK(caller.owned_clutch_site_count == 1);
    CHECK(caller.instructions[3].opcode == TURBOJS_IR_CALL_NATIVE_I64);
    CHECK(caller.owned_clutch_sites[0]->continuation_bytecode_offset == 8);
    CHECK(TurboJS_BaselineCompile(&caller, &native_caller, &diag) == TURBOJS_IR_OK);
    /* Native code must own stable copies of embedded Clutch metadata. */
    TurboJS_IRFunctionDestroy(&caller);
    memset(&caller, 0, sizeof(caller));
    CHECK(TurboJS_NativeInvoke(native_caller, args, 2, &result) == TURBOJS_IR_OK);
    CHECK(result == 42);
    TurboJS_CodeCacheClear(cache);
    CHECK(TurboJS_NativeInvoke(native_caller, args, 2, &result) == TURBOJS_IR_BAILOUT);

    TurboJS_NativeFunctionDestroy(native_caller);
    TurboJS_IRFunctionDestroy(&caller);
    TurboJS_CodeCacheDestroy(cache);
    TurboJS_IRFunctionDestroy(&callee);
    puts("TurboJS engine bytecode Clutch lowering passed");
    return 0;
}
