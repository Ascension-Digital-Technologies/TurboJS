#include <stdlib.h>
#include <string.h>

#include "jit.h"
#include "../../runtime/executable_memory.h"

struct TurboJSNativeFunction {
    void *code;
    size_t code_size;
    size_t allocation_size;
    uint16_t argument_count;
    uint16_t register_count;
    uint16_t local_count;
    uint32_t *bytecode_offsets;
    uint64_t *materialized_masks;
    uint64_t *materialized_local_masks;
    uint64_t *live_register_masks;
    uint64_t *live_local_masks;
    TurboJSValueKind *register_kinds;
    TurboJSValueKind *local_kinds;
    int64_t *deopt_values;
    size_t instruction_count;
    TurboJSBailoutInfo last_bailout;
    TurboJSStackMap *stack_maps;
    size_t stack_map_count;
    TurboJSSafepointController owned_safepoint;
    TurboJSSafepointController *safepoint;
};

typedef struct CodeBuffer {
    unsigned char *bytes;
    size_t count;
    size_t capacity;
} CodeBuffer;

typedef struct BranchPatch {
    size_t displacement_offset;
    uint32_t target_instruction;
} BranchPatch;

static int reserve(CodeBuffer *b, size_t n) {
    unsigned char *p; size_t cap;
    if (b->count + n <= b->capacity) return 1;
    cap = b->capacity ? b->capacity * 2u : 256u;
    while (cap < b->count + n) cap *= 2u;
    p = (unsigned char *)realloc(b->bytes, cap);
    if (!p) return 0;
    b->bytes = p; b->capacity = cap; return 1;
}
static int emit8(CodeBuffer *b, unsigned v) { if (!reserve(b,1)) return 0; b->bytes[b->count++]=(unsigned char)v; return 1; }
static int emit32(CodeBuffer *b, uint32_t v) { unsigned i; for(i=0;i<4;i++) if(!emit8(b,v>>(i*8u))) return 0; return 1; }
static int emit64(CodeBuffer *b, uint64_t v) { unsigned i; for(i=0;i<8;i++) if(!emit8(b,(unsigned)(v>>(i*8u)))) return 0; return 1; }
static void patch32(CodeBuffer *b, size_t off, int32_t v) { unsigned i; for(i=0;i<4;i++) b->bytes[off+i]=(unsigned char)((uint32_t)v>>(i*8u)); }

static int rex(CodeBuffer *b,int w,unsigned r,unsigned x,unsigned m){return emit8(b,0x40u|(w?8u:0u)|((r&8u)?4u:0u)|((x&8u)?2u:0u)|((m&8u)?1u:0u));}
static int mov_rr(CodeBuffer*b,unsigned d,unsigned s){return rex(b,1,s,0,d)&&emit8(b,0x89)&&emit8(b,0xC0u|((s&7u)<<3u)|(d&7u));}
static int mov_imm64(CodeBuffer*b,unsigned d,uint64_t v){return rex(b,1,0,0,d)&&emit8(b,0xB8u+(d&7u))&&emit64(b,v);}
static int prologue(CodeBuffer*b,uint32_t frame){
    if(!emit8(b,0x55)||!rex(b,1,5,0,4)||!emit8(b,0x89)||!emit8(b,0xE5)) return 0; /* push rbp; mov rbp,rsp */
    if(frame){ if(!rex(b,1,0,0,4)||!emit8(b,0x81)||!emit8(b,0xEC)||!emit32(b,frame)) return 0; }
#if defined(_WIN32)
    /* args rcx, result rdx, bailout r8, safepoint r9 */
    return mov_rr(b,11,1) && mov_rr(b,10,2) && mov_rr(b,0,9) && mov_rr(b,9,8) && mov_rr(b,8,0);
#else
    /* args rdi, result rsi, bailout rdx, safepoint rcx */
    return mov_rr(b,11,7) && mov_rr(b,10,6) && mov_rr(b,9,2) && mov_rr(b,8,1);
#endif
}
static int epilogue(CodeBuffer*b){return emit8(b,0xC9)&&emit8(b,0xC3);} /* leave; ret */
static int return_success(CodeBuffer*b){
    return rex(b,1,0,0,10)&&emit8(b,0x89)&&emit8(b,0x02) && /* mov [r10],rax */
           emit8(b,0x31)&&emit8(b,0xC0) && epilogue(b);       /* xor eax,eax */
}
static int binary_rr(CodeBuffer*b,unsigned opcode,unsigned dst,unsigned src);
static int load_slot(CodeBuffer*b,unsigned reg,uint16_t slot);
static int mem_rbp_disp32(CodeBuffer*b,unsigned opcode,unsigned reg,int disp);
static int local_disp(const TurboJSIRFunction *f, uint16_t l);
static int store_base_disp32(CodeBuffer*b,unsigned base,unsigned reg,uint32_t disp){
    return rex(b,1,reg,0,base)&&emit8(b,0x89)&&emit8(b,0x80u|((reg&7u)<<3u)|(base&7u))&&emit32(b,disp);
}
static int return_bailout_at(CodeBuffer*b,const TurboJSIRFunction*f,size_t instruction_index,unsigned status_code){
    uint16_t i;
    /* Snapshot the instruction index followed by all virtual registers and locals. */
    if(!mov_imm64(b,0,(uint64_t)instruction_index)||!store_base_disp32(b,9,0,0))return 0;
    for(i=0;i<f->register_count;i++){
        if(!load_slot(b,0,i)||!store_base_disp32(b,9,0,8u+(uint32_t)i*8u))return 0;
    }
    for(i=0;i<f->local_count;i++){
        if(!mem_rbp_disp32(b,0x8B,0,local_disp(f,i))||
           !store_base_disp32(b,9,0,8u+(uint32_t)(f->register_count+i)*8u))return 0;
    }
    return emit8(b,0xB8) && emit32(b,status_code) && epilogue(b);
}

