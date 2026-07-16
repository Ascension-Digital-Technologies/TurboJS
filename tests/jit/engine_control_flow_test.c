#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
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

static void put_i32(uint8_t *p, int32_t v) {
    uint32_t u=(uint32_t)v; p[0]=(uint8_t)u; p[1]=(uint8_t)(u>>8); p[2]=(uint8_t)(u>>16); p[3]=(uint8_t)(u>>24);
}
static size_t emit_u8(uint8_t *b,size_t p,uint8_t v){b[p++]=v;return p;}
static size_t emit_i32(uint8_t *b,size_t p,int32_t v){put_i32(b+p,v);return p+4;}

int main(void) {
    uint8_t bc[128]; size_t p=0, loop, if_operand, goto_operand, end;
    TurboJSEngineBytecodeInfo info; TurboJSIRFunction ir; TurboJSIRDiagnostic d={0};
    TurboJSNativeFunction *native=NULL; int64_t a[1], ires, nres;

    p=emit_u8(bc,p,OP_get_arg0); p=emit_u8(bc,p,OP_put_loc0);
    p=emit_u8(bc,p,OP_push_i32); p=emit_i32(bc,p,0); p=emit_u8(bc,p,OP_put_loc1);
    loop=p;
    p=emit_u8(bc,p,OP_push_i32); p=emit_i32(bc,p,0);
    p=emit_u8(bc,p,OP_get_loc0); p=emit_u8(bc,p,OP_lt);
    p=emit_u8(bc,p,OP_if_false); if_operand=p; p=emit_i32(bc,p,0);
    p=emit_u8(bc,p,OP_get_loc1); p=emit_u8(bc,p,OP_get_loc0); p=emit_u8(bc,p,OP_add); p=emit_u8(bc,p,OP_put_loc1);
    p=emit_u8(bc,p,OP_get_loc0); p=emit_u8(bc,p,OP_push_i32); p=emit_i32(bc,p,1); p=emit_u8(bc,p,OP_sub); p=emit_u8(bc,p,OP_put_loc0);
    p=emit_u8(bc,p,OP_goto); goto_operand=p; p=emit_i32(bc,p,0);
    end=p;
    p=emit_u8(bc,p,OP_get_loc1); p=emit_u8(bc,p,OP_return);
    put_i32(bc+if_operand,(int32_t)end-(int32_t)if_operand);
    put_i32(bc+goto_operand,(int32_t)loop-(int32_t)goto_operand);

    memset(&info,0,sizeof(info)); info.bytecode=bc; info.bytecode_length=p; info.argument_count=1; info.local_count=2; info.stack_size=3;
    if (TurboJS_EngineBytecodeToIR(&info,&ir,&d)!=TURBOJS_IR_OK) { fprintf(stderr,"frontend failed at %zu: %s\n",d.instruction_index,d.message); return 1; }
    if (TurboJS_BaselineCompile(&ir,&native,&d)!=TURBOJS_IR_OK) { fprintf(stderr,"compile failed: %s\n",d.message); return 1; }
    for (int64_t n=0;n<=500;n++) {
        a[0]=n;
        if (TurboJS_IRExecute(&ir,a,1,&ires)!=TURBOJS_IR_OK || TurboJS_NativeInvoke(native,a,1,&nres)!=TURBOJS_IR_OK || ires!=nres || ires!=n*(n+1)/2) {
            fprintf(stderr,"mismatch n=%lld ir=%lld native=%lld\n",(long long)n,(long long)ires,(long long)nres); return 1;
        }
    }
    printf("engine control-flow loop passed 501 differential cases\n");
    printf("native code size: %zu bytes\n",TurboJS_NativeCodeSize(native));
    TurboJS_NativeFunctionDestroy(native); TurboJS_IRFunctionDestroy(&ir); return 0;
}
