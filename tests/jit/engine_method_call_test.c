#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "jit.h"

typedef enum TestOpcode {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) OP_##id,
#define def(id, size, n_pop, n_push, f)
#include "internal/bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
} TestOpcode;

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

typedef struct ResolverContext {
    TurboJSNativeEntryHandle *handle;
    uint64_t identity;
    int callable_resolves;
    int call_resolves;
    int property_resolves;
} ResolverContext;

static TurboJSIRStatus resolve_callable(void *opaque, uint32_t offset,
    TurboJSEngineCallableLoadKind kind, uint32_t index,
    TurboJSCallableReference *out)
{
    ResolverContext *c = (ResolverContext *)opaque;
    (void)offset;
    if (kind != TURBOJS_ENGINE_CALLABLE_GLOBAL || index != 42)
        return TURBOJS_IR_UNSUPPORTED;
    c->callable_resolves++;
    TurboJS_CallableReferenceInit(out, c->identity, c->handle,
        c->handle->generation, TURBOJS_NATIVE_ENTRY_INT32, 3, NULL);
    return TURBOJS_IR_OK;
}

static TurboJSIRStatus resolve_property(void *opaque, uint32_t offset,
    uint32_t atom, TurboJSCallableReference *out, uint32_t *shape_offset,
    uint64_t *shape_identity)
{
    ResolverContext *c = (ResolverContext *)opaque;
    (void)offset;
    if (atom != 77) return TURBOJS_IR_UNSUPPORTED;
    c->property_resolves++;
    *shape_offset = 0;
    *shape_identity = UINT64_C(0x53484150453330);
    TurboJS_CallableReferenceInit(out, c->identity, c->handle,
        c->handle->generation, TURBOJS_NATIVE_ENTRY_INT32, 3, NULL);
    return TURBOJS_IR_OK;
}

static TurboJSIRStatus resolve_call(void *opaque, uint32_t offset,
    uint16_t argc, TurboJSEngineNumericMode mode, TurboJSClutchCallSite *site)
{
    ResolverContext *c = (ResolverContext *)opaque;
    (void)offset;
    if (argc != 2 || mode != TURBOJS_ENGINE_NUMERIC_INT32)
        return TURBOJS_IR_UNSUPPORTED;
    c->call_resolves++;
    TurboJS_ClutchCallSiteInit(site, c->handle, c->handle->generation,
        TURBOJS_NATIVE_ENTRY_INT32, argc);
    TurboJS_ClutchCallSiteSetTargetIdentity(site, c->identity);
    return TURBOJS_IR_OK;
}

static int emit_method(TurboJSIRFunction *f)
{
    uint16_t receiver, a, b, r;
    TurboJS_IRFunctionInit(f, 3);
    receiver = TurboJS_IRAllocateRegister(f);
    a = TurboJS_IRAllocateRegister(f);
    b = TurboJS_IRAllocateRegister(f);
    r = TurboJS_IRAllocateRegister(f);
    return r != TURBOJS_IR_NO_REGISTER &&
      TurboJS_IREmit(f, (TurboJSIRInstruction){TURBOJS_IR_ARGUMENT, receiver, 0, 0, 0, 0, 0}) == TURBOJS_IR_OK &&
      TurboJS_IREmit(f, (TurboJSIRInstruction){TURBOJS_IR_ARGUMENT, a, 0, 0, 1, 0, 1}) == TURBOJS_IR_OK &&
      TurboJS_IREmit(f, (TurboJSIRInstruction){TURBOJS_IR_ARGUMENT, b, 0, 0, 2, 0, 2}) == TURBOJS_IR_OK &&
      TurboJS_IREmit(f, (TurboJSIRInstruction){TURBOJS_IR_ADD_I64, r, a, b, 0, 0, 3}) == TURBOJS_IR_OK &&
      TurboJS_IREmit(f, (TurboJSIRInstruction){TURBOJS_IR_RETURN_I64, 0, r, 0, 0, 0, 4}) == TURBOJS_IR_OK;
}