static int emit_safepoint_poll(CodeBuffer*b,const TurboJSIRFunction*f,size_t instruction_index){
    size_t clear_disp,after;
    /* cmp dword ptr [r8], 0; je continue; snapshot + safepoint bailout */
    if(!emit8(b,0x41)||!emit8(b,0x83)||!emit8(b,0x38)||!emit8(b,0x00)||!emit8(b,0x0F)||!emit8(b,0x84))return 0;
    clear_disp=b->count;if(!emit32(b,0)||!return_bailout_at(b,f,instruction_index,4u))return 0;
    after=clear_disp+4u;patch32(b,clear_disp,(int32_t)((intptr_t)b->count-(intptr_t)after));
    return 1;
}

static int check_i32(CodeBuffer*b,const TurboJSIRFunction*f,size_t instruction_index){
    size_t skip_disp,after;
    /* movsxd rcx,eax; cmp rax,rcx; je continue; bailout(index) */
    if(!emit8(b,0x48)||!emit8(b,0x63)||!emit8(b,0xC8)||
       !emit8(b,0x48)||!emit8(b,0x39)||!emit8(b,0xC8)||
       !emit8(b,0x0F)||!emit8(b,0x84)) return 0;
    skip_disp=b->count;if(!emit32(b,0)||!return_bailout_at(b,f,instruction_index,1u))return 0;
    after=skip_disp+4u;patch32(b,skip_disp,(int32_t)((intptr_t)b->count-(intptr_t)after));
    return 1;
}

