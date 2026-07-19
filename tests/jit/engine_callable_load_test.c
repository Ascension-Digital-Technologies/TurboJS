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

typedef struct RootCounter { int retains, releases, live; } RootCounter;
typedef struct ResolverContext {
    TurboJSNativeEntryHandle *handle;
    TurboJSRootingHooks hooks;
    RootCounter roots;
    int environment;
    uint64_t identity;
    int callable_resolves;
    int call_resolves;
} ResolverContext;

static void *retain_root(void *opaque, void *reference) {
    RootCounter *r = (RootCounter *)opaque; r->retains++; r->live++; return reference;
}
static void release_root(void *opaque, void *reference) {
    RootCounter *r = (RootCounter *)opaque; (void)reference; r->releases++; r->live--;
}
static TurboJSIRStatus resolve_callable(void *opaque, uint32_t offset,
    TurboJSEngineCallableLoadKind kind, uint32_t index,
    TurboJSCallableReference *out)
{
    ResolverContext *c = (ResolverContext *)opaque;
    (void)offset;
    if (!((kind == TURBOJS_ENGINE_CALLABLE_CLOSURE && index == 0) ||
          (kind == TURBOJS_ENGINE_CALLABLE_GLOBAL && index == 42)))
        return TURBOJS_IR_UNSUPPORTED;
    c->callable_resolves++;
    return TurboJS_CallableReferenceInitRooted(out, c->identity, c->handle,
        c->handle->generation, TURBOJS_NATIVE_ENTRY_INT32, 2,
        &c->environment, &c->hooks);
}
static TurboJSIRStatus resolve_call(void *opaque, uint32_t offset,
    uint16_t argc, TurboJSEngineNumericMode mode, TurboJSClutchCallSite *site)
{
    ResolverContext *c = (ResolverContext *)opaque;
    (void)offset;
    if (argc != 2 || mode != TURBOJS_ENGINE_NUMERIC_INT32) return TURBOJS_IR_UNSUPPORTED;
    c->call_resolves++;
    TurboJS_ClutchCallSiteInit(site, c->handle, c->handle->generation,
        TURBOJS_NATIVE_ENTRY_INT32, argc);
    TurboJS_ClutchCallSiteSetTargetIdentity(site, c->identity);
    return TurboJS_ClutchCallSiteSetClosureEnvironment(site, &c->environment, &c->hooks);
}
static int emit_add(TurboJSIRFunction *f) {
    uint16_t a,b,r; TurboJS_IRFunctionInit(f,2);
    a=TurboJS_IRAllocateRegister(f); b=TurboJS_IRAllocateRegister(f); r=TurboJS_IRAllocateRegister(f);
    return r!=TURBOJS_IR_NO_REGISTER &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,0,0,0})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,b,0,0,1,0,0})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_ADD_I64,r,a,b,0,0,0})==TURBOJS_IR_OK &&
      TurboJS_IREmit(f,(TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,r,0,0,0,0})==TURBOJS_IR_OK;
}
static void put_i32(uint8_t *p, int32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
int main(void) {
    TurboJSCodeCache *cache=TurboJS_CodeCacheCreate(8,1u<<20);
    TurboJSIRFunction callee, caller; TurboJSIRDiagnostic d; TurboJSNativeEntryHandle handle;
    const TurboJSNativeFunction *native=NULL,*wrapper=NULL; ResolverContext c; int key=1,wkey=2; int64_t result=0;
    uint8_t bc[13], global_bc[17]; TurboJSEngineBytecodeInfo info;
    TurboJSSpoolLoweringStats stats; TurboJSIRFunction global_caller;
    const TurboJSNativeFunction *global_wrapper = NULL; int gwkey = 3;
    CHECK(cache); memset(&c,0,sizeof(c)); c.identity=UINT64_C(0xC105ED27); c.environment=91;
    c.hooks.opaque=&c.roots; c.hooks.retain=retain_root; c.hooks.release=release_root;
    TurboJS_NativeEntryHandleInit(&handle); c.handle=&handle;
    CHECK(emit_add(&callee)); CHECK(TurboJS_CodeCacheCompile(cache,&key,&callee,&native,&d)==TURBOJS_IR_OK);
    TurboJS_IRFunctionDestroy(&callee);
    CHECK(TurboJS_CodeCacheAttachEntryHandleIdentity(cache,&key,&handle,TURBOJS_NATIVE_ENTRY_INT32,2,c.identity)==TURBOJS_IR_OK);
    bc[0]=OP_get_var_ref0; bc[1]=OP_push_i32; put_i32(bc+2,20); bc[6]=OP_push_i32; put_i32(bc+7,22); bc[11]=OP_call2; bc[12]=OP_return;
    memset(&stats,0,sizeof(stats)); memset(&info,0,sizeof(info)); info.bytecode=bc; info.bytecode_length=sizeof(bc); info.stack_size=4;
    info.numeric_mode=TURBOJS_ENGINE_NUMERIC_INT32; info.call_resolver=resolve_call; info.call_resolver_opaque=&c;
    info.callable_resolver=resolve_callable; info.callable_resolver_opaque=&c; info.lowering_stats=&stats;
    CHECK(TurboJS_EngineBytecodeToIR(&info,&caller,&d)==TURBOJS_IR_OK);
    CHECK(c.callable_resolves==1 && c.call_resolves==1); CHECK(c.roots.live==2);
    CHECK(stats.callable_closure_load_count==1 && stats.callable_global_load_count==0);
    CHECK(TurboJS_CodeCacheCompile(cache,&wkey,&caller,&wrapper,&d)==TURBOJS_IR_OK);
    TurboJS_IRFunctionDestroy(&caller); CHECK(c.roots.live==1);
    CHECK(TurboJS_NativeInvoke(wrapper,NULL,0,&result)==TURBOJS_IR_OK && result==42);
    global_bc[0]=OP_get_var; put_i32(global_bc+1,42); global_bc[5]=OP_push_i32; put_i32(global_bc+6,30);
    global_bc[10]=OP_push_i32; put_i32(global_bc+11,12); global_bc[15]=OP_call2; global_bc[16]=OP_return;
    memset(&stats,0,sizeof(stats)); info.bytecode=global_bc; info.bytecode_length=sizeof(global_bc);
    CHECK(TurboJS_EngineBytecodeToIR(&info,&global_caller,&d)==TURBOJS_IR_OK);
    CHECK(stats.callable_global_load_count==1 && stats.callable_closure_load_count==0);
    CHECK(TurboJS_CodeCacheCompile(cache,&gwkey,&global_caller,&global_wrapper,&d)==TURBOJS_IR_OK);
    TurboJS_IRFunctionDestroy(&global_caller); CHECK(c.roots.live==2);
    result=0; CHECK(TurboJS_NativeInvoke(global_wrapper,NULL,0,&result)==TURBOJS_IR_OK && result==42);
    TurboJS_CodeCacheDestroy(cache); CHECK(c.roots.live==0 && c.roots.retains==c.roots.releases);
    puts("Engine rooted closure callable load passed"); return 0;
}