static void put_i32(uint8_t *p, int32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

int main(void)
{
    TurboJSCodeCache *cache = TurboJS_CodeCacheCreate(8, 1u << 20);
    TurboJSIRFunction callee_ir, caller_ir;
    TurboJSIRDiagnostic diagnostic;
    TurboJSNativeEntryHandle handle;
    const TurboJSNativeFunction *callee = NULL, *caller = NULL;
    ResolverContext context;
    TurboJSEngineBytecodeInfo info;
    TurboJSSpoolLoweringStats stats;
    uint8_t bytecode[20];
    struct MockReceiver { uint64_t shape; int64_t payload; } receiver, wrong_receiver;
    int64_t caller_args[1];
    int callee_key = 1, caller_key = 2;
    int64_t result = 0;

    CHECK(cache != NULL);
    memset(&context, 0, sizeof(context));
    context.identity = UINT64_C(0x4D4554484F443239);
    TurboJS_NativeEntryHandleInit(&handle);
    context.handle = &handle;

    CHECK(emit_method(&callee_ir));
    CHECK(TurboJS_CodeCacheCompile(cache, &callee_key, &callee_ir,
        &callee, &diagnostic) == TURBOJS_IR_OK);
    TurboJS_IRFunctionDestroy(&callee_ir);
    CHECK(TurboJS_CodeCacheAttachEntryHandleIdentity(cache, &callee_key,
        &handle, TURBOJS_NATIVE_ENTRY_INT32, 3, context.identity) == TURBOJS_IR_OK);

    receiver.shape = UINT64_C(0x53484150453330); receiver.payload = 10;
    wrong_receiver.shape = UINT64_C(0xBAD); wrong_receiver.payload = 10;
    bytecode[0] = OP_get_arg0;
    bytecode[1] = OP_get_field2; put_i32(bytecode + 2, 77);
    bytecode[6] = OP_push_i32; put_i32(bytecode + 7, 20);
    bytecode[11] = OP_push_i32; put_i32(bytecode + 12, 22);
    bytecode[16] = OP_call_method; bytecode[17] = 2; bytecode[18] = 0;
    bytecode[19] = OP_return;

    memset(&stats, 0, sizeof(stats));
    memset(&info, 0, sizeof(info));
    info.bytecode = bytecode;
    info.bytecode_length = sizeof(bytecode);
    info.argument_count = 1;
    info.stack_size = 5;
    info.numeric_mode = TURBOJS_ENGINE_NUMERIC_INT32;
    info.call_resolver = resolve_call;
    info.call_resolver_opaque = &context;
    info.callable_resolver = resolve_callable;
    info.callable_resolver_opaque = &context;
    info.method_property_resolver = resolve_property;
    info.method_property_resolver_opaque = &context;
    info.lowering_stats = &stats;

    CHECK(TurboJS_EngineBytecodeToIR(&info, &caller_ir, &diagnostic) == TURBOJS_IR_OK);
    CHECK(context.property_resolves == 1 && context.call_resolves == 1);
    CHECK(stats.property_method_load_count == 1);
    CHECK(stats.property_shape_guard_count == 1);
    CHECK(stats.receiver_method_call_count == 1);
    CHECK(caller_ir.owned_clutch_site_count == 1);
    CHECK((caller_ir.owned_clutch_sites[0]->flags & TURBOJS_CLUTCH_CALL_HAS_RECEIVER) != 0);
    CHECK(caller_ir.owned_clutch_sites[0]->argument_count == 3);

    CHECK(TurboJS_CodeCacheCompile(cache, &caller_key, &caller_ir,
        &caller, &diagnostic) == TURBOJS_IR_OK);
    TurboJS_IRFunctionDestroy(&caller_ir);
    caller_args[0] = (int64_t)(intptr_t)&receiver;
    CHECK(TurboJS_NativeInvoke(caller, caller_args, 1, &result) == TURBOJS_IR_OK);
    CHECK(result == 42);
    caller_args[0] = (int64_t)(intptr_t)&wrong_receiver;
    CHECK(TurboJS_NativeInvoke(caller, caller_args, 1, &result) == TURBOJS_IR_BAILOUT);

    TurboJS_CodeCacheDestroy(cache);
    puts("engine shape-guarded property method fusion passed");
    return 0;
}