static int emit_divrem_checked(CodeBuffer*b,const TurboJSIRFunction*f,size_t instruction_index,int want_remainder){
    size_t nonzero_disp,not_min_disp,not_neg1_disp,special_done_disp,after;
    /* rcx is divisor, rax is dividend. */
    if(!rex(b,1,1,0,1)||!emit8(b,0x85)||!emit8(b,0xC9)||!emit8(b,0x0F)||!emit8(b,0x85))return 0;
    nonzero_disp=b->count;if(!emit32(b,0)||!return_bailout_at(b,f,instruction_index,2u))return 0;
    after=nonzero_disp+4u;patch32(b,nonzero_disp,(int32_t)((intptr_t)b->count-(intptr_t)after));
    if(!mov_imm64(b,2,(uint64_t)(int64_t)INT32_MIN)||!binary_rr(b,0x39,0,2)||!emit8(b,0x0F)||!emit8(b,0x85))return 0;
    not_min_disp=b->count;if(!emit32(b,0))return 0;
    if(!mov_imm64(b,2,(uint64_t)-1)||!binary_rr(b,0x39,1,2)||!emit8(b,0x0F)||!emit8(b,0x85))return 0;
    not_neg1_disp=b->count;if(!emit32(b,0))return 0;
    if(want_remainder){
        if(!emit8(b,0x31)||!emit8(b,0xC0)||!emit8(b,0xE9))return 0;
        special_done_disp=b->count;if(!emit32(b,0))return 0;
    }else{
        if(!return_bailout_at(b,f,instruction_index,3u))return 0;
        special_done_disp=0;
    }
    after=not_min_disp+4u;patch32(b,not_min_disp,(int32_t)((intptr_t)b->count-(intptr_t)after));
    after=not_neg1_disp+4u;patch32(b,not_neg1_disp,(int32_t)((intptr_t)b->count-(intptr_t)after));
    if(!emit8(b,0x48)||!emit8(b,0x99)||!rex(b,1,0,0,1)||!emit8(b,0xF7)||!emit8(b,0xF9))return 0; /* cqo; idiv rcx */
    if(want_remainder && !mov_rr(b,0,2))return 0;
    if(want_remainder){after=special_done_disp+4u;patch32(b,special_done_disp,(int32_t)((intptr_t)b->count-(intptr_t)after));}
    return 1;
}
static int slot_disp(uint16_t r){ return -8 * ((int)r + 1); }
static int local_disp(const TurboJSIRFunction *f, uint16_t l){ return -8 * ((int)f->register_count + (int)l + 1); }
static int mem_rbp_disp32(CodeBuffer*b,unsigned opcode,unsigned reg,int disp){
    return rex(b,1,reg,0,5)&&emit8(b,opcode)&&emit8(b,0x80u|((reg&7u)<<3u)|5u)&&emit32(b,(uint32_t)disp);
}
static int load_slot(CodeBuffer*b,unsigned reg,uint16_t slot){return mem_rbp_disp32(b,0x8B,reg,slot_disp(slot));}
static int store_slot(CodeBuffer*b,uint16_t slot,unsigned reg){return mem_rbp_disp32(b,0x89,reg,slot_disp(slot));}
static int load_arg(CodeBuffer*b,unsigned reg,unsigned arg){
    uint32_t d=arg*8u;
    return rex(b,1,reg,0,11)&&emit8(b,0x8B)&&emit8(b,0x80u|((reg&7u)<<3u)|3u)&&emit32(b,d);
}
static int binary_rr(CodeBuffer*b,unsigned opcode,unsigned dst,unsigned src){return rex(b,1,src,0,dst)&&emit8(b,opcode)&&emit8(b,0xC0u|((src&7u)<<3u)|(dst&7u));}
static int imul_rr(CodeBuffer*b,unsigned dst,unsigned src){return rex(b,1,dst,0,src)&&emit8(b,0x0F)&&emit8(b,0xAF)&&emit8(b,0xC0u|((dst&7u)<<3u)|(src&7u));}
static TurboJSIRStatus fail(TurboJSIRDiagnostic*d,TurboJSIRStatus s,size_t i,const char*m){if(d){d->status=s;d->instruction_index=i;d->message=m;}return s;}

static uint64_t reg_bit(uint16_t r){ return r < 64u ? ((uint64_t)1u << r) : 0; }
static uint64_t local_bit(int64_t l){ return l >= 0 && l < 64 ? ((uint64_t)1u << (uint16_t)l) : 0; }
static void instruction_use_def(const TurboJSIRInstruction *in, uint64_t *use_r, uint64_t *def_r, uint64_t *use_l, uint64_t *def_l){
    *use_r=*def_r=*use_l=*def_l=0;
    switch(in->opcode){
    case TURBOJS_IR_ARGUMENT: case TURBOJS_IR_CONSTANT_I64: *def_r=reg_bit(in->destination); break;
    case TURBOJS_IR_ADD_I64: case TURBOJS_IR_SUB_I64: case TURBOJS_IR_MUL_I64:
    case TURBOJS_IR_ADD_I32_CHECKED: case TURBOJS_IR_SUB_I32_CHECKED: case TURBOJS_IR_MUL_I32_CHECKED:
    case TURBOJS_IR_DIV_I32_CHECKED: case TURBOJS_IR_REM_I32_CHECKED: case TURBOJS_IR_RUNTIME_HELPER: case TURBOJS_IR_LESS_THAN_I64:
        *use_r=reg_bit(in->left)|reg_bit(in->right); *def_r=reg_bit(in->destination); break;
    case TURBOJS_IR_LOCAL_GET: *use_l=local_bit(in->immediate); *def_r=reg_bit(in->destination); break;
    case TURBOJS_IR_LOCAL_SET: *use_r=reg_bit(in->left); *def_l=local_bit(in->immediate); break;
    case TURBOJS_IR_BRANCH_TRUE: case TURBOJS_IR_BRANCH_FALSE: case TURBOJS_IR_RETURN_I64: *use_r=reg_bit(in->left); break;
    default: break;
    }
}
static int compute_liveness(const TurboJSIRFunction *f,uint64_t *live_r,uint64_t *live_l){
    int changed=1; size_t iter=0,pc;
    while(changed && iter++ < f->instruction_count*4u+8u){
        changed=0;
        for(pc=f->instruction_count;pc-- > 0;){
            const TurboJSIRInstruction *in=&f->instructions[pc]; uint64_t out_r=0,out_l=0,use_r,def_r,use_l,def_l,new_r,new_l;
            if(in->opcode==TURBOJS_IR_JUMP){out_r=live_r[in->target];out_l=live_l[in->target];}
            else if(in->opcode==TURBOJS_IR_BRANCH_TRUE||in->opcode==TURBOJS_IR_BRANCH_FALSE){
                out_r=live_r[in->target];out_l=live_l[in->target]; if(pc+1u<f->instruction_count){out_r|=live_r[pc+1u];out_l|=live_l[pc+1u];}
            }else if(in->opcode!=TURBOJS_IR_RETURN_I64 && pc+1u<f->instruction_count){out_r=live_r[pc+1u];out_l=live_l[pc+1u];}
            instruction_use_def(in,&use_r,&def_r,&use_l,&def_l); new_r=use_r|(out_r&~def_r); new_l=use_l|(out_l&~def_l);
            if(new_r!=live_r[pc]||new_l!=live_l[pc]){live_r[pc]=new_r;live_l[pc]=new_l;changed=1;}
        }
    }
    return !changed;
}

TurboJSIRStatus TurboJS_BaselineCompile(const TurboJSIRFunction *f, TurboJSNativeFunction **out, TurboJSIRDiagnostic *diag)
{
#if !defined(__x86_64__) && !defined(_M_X64)
    (void)f;(void)out;return fail(diag,TURBOJS_IR_UNSUPPORTED,0,"x64 baseline backend unavailable");
#else
    CodeBuffer b={0}; TurboJSNativeFunction*n=NULL; size_t*i_offsets=NULL; BranchPatch*patches=NULL; size_t pc,patch_count=0; uint32_t frame; TurboJSIRStatus st; uint64_t materialized=0, materialized_locals=0;
    if (!f || !out)
        return TURBOJS_IR_INVALID_ARGUMENT;
    *out = NULL;
    st=TurboJS_IRVerify(f,diag); if(st!=TURBOJS_IR_OK) return st;
    frame=(uint32_t)((((size_t)f->register_count + (size_t)f->local_count)*8u+15u)&~15u);
    i_offsets=(size_t*)calloc(f->instruction_count+1,sizeof(*i_offsets)); patches=(BranchPatch*)calloc(f->instruction_count,sizeof(*patches));
    if(!i_offsets||!patches){st=TURBOJS_IR_OUT_OF_MEMORY;goto done;}
    if(!prologue(&b,frame)){st=TURBOJS_IR_OUT_OF_MEMORY;goto done;}
    for(pc=0;pc<f->instruction_count;pc++){
        const TurboJSIRInstruction*in=&f->instructions[pc]; i_offsets[pc]=b.count;
        switch(in->opcode){
        case TURBOJS_IR_NOP: if(!emit8(&b,0x90)) goto oom; break;
        case TURBOJS_IR_ARGUMENT: if(!load_arg(&b,0,(unsigned)in->immediate)||!store_slot(&b,in->destination,0)) goto oom; break;
        case TURBOJS_IR_CONSTANT_I64: if(!mov_imm64(&b,0,(uint64_t)in->immediate)||!store_slot(&b,in->destination,0)) goto oom; break;
        case TURBOJS_IR_ADD_I64: case TURBOJS_IR_SUB_I64: case TURBOJS_IR_MUL_I64:
        case TURBOJS_IR_ADD_I32_CHECKED: case TURBOJS_IR_SUB_I32_CHECKED: case TURBOJS_IR_MUL_I32_CHECKED:
            if(!load_slot(&b,0,in->left)||!load_slot(&b,1,in->right)) goto oom;
            if(in->opcode==TURBOJS_IR_ADD_I64 || in->opcode==TURBOJS_IR_ADD_I32_CHECKED){if(!binary_rr(&b,0x01,0,1))goto oom;}
            else if(in->opcode==TURBOJS_IR_SUB_I64 || in->opcode==TURBOJS_IR_SUB_I32_CHECKED){if(!binary_rr(&b,0x29,0,1))goto oom;}
            else if(!imul_rr(&b,0,1)) goto oom;
            if ((in->opcode==TURBOJS_IR_ADD_I32_CHECKED || in->opcode==TURBOJS_IR_SUB_I32_CHECKED || in->opcode==TURBOJS_IR_MUL_I32_CHECKED) &&
                !check_i32(&b,f,pc)) goto oom;
            if (!store_slot(&b, in->destination, 0))
                goto oom;
            break;
        case TURBOJS_IR_DIV_I32_CHECKED:
        case TURBOJS_IR_REM_I32_CHECKED:
            if(!load_slot(&b,0,in->left)||!load_slot(&b,1,in->right)||
               !emit_divrem_checked(&b,f,pc,in->opcode==TURBOJS_IR_REM_I32_CHECKED)||
               !store_slot(&b,in->destination,0)) goto oom;
            break;
        case TURBOJS_IR_RUNTIME_HELPER:
            if(!return_bailout_at(&b,f,pc,5u)) goto oom;
            break;
        case TURBOJS_IR_LESS_THAN_I64:
            if(!load_slot(&b,0,in->left)||!load_slot(&b,1,in->right)||!binary_rr(&b,0x39,0,1)) goto oom; /* cmp rax,rcx */
            if(!emit8(&b,0x0F)||!emit8(&b,0x9C)||!emit8(&b,0xC0)) goto oom; /* setl al */
            if(!rex(&b,1,0,0,0)||!emit8(&b,0x0F)||!emit8(&b,0xB6)||!emit8(&b,0xC0)||!store_slot(&b,in->destination,0)) goto oom;
            break;
        case TURBOJS_IR_LOCAL_GET:
            if(!mem_rbp_disp32(&b,0x8B,0,local_disp(f,(uint16_t)in->immediate))||!store_slot(&b,in->destination,0)) goto oom;
            break;
        case TURBOJS_IR_LOCAL_SET:
            if(!load_slot(&b,0,in->left)||!mem_rbp_disp32(&b,0x89,0,local_disp(f,(uint16_t)in->immediate))) goto oom;
            break;
        case TURBOJS_IR_JUMP:
            if(in->target<=pc && !emit_safepoint_poll(&b,f,pc)) goto oom;
            if (!emit8(&b, 0xE9))
                goto oom;
            patches[patch_count] = (BranchPatch){ b.count, in->target };
            patch_count++;
            if (!emit32(&b, 0))
                goto oom;
            break;
        case TURBOJS_IR_BRANCH_TRUE:
        case TURBOJS_IR_BRANCH_FALSE:
            if(in->target<=pc && !emit_safepoint_poll(&b,f,pc)) goto oom;
            if(!load_slot(&b,0,in->left)||!rex(&b,1,0,0,0)||!emit8(&b,0x85)||!emit8(&b,0xC0)) goto oom;
            if (!emit8(&b, 0x0F) || !emit8(&b, in->opcode == TURBOJS_IR_BRANCH_TRUE ? 0x85 : 0x84))
                goto oom;
            patches[patch_count] = (BranchPatch){ b.count, in->target };
            patch_count++;
            if (!emit32(&b, 0))
                goto oom;
            break;
        case TURBOJS_IR_RETURN_I64: if(!load_slot(&b,0,in->left)||!return_success(&b)) goto oom; break;
        default: st=fail(diag,TURBOJS_IR_UNSUPPORTED,pc,"opcode requires interpreter fallback");goto done;
        }
    }
    i_offsets[f->instruction_count]=b.count;
    for(pc=0;pc<patch_count;pc++){
        size_t target=i_offsets[patches[pc].target_instruction]; size_t after=patches[pc].displacement_offset+4u;
        patch32(&b,patches[pc].displacement_offset,(int32_t)((intptr_t)target-(intptr_t)after));
    }
    n=(TurboJSNativeFunction*)calloc(1,sizeof(*n)); if(!n) goto oom;
    n->allocation_size=(b.count+4095u)&~(size_t)4095u; n->code=turbojs_executable_memory_allocate(n->allocation_size); if(!n->code) goto oom;
    memcpy(n->code,b.bytes,b.count); if(!turbojs_executable_memory_seal(n->code,n->allocation_size)){st=TURBOJS_IR_UNSUPPORTED;goto done;}
    n->code_size=b.count;n->argument_count=f->argument_count;n->register_count=f->register_count;n->local_count=f->local_count;n->instruction_count=f->instruction_count;
    TurboJS_SafepointControllerInit(&n->owned_safepoint);n->safepoint=&n->owned_safepoint;
    n->bytecode_offsets=(uint32_t*)malloc(f->instruction_count*sizeof(*n->bytecode_offsets));
    n->materialized_masks=(uint64_t*)calloc(f->instruction_count,sizeof(*n->materialized_masks));
    n->materialized_local_masks=(uint64_t*)calloc(f->instruction_count,sizeof(*n->materialized_local_masks));
    n->live_register_masks=(uint64_t*)calloc(f->instruction_count,sizeof(*n->live_register_masks));
    n->live_local_masks=(uint64_t*)calloc(f->instruction_count,sizeof(*n->live_local_masks));
    n->register_kinds=(TurboJSValueKind*)calloc(f->register_count,sizeof(*n->register_kinds));
    n->local_kinds=(TurboJSValueKind*)calloc(f->local_count,sizeof(*n->local_kinds));
    n->deopt_values=(int64_t*)calloc(1u+(size_t)f->register_count+(size_t)f->local_count,sizeof(*n->deopt_values));
    n->stack_maps=(TurboJSStackMap*)calloc(f->instruction_count,sizeof(*n->stack_maps));
    if(!n->bytecode_offsets||!n->materialized_masks||!n->materialized_local_masks||!n->live_register_masks||!n->live_local_masks||
       (f->register_count&&!n->register_kinds)||(f->local_count&&!n->local_kinds)||!n->deopt_values||!n->stack_maps)goto oom;
    if(!compute_liveness(f,n->live_register_masks,n->live_local_masks)){st=TURBOJS_IR_INVALID_TARGET;goto done;}
    for(pc=0;pc<f->instruction_count;pc++){
        const TurboJSIRInstruction *in=&f->instructions[pc];
        n->bytecode_offsets[pc]=in->bytecode_offset;
        n->materialized_masks[pc]=materialized;
        n->materialized_local_masks[pc]=materialized_locals;
        switch(in->opcode){
        case TURBOJS_IR_ARGUMENT: case TURBOJS_IR_CONSTANT_I64:
        case TURBOJS_IR_ADD_I64: case TURBOJS_IR_SUB_I64: case TURBOJS_IR_MUL_I64:
        case TURBOJS_IR_ADD_I32_CHECKED: case TURBOJS_IR_SUB_I32_CHECKED: case TURBOJS_IR_MUL_I32_CHECKED:
        case TURBOJS_IR_DIV_I32_CHECKED: case TURBOJS_IR_REM_I32_CHECKED:
        case TURBOJS_IR_RUNTIME_HELPER: case TURBOJS_IR_LESS_THAN_I64: case TURBOJS_IR_LOCAL_GET:
            if(in->destination<64u) materialized|=((uint64_t)1u<<in->destination);
            if(in->destination<f->register_count)
                n->register_kinds[in->destination]=(in->opcode==TURBOJS_IR_LESS_THAN_I64)?TURBOJS_VALUE_BOOLEAN:
                    ((in->opcode>=TURBOJS_IR_ADD_I32_CHECKED&&in->opcode<=TURBOJS_IR_REM_I32_CHECKED)?TURBOJS_VALUE_I32:TURBOJS_VALUE_I64);
            break;
        case TURBOJS_IR_LOCAL_SET:
            if(in->immediate>=0 && in->immediate<64) materialized_locals|=((uint64_t)1u<<(uint16_t)in->immediate);
            if(in->immediate>=0 && (uint64_t)in->immediate<f->local_count)
                n->local_kinds[(uint16_t)in->immediate]=in->left<f->register_count?n->register_kinds[in->left]:TURBOJS_VALUE_UNKNOWN;
            break;
        default: break;
        }
    }
    for(pc=0;pc<f->instruction_count;pc++){
        const TurboJSIRInstruction *in=&f->instructions[pc];
        TurboJSSafepointKind kind=0;
        uint64_t ref_regs=0,ref_locals=0;
        uint16_t k;
        if(in->opcode>=TURBOJS_IR_ADD_I32_CHECKED && in->opcode<=TURBOJS_IR_REM_I32_CHECKED) kind=TURBOJS_SAFEPOINT_BAILOUT;
        else if(in->opcode==TURBOJS_IR_RUNTIME_HELPER) kind=TURBOJS_SAFEPOINT_RUNTIME_CALL;
        else if((in->opcode==TURBOJS_IR_JUMP||in->opcode==TURBOJS_IR_BRANCH_TRUE||in->opcode==TURBOJS_IR_BRANCH_FALSE)&&in->target<=pc) kind=TURBOJS_SAFEPOINT_LOOP_BACKEDGE;
        else if(in->opcode==TURBOJS_IR_RETURN_I64) kind=TURBOJS_SAFEPOINT_RETURN;
        if(!kind) continue;
        for(k=0;k<f->register_count&&k<64u;k++) if(n->register_kinds[k]==TURBOJS_VALUE_HEAP_REFERENCE) ref_regs|=((uint64_t)1u<<k);
        for(k=0;k<f->local_count&&k<64u;k++) if(n->local_kinds[k]==TURBOJS_VALUE_HEAP_REFERENCE) ref_locals|=((uint64_t)1u<<k);
        n->stack_maps[n->stack_map_count++]=(TurboJSStackMap){kind,pc,i_offsets[pc],in->bytecode_offset,n->live_register_masks[pc],n->live_local_masks[pc],ref_regs&n->live_register_masks[pc],ref_locals&n->live_local_masks[pc]};
    }
    *out=n;n=NULL;st=TURBOJS_IR_OK;
    if(diag){diag->status=st;diag->instruction_index=0;diag->message="ok";} goto done;
oom: st=TURBOJS_IR_OUT_OF_MEMORY;
done:
    if(n){if(n->code)turbojs_executable_memory_free(n->code,n->allocation_size);free(n->bytecode_offsets);free(n->materialized_masks);free(n->materialized_local_masks);free(n->live_register_masks);free(n->live_local_masks);free(n->register_kinds);free(n->local_kinds);free(n->deopt_values);free(n->stack_maps);free(n);} free(i_offsets);free(patches);free(b.bytes);return st;
#endif
}

TurboJSIRStatus TurboJS_NativeInvoke(const TurboJSNativeFunction*f,const int64_t*a,size_t count,int64_t*r){
    typedef int(*Entry)(const int64_t*,int64_t*,int64_t*,TurboJSSafepointController*);Entry e;int rc;
    TurboJSNativeFunction*m=(TurboJSNativeFunction*)f;
    if(!f||!r||count<f->argument_count||(count&&!a))return TURBOJS_IR_INVALID_ARGUMENT;
    if(f->deopt_values)memset(f->deopt_values,0,(1u+(size_t)f->register_count+(size_t)f->local_count)*sizeof(*f->deopt_values));
    memcpy(&e,&f->code,sizeof(e));rc=e(a,r,f->deopt_values,f->safepoint);
    m->last_bailout.reason=rc==1?TURBOJS_BAILOUT_INTEGER_OVERFLOW:rc==2?TURBOJS_BAILOUT_DIVISION_BY_ZERO:rc==3?TURBOJS_BAILOUT_DIVISION_OVERFLOW:rc==4?TURBOJS_BAILOUT_SAFEPOINT_REQUESTED:rc==5?TURBOJS_BAILOUT_RUNTIME_HELPER:TURBOJS_BAILOUT_NONE;
    m->last_bailout.instruction_index=rc?(size_t)f->deopt_values[0]:0;
    m->last_bailout.bytecode_offset=(rc&&m->last_bailout.instruction_index<f->instruction_count)?f->bytecode_offsets[m->last_bailout.instruction_index]:0;
    return rc?TURBOJS_IR_BAILOUT:TURBOJS_IR_OK;
}
TurboJSBailoutInfo TurboJS_NativeLastBailout(const TurboJSNativeFunction*f){TurboJSBailoutInfo z={TURBOJS_BAILOUT_NONE,0,0};return f?f->last_bailout:z;}
TurboJSDeoptFrame TurboJS_NativeLastDeoptFrame(const TurboJSNativeFunction*f){
    TurboJSDeoptFrame z;memset(&z,0,sizeof(z));if(!f)return z;z.bailout=f->last_bailout;z.register_count=f->register_count;z.local_count=f->local_count;
    if(z.bailout.reason!=TURBOJS_BAILOUT_NONE && z.bailout.instruction_index<f->instruction_count){
        z.materialized_register_mask=f->materialized_masks[z.bailout.instruction_index];
        z.materialized_local_mask=f->materialized_local_masks[z.bailout.instruction_index];
        z.live_register_mask=f->live_register_masks[z.bailout.instruction_index];
        z.live_local_mask=f->live_local_masks[z.bailout.instruction_index];
    }
    z.stack_count=0;z.register_values=f->deopt_values?f->deopt_values+1:NULL;z.local_values=f->deopt_values?f->deopt_values+1+f->register_count:NULL;
    z.register_kinds=f->register_kinds;z.local_kinds=f->local_kinds;return z;
}
size_t TurboJS_NativeStackMapCount(const TurboJSNativeFunction*f){return f?f->stack_map_count:0;}
const TurboJSStackMap *TurboJS_NativeStackMapAt(const TurboJSNativeFunction*f,size_t index){return (f&&index<f->stack_map_count)?&f->stack_maps[index]:NULL;}
void TurboJS_TraceDeoptFrame(const TurboJSDeoptFrame *frame,TurboJSGCTraceCallback trace,void *opaque){
    uint16_t i;if(!frame||!trace)return;
    for(i=0;i<frame->register_count&&i<64u;i++) if((frame->live_register_mask&frame->materialized_register_mask&((uint64_t)1u<<i))&&frame->register_kinds&&frame->register_kinds[i]==TURBOJS_VALUE_HEAP_REFERENCE&&frame->register_values) trace(opaque,(void*)(uintptr_t)frame->register_values[i]);
    for(i=0;i<frame->local_count&&i<64u;i++) if((frame->live_local_mask&frame->materialized_local_mask&((uint64_t)1u<<i))&&frame->local_kinds&&frame->local_kinds[i]==TURBOJS_VALUE_HEAP_REFERENCE&&frame->local_values) trace(opaque,(void*)(uintptr_t)frame->local_values[i]);
}

void TurboJS_RelocateDeoptFrame(TurboJSDeoptFrame *frame,TurboJSGCRelocateCallback relocate,void *opaque){
    uint16_t i;int64_t *regs,*locals;if(!frame||!relocate)return;regs=(int64_t*)(uintptr_t)frame->register_values;locals=(int64_t*)(uintptr_t)frame->local_values;
    for(i=0;i<frame->register_count&&i<64u;i++) if((frame->live_register_mask&frame->materialized_register_mask&((uint64_t)1u<<i))&&frame->register_kinds&&frame->register_kinds[i]==TURBOJS_VALUE_HEAP_REFERENCE&&regs) regs[i]=(int64_t)(uintptr_t)relocate(opaque,(void*)(uintptr_t)regs[i]);
    for(i=0;i<frame->local_count&&i<64u;i++) if((frame->live_local_mask&frame->materialized_local_mask&((uint64_t)1u<<i))&&frame->local_kinds&&frame->local_kinds[i]==TURBOJS_VALUE_HEAP_REFERENCE&&locals) locals[i]=(int64_t)(uintptr_t)relocate(opaque,(void*)(uintptr_t)locals[i]);
}
void TurboJS_SafepointControllerInit(TurboJSSafepointController *c){if(c){c->requested=0;c->epoch=0;}}
void TurboJS_SafepointRequest(TurboJSSafepointController *c){if(c){c->epoch++;c->requested=1;}}
void TurboJS_SafepointClear(TurboJSSafepointController *c){if(c)c->requested=0;}
void TurboJS_NativeSetSafepointController(TurboJSNativeFunction *f,TurboJSSafepointController *c){if(f)f->safepoint=c?c:&f->owned_safepoint;}

void TurboJS_NativeFunctionDestroy(TurboJSNativeFunction*f){if(!f)return;turbojs_executable_memory_free(f->code,f->allocation_size);free(f->bytecode_offsets);free(f->materialized_masks);free(f->materialized_local_masks);free(f->live_register_masks);free(f->live_local_masks);free(f->register_kinds);free(f->local_kinds);free(f->deopt_values);free(f->stack_maps);free(f);}
size_t TurboJS_NativeCodeSize(const TurboJSNativeFunction*f){return f?f->code_size:0;}
