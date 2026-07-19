#include "src/jit/backend/arm64/arm64_lowering.h"
#include <stdlib.h>
#include <string.h>

#define X_RESULT 19u
#define X_ARGS   20u
#define X_A      9u
#define X_B      10u
#define X_ZERO   31u
#define X_HELPERS 21u
#define X_SAFEPOINT 22u
#define D_A      0u
#define D_B      1u
#define SP       31u
#define FP       29u

static TurboJSIRStatus fail(TurboJSIRDiagnostic *d, TurboJSIRStatus s,
                            size_t i, const char *m)
{
    if (d) { d->status=s; d->instruction_index=i; d->message=m; }
    return s;
}
static uint16_t align16(uint32_t n){ return (uint16_t)((n+15u)&~15u); }
static int emit_prologue(TurboJSArm64Buffer *b, uint16_t frame)
{
    return TurboJS_Arm64EmitWord(b,0xA9BF53F3u) && /* stp x19,x20,[sp,#-16]! */
           TurboJS_Arm64EmitWord(b,0xA9BF5BF5u) && /* stp x21,x22,[sp,#-16]! */
           TurboJS_Arm64EmitWord(b,0xA9BF7BFDu) && /* stp x29,x30,[sp,#-16]! */
           TurboJS_Arm64EmitAddImm(b,FP,SP,0) &&
           TurboJS_Arm64EmitMovReg(b,X_ARGS,0) &&
           TurboJS_Arm64EmitMovReg(b,X_RESULT,1) &&
           TurboJS_Arm64EmitMovReg(b,X_HELPERS,2) &&
           TurboJS_Arm64EmitMovReg(b,X_SAFEPOINT,3) &&
           (!frame || TurboJS_Arm64EmitSubImm(b,SP,SP,frame));
}
static int emit_epilogue(TurboJSArm64Buffer *b, uint16_t frame)
{
    return (!frame || TurboJS_Arm64EmitAddImm(b,SP,SP,frame)) &&
           TurboJS_Arm64EmitWord(b,0xA8C17BFDu) && /* ldp x29,x30,[sp],#16 */
           TurboJS_Arm64EmitWord(b,0xA8C15BF5u) && /* ldp x21,x22,[sp],#16 */
           TurboJS_Arm64EmitWord(b,0xA8C153F3u) && /* ldp x19,x20,[sp],#16 */
           TurboJS_Arm64EmitRet(b);
}
static uint16_t reg_off(uint16_t r){ return (uint16_t)(r*8u); }
static uint16_t local_off(const TurboJSIRFunction *f,uint16_t l)
{ return (uint16_t)(((uint32_t)f->register_count+(uint32_t)l)*8u); }
static int load_reg(TurboJSArm64Buffer*b,unsigned x,uint16_t r)
{ return TurboJS_Arm64EmitLoad64(b,x,SP,reg_off(r)); }
static int store_reg(TurboJSArm64Buffer*b,uint16_t r,unsigned x)
{ return TurboJS_Arm64EmitStore64(b,x,SP,reg_off(r)); }

typedef struct Patch { size_t at; uint32_t target; uint8_t conditional; } Patch;

static TurboJSValueKind result_kind(const TurboJSIRFunction *f,
                                    const TurboJSIRInstruction *in,
                                    const TurboJSValueKind *regs,
                                    const TurboJSValueKind *locals)
{
    TurboJSValueKind hint;
    if (in->destination < f->register_count) {
        hint = TurboJS_IRFunctionRegisterKind(f, in->destination);
        if (hint != TURBOJS_VALUE_UNKNOWN) return hint;
    }
    switch (in->opcode) {
    case TURBOJS_IR_CONSTANT_F64: case TURBOJS_IR_ADD_F64: case TURBOJS_IR_SUB_F64:
    case TURBOJS_IR_MUL_F64: case TURBOJS_IR_DIV_F64: case TURBOJS_IR_I64_TO_F64:
        return TURBOJS_VALUE_F64;
    case TURBOJS_IR_LESS_THAN_I64: case TURBOJS_IR_LESS_THAN_F64:
    case TURBOJS_IR_LESS_EQUAL_F64: case TURBOJS_IR_EQUAL_F64:
        return TURBOJS_VALUE_BOOLEAN;
    case TURBOJS_IR_ADD_I32_CHECKED: case TURBOJS_IR_SUB_I32_CHECKED:
    case TURBOJS_IR_MUL_I32_CHECKED: case TURBOJS_IR_DIV_I32_CHECKED:
    case TURBOJS_IR_REM_I32_CHECKED:
        return TURBOJS_VALUE_I32;
    case TURBOJS_IR_LOCAL_GET:
        return ((uint64_t)in->immediate < f->local_count) ? locals[in->immediate] : TURBOJS_VALUE_UNKNOWN;
    case TURBOJS_IR_ARGUMENT:
        return regs[in->destination] != TURBOJS_VALUE_UNKNOWN ? regs[in->destination] : TURBOJS_VALUE_I64;
    case TURBOJS_IR_CONSTANT_I64: case TURBOJS_IR_ADD_I64: case TURBOJS_IR_SUB_I64:
    case TURBOJS_IR_MUL_I64: case TURBOJS_IR_F64_TO_I64_TRUNC:
        return TURBOJS_VALUE_I64;
    case TURBOJS_IR_RUNTIME_HELPER:
        return regs[in->destination];
    default: return TURBOJS_VALUE_UNKNOWN;
    }
}
static int emit_status_exit(TurboJSArm64Buffer *code, uint16_t frame, TurboJSIRStatus status)
{
    return TurboJS_Arm64EmitMovZ(code,0,(uint16_t)status,0) && emit_epilogue(code,frame);
}

TurboJSIRStatus TurboJS_Arm64LowerIREx(const TurboJSIRFunction *f,
                                       TurboJSArm64Compilation *compilation,
                                       TurboJSIRDiagnostic *diag)
{
    TurboJSArm64Buffer *code;
    size_t pc,patch_count=0; size_t *labels=NULL; Patch *patches=NULL;
    uint32_t raw_frame; uint16_t frame; TurboJSIRStatus st;
    TurboJSValueKind reg_kinds[TURBOJS_IR_MAX_REGISTERS]={0};
    TurboJSValueKind local_kinds[TURBOJS_IR_MAX_REGISTERS]={0};
    if(!f||!compilation)return fail(diag,TURBOJS_IR_INVALID_ARGUMENT,0,"invalid ARM64 lowering argument");
    code=&compilation->code;
    st=TurboJS_IRVerify(f,diag); if(st!=TURBOJS_IR_OK)return st;
    for(pc=0;pc<f->register_count;pc++) reg_kinds[pc]=TurboJS_IRFunctionRegisterKind(f,(uint16_t)pc);
    for(pc=0;pc<f->local_count && pc<TURBOJS_IR_MAX_REGISTERS;pc++) local_kinds[pc]=TurboJS_IRFunctionLocalKind(f,(uint16_t)pc);
    raw_frame=((uint32_t)f->register_count+(uint32_t)f->local_count)*8u;
    if(raw_frame>4080u)return fail(diag,TURBOJS_IR_UNSUPPORTED,0,"ARM64 frame exceeds immediate range");
    frame=align16(raw_frame);
    labels=(size_t*)calloc(f->instruction_count+1u,sizeof(*labels));
    patches=(Patch*)calloc(f->instruction_count*2u+1u,sizeof(*patches));
    if(!labels||!patches){st=TURBOJS_IR_OUT_OF_MEMORY;goto done;}
    if(!emit_prologue(code,frame)){st=TURBOJS_IR_OUT_OF_MEMORY;goto done;}
    for(pc=0;pc<f->instruction_count;pc++){
        const TurboJSIRInstruction *in=&f->instructions[pc]; labels[pc]=code->count;
        switch(in->opcode){
        case TURBOJS_IR_NOP:
            if(!TurboJS_Arm64EmitWord(code,0xD503201Fu)) goto oom;
            break;
        case TURBOJS_IR_ARGUMENT:
            if(in->immediate<0 || (uint64_t)in->immediate>=f->argument_count)
                {st=fail(diag,TURBOJS_IR_INVALID_ARGUMENT,pc,"ARM64 argument index out of range");goto done;}
            if(!TurboJS_Arm64EmitLoad64(code,X_A,X_ARGS,(uint16_t)(in->immediate*8))||
               !store_reg(code,in->destination,X_A)) goto oom;
            break;
        case TURBOJS_IR_CONSTANT_I64:
        case TURBOJS_IR_CONSTANT_F64:
            if(!TurboJS_Arm64EmitMovImm64(code,X_A,(uint64_t)in->immediate)||
               !store_reg(code,in->destination,X_A)) goto oom;
            break;
        case TURBOJS_IR_ADD_I64: case TURBOJS_IR_SUB_I64: case TURBOJS_IR_MUL_I64:
            if(!load_reg(code,X_A,in->left)||!load_reg(code,X_B,in->right))goto oom;
            if(in->opcode==TURBOJS_IR_ADD_I64){if(!TurboJS_Arm64EmitAddReg(code,X_A,X_A,X_B))goto oom;}
            else if(in->opcode==TURBOJS_IR_SUB_I64){if(!TurboJS_Arm64EmitSubReg(code,X_A,X_A,X_B))goto oom;}
            else if(!TurboJS_Arm64EmitMulReg(code,X_A,X_A,X_B))goto oom;
            if(!store_reg(code,in->destination,X_A)) goto oom;
            break;
        case TURBOJS_IR_RUNTIME_HELPER: {
            size_t ok_patch;
            if(in->immediate<0 || in->immediate>=TURBOJS_RUNTIME_HELPER_LIMIT)
                {st=fail(diag,TURBOJS_IR_INVALID_ARGUMENT,pc,"ARM64 runtime helper id out of range");goto done;}
            /* Dispatcher ABI: x0=helper id, x1=frame slots, x2=result pointer,
             * x3=safepoint controller; w0=status and result is written to *x2. */
            if(!TurboJS_Arm64EmitMovImm64(code,0,(uint64_t)in->immediate) ||
               !TurboJS_Arm64EmitMovReg(code,1,SP) ||
               !TurboJS_Arm64EmitMovReg(code,2,X_RESULT) ||
               !TurboJS_Arm64EmitMovReg(code,3,X_SAFEPOINT) ||
               !TurboJS_Arm64EmitBlr(code,X_HELPERS) ||
               !TurboJS_Arm64EmitCmpReg(code,0,X_ZERO)) goto oom;
            ok_patch=code->count;
            if(!TurboJS_Arm64EmitBranchCond(code,0,TURBOJS_ARM64_EQ) ||
               !emit_epilogue(code,frame)) goto oom;
            if(!TurboJS_Arm64PatchBranch(code,ok_patch,code->count,1) ||
               !TurboJS_Arm64EmitLoad64(code,X_A,X_RESULT,0) ||
               !store_reg(code,in->destination,X_A)) goto oom;
            compilation->uses_runtime_helpers=1;
            break;
        }
        case TURBOJS_IR_LESS_THAN_I64:
            if(!load_reg(code,X_A,in->left)||!load_reg(code,X_B,in->right)||
               !TurboJS_Arm64EmitCmpReg(code,X_A,X_B)||
               !TurboJS_Arm64EmitCset(code,X_A,TURBOJS_ARM64_LT)||
               !store_reg(code,in->destination,X_A)) goto oom;
            break;
        case TURBOJS_IR_ADD_I32_CHECKED: case TURBOJS_IR_SUB_I32_CHECKED:
        case TURBOJS_IR_MUL_I32_CHECKED: {
            size_t ok_patch;
            if(!load_reg(code,X_A,in->left)||!load_reg(code,X_B,in->right)||
               !TurboJS_Arm64EmitSxtw(code,X_A,X_A)||!TurboJS_Arm64EmitSxtw(code,X_B,X_B)) goto oom;
            if(in->opcode==TURBOJS_IR_ADD_I32_CHECKED){
                if(!TurboJS_Arm64EmitAdds32(code,X_A,X_A,X_B)) goto oom;
                ok_patch=code->count;
                if(!TurboJS_Arm64EmitBranchCond(code,0,TURBOJS_ARM64_VC)||
                   !emit_status_exit(code,frame,TURBOJS_IR_BAILOUT)||
                   !TurboJS_Arm64PatchBranch(code,ok_patch,code->count,1)||
                   !TurboJS_Arm64EmitSxtw(code,X_A,X_A)) goto oom;
            } else if(in->opcode==TURBOJS_IR_SUB_I32_CHECKED){
                if(!TurboJS_Arm64EmitSubs32(code,X_A,X_A,X_B)) goto oom;
                ok_patch=code->count;
                if(!TurboJS_Arm64EmitBranchCond(code,0,TURBOJS_ARM64_VC)||
                   !emit_status_exit(code,frame,TURBOJS_IR_BAILOUT)||
                   !TurboJS_Arm64PatchBranch(code,ok_patch,code->count,1)||
                   !TurboJS_Arm64EmitSxtw(code,X_A,X_A)) goto oom;
            } else {
                if(!TurboJS_Arm64EmitMulReg(code,X_A,X_A,X_B)||
                   !TurboJS_Arm64EmitSxtw(code,X_B,X_A)||
                   !TurboJS_Arm64EmitCmpReg(code,X_A,X_B)) goto oom;
                ok_patch=code->count;
                if(!TurboJS_Arm64EmitBranchCond(code,0,TURBOJS_ARM64_EQ)||
                   !emit_status_exit(code,frame,TURBOJS_IR_BAILOUT)||
                   !TurboJS_Arm64PatchBranch(code,ok_patch,code->count,1)||
                   !TurboJS_Arm64EmitMovReg(code,X_A,X_B)) goto oom;
            }
            if(!store_reg(code,in->destination,X_A)) goto oom;
            break;
        }
        case TURBOJS_IR_ADD_F64: case TURBOJS_IR_SUB_F64:
        case TURBOJS_IR_MUL_F64: case TURBOJS_IR_DIV_F64:
            if(!load_reg(code,X_A,in->left)||!load_reg(code,X_B,in->right)||
               !TurboJS_Arm64EmitFmovDFromX(code,D_A,X_A)||
               !TurboJS_Arm64EmitFmovDFromX(code,D_B,X_B)) goto oom;
            if(in->opcode==TURBOJS_IR_ADD_F64){if(!TurboJS_Arm64EmitFaddD(code,D_A,D_A,D_B))goto oom;}
            else if(in->opcode==TURBOJS_IR_SUB_F64){if(!TurboJS_Arm64EmitFsubD(code,D_A,D_A,D_B))goto oom;}
            else if(in->opcode==TURBOJS_IR_MUL_F64){if(!TurboJS_Arm64EmitFmulD(code,D_A,D_A,D_B))goto oom;}
            else if(!TurboJS_Arm64EmitFdivD(code,D_A,D_A,D_B))goto oom;
            if(!TurboJS_Arm64EmitFmovXFromD(code,X_A,D_A)||!store_reg(code,in->destination,X_A))goto oom;
            break;
        case TURBOJS_IR_LESS_THAN_F64: case TURBOJS_IR_LESS_EQUAL_F64: case TURBOJS_IR_EQUAL_F64:
            if(!load_reg(code,X_A,in->left)||!load_reg(code,X_B,in->right)||
               !TurboJS_Arm64EmitFmovDFromX(code,D_A,X_A)||
               !TurboJS_Arm64EmitFmovDFromX(code,D_B,X_B)||
               !TurboJS_Arm64EmitFcmpD(code,D_A,D_B)) goto oom;
            if(!TurboJS_Arm64EmitCset(code,X_A,in->opcode==TURBOJS_IR_EQUAL_F64?TURBOJS_ARM64_EQ:
                    in->opcode==TURBOJS_IR_LESS_THAN_F64?TURBOJS_ARM64_MI:TURBOJS_ARM64_LS)||
               !store_reg(code,in->destination,X_A))goto oom;
            break;
        case TURBOJS_IR_I64_TO_F64:
            if(!load_reg(code,X_A,in->left)||!TurboJS_Arm64EmitScvtfDFromX(code,D_A,X_A)||
               !TurboJS_Arm64EmitFmovXFromD(code,X_A,D_A)||!store_reg(code,in->destination,X_A))goto oom;
            break;
        case TURBOJS_IR_F64_TO_I64_TRUNC:
            if(!load_reg(code,X_A,in->left)||!TurboJS_Arm64EmitFmovDFromX(code,D_A,X_A)||
               !TurboJS_Arm64EmitFcvtzsXFromD(code,X_A,D_A)||!store_reg(code,in->destination,X_A))goto oom;
            break;
        case TURBOJS_IR_LOCAL_GET:
            if(in->immediate<0 || (uint64_t)in->immediate>=f->local_count)
                {st=fail(diag,TURBOJS_IR_INVALID_ARGUMENT,pc,"ARM64 local index out of range");goto done;}
            if(!TurboJS_Arm64EmitLoad64(code,X_A,SP,local_off(f,(uint16_t)in->immediate))||
               !store_reg(code,in->destination,X_A)) goto oom;
            break;
        case TURBOJS_IR_LOCAL_SET:
            if(in->immediate<0 || (uint64_t)in->immediate>=f->local_count)
                {st=fail(diag,TURBOJS_IR_INVALID_ARGUMENT,pc,"ARM64 local index out of range");goto done;}
            if(!load_reg(code,X_A,in->left)||
               !TurboJS_Arm64EmitStore64(code,X_A,SP,local_off(f,(uint16_t)in->immediate))) goto oom;
            break;
        case TURBOJS_IR_JUMP:
            if(in->target<=pc){
                size_t continue_patch;
                if(!TurboJS_Arm64EmitLoad64(code,X_A,X_SAFEPOINT,0) ||
                   !TurboJS_Arm64EmitCmpReg(code,X_A,X_ZERO)) goto oom;
                continue_patch=code->count;
                if(!TurboJS_Arm64EmitBranchCond(code,0,TURBOJS_ARM64_EQ) ||
                   !TurboJS_Arm64EmitMovZ(code,0,TURBOJS_IR_BAILOUT,0) ||
                   !emit_epilogue(code,frame)) goto oom;
                if(!TurboJS_Arm64PatchBranch(code,continue_patch,code->count,1)) goto oom;
                compilation->uses_safepoints=1;
            }
            patches[patch_count++]=(Patch){code->count,in->target,0};
            if(!TurboJS_Arm64EmitBranch(code,0)) goto oom;
            break;
        case TURBOJS_IR_BRANCH_TRUE: case TURBOJS_IR_BRANCH_FALSE:
            if(in->target<=pc){
                size_t continue_patch;
                if(!TurboJS_Arm64EmitLoad64(code,X_A,X_SAFEPOINT,0) ||
                   !TurboJS_Arm64EmitCmpReg(code,X_A,X_ZERO)) goto oom;
                continue_patch=code->count;
                if(!TurboJS_Arm64EmitBranchCond(code,0,TURBOJS_ARM64_EQ) ||
                   !TurboJS_Arm64EmitMovZ(code,0,TURBOJS_IR_BAILOUT,0) ||
                   !emit_epilogue(code,frame)) goto oom;
                if(!TurboJS_Arm64PatchBranch(code,continue_patch,code->count,1)) goto oom;
                compilation->uses_safepoints=1;
            }
            if(!load_reg(code,X_A,in->left)||!TurboJS_Arm64EmitCmpReg(code,X_A,X_ZERO))goto oom;
            patches[patch_count++]=(Patch){code->count,in->target,1};
            if(!TurboJS_Arm64EmitBranchCond(code,0,in->opcode==TURBOJS_IR_BRANCH_TRUE?TURBOJS_ARM64_NE:TURBOJS_ARM64_EQ))goto oom;
            break;
        case TURBOJS_IR_RETURN_I64:
        case TURBOJS_IR_RETURN_F64:
            if(!load_reg(code,X_A,in->left)||!TurboJS_Arm64EmitStore64(code,X_A,X_RESULT,0)||
               !TurboJS_Arm64EmitMovZ(code,0,TURBOJS_IR_OK,0)||!emit_epilogue(code,frame))goto oom;
            break;
        default:
            st=fail(diag,TURBOJS_IR_UNSUPPORTED,pc,"opcode not supported by ARM64 integer CFG backend");goto done;
        }
        if(in->opcode==TURBOJS_IR_LOCAL_SET && (uint64_t)in->immediate<f->local_count && in->immediate<TURBOJS_IR_MAX_REGISTERS){
            TurboJSValueKind hint=TurboJS_IRFunctionLocalKind(f,(uint16_t)in->immediate);
            local_kinds[in->immediate]=hint!=TURBOJS_VALUE_UNKNOWN?hint:reg_kinds[in->left];
        }
        else if(in->destination<f->register_count)
            reg_kinds[in->destination]=result_kind(f,in,reg_kinds,local_kinds);
    }
    labels[f->instruction_count]=code->count;
    for(pc=0;pc<patch_count;pc++){
        if(patches[pc].target>=f->instruction_count){st=fail(diag,TURBOJS_IR_INVALID_TARGET,patches[pc].at,"ARM64 branch target out of range");goto done;}
        if(!TurboJS_Arm64PatchBranch(code,patches[pc].at,labels[patches[pc].target],patches[pc].conditional))
            {st=fail(diag,TURBOJS_IR_INVALID_TARGET,patches[pc].at,"ARM64 branch displacement out of range");goto done;}
    }
    compilation->frame_size=frame;
    compilation->register_count=f->register_count;
    compilation->local_count=f->local_count;
    compilation->stack_maps=(TurboJSStackMap*)calloc(f->instruction_count,sizeof(*compilation->stack_maps));
    compilation->deopt_sites=(TurboJSArm64DeoptSite*)calloc(f->instruction_count,sizeof(*compilation->deopt_sites));
    if(!compilation->stack_maps||!compilation->deopt_sites) goto oom;
    for(pc=0;pc<f->instruction_count;pc++){
        const TurboJSIRInstruction *in=&f->instructions[pc];
        TurboJSSafepointKind kind=0; TurboJSBailoutReason reason=TURBOJS_BAILOUT_NONE;
        uint64_t regs=f->register_count>=64u?UINT64_MAX:((f->register_count==0)?0:(((uint64_t)1u<<f->register_count)-1u));
        uint64_t locals=f->local_count>=64u?UINT64_MAX:((f->local_count==0)?0:(((uint64_t)1u<<f->local_count)-1u));
        if(in->opcode==TURBOJS_IR_ADD_I32_CHECKED||in->opcode==TURBOJS_IR_SUB_I32_CHECKED||in->opcode==TURBOJS_IR_MUL_I32_CHECKED){kind=TURBOJS_SAFEPOINT_BAILOUT;reason=TURBOJS_BAILOUT_INTEGER_OVERFLOW;}
        else if(in->opcode==TURBOJS_IR_RUNTIME_HELPER){kind=TURBOJS_SAFEPOINT_RUNTIME_CALL;reason=TURBOJS_BAILOUT_RUNTIME_HELPER;}
        else if((in->opcode==TURBOJS_IR_JUMP||in->opcode==TURBOJS_IR_BRANCH_TRUE||in->opcode==TURBOJS_IR_BRANCH_FALSE)&&in->target<=pc){kind=TURBOJS_SAFEPOINT_LOOP_BACKEDGE;reason=TURBOJS_BAILOUT_SAFEPOINT_REQUESTED;}
        else if(in->opcode==TURBOJS_IR_RETURN_I64||in->opcode==TURBOJS_IR_RETURN_F64)kind=TURBOJS_SAFEPOINT_RETURN;
        if(kind){
            uint64_t ref_regs=0,ref_locals=0; size_t k;
            for(k=0;k<f->register_count;k++) if(reg_kinds[k]==TURBOJS_VALUE_HEAP_REFERENCE) ref_regs|=((uint64_t)1u<<k);
            for(k=0;k<f->local_count && k<64u;k++) if(local_kinds[k]==TURBOJS_VALUE_HEAP_REFERENCE) ref_locals|=((uint64_t)1u<<k);
            compilation->stack_maps[compilation->stack_map_count++]=(TurboJSStackMap){kind,pc,labels[pc]*4u,in->bytecode_offset,regs,locals,ref_regs&regs,ref_locals&locals};
        }
        if(reason!=TURBOJS_BAILOUT_NONE) compilation->deopt_sites[compilation->deopt_site_count++]=(TurboJSArm64DeoptSite){pc,labels[pc]*4u,in->bytecode_offset,reason,regs,locals};
    }
    if(diag){diag->status=TURBOJS_IR_OK;diag->instruction_index=0;diag->message=NULL;}
    st=TURBOJS_IR_OK;goto done;
oom: st=TURBOJS_IR_OUT_OF_MEMORY;
done:
    free(labels);free(patches);return st;
}

void TurboJS_Arm64CompilationInit(TurboJSArm64Compilation *c)
{ if(c){memset(c,0,sizeof(*c));TurboJS_Arm64BufferInit(&c->code);} }
void TurboJS_Arm64CompilationDestroy(TurboJSArm64Compilation *c)
{ if(!c)return;TurboJS_Arm64BufferDestroy(&c->code);free(c->stack_maps);free(c->deopt_sites);memset(c,0,sizeof(*c)); }
TurboJSIRStatus TurboJS_Arm64LowerIR(const TurboJSIRFunction *f,TurboJSArm64Buffer *code,TurboJSIRDiagnostic *diag)
{ TurboJSArm64Compilation c;TurboJSIRStatus st;if(!code)return fail(diag,TURBOJS_IR_INVALID_ARGUMENT,0,"invalid ARM64 code buffer");TurboJS_Arm64CompilationInit(&c);st=TurboJS_Arm64LowerIREx(f,&c,diag);if(st==TURBOJS_IR_OK){TurboJS_Arm64BufferDestroy(code);*code=c.code;c.code.words=NULL;c.code.count=c.code.capacity=0;}TurboJS_Arm64CompilationDestroy(&c);return st; }


TurboJSIRStatus TurboJS_Arm64RelocateFrameReferences(
    const TurboJSArm64Compilation *c, size_t map_index,
    int64_t *slots, size_t slot_count,
    TurboJSGCRelocateCallback relocate, void *opaque)
{
    const TurboJSStackMap *m; size_t i; void *old_ref,*new_ref;
    if(!c||map_index>=c->stack_map_count||!slots||!relocate) return TURBOJS_IR_INVALID_ARGUMENT;
    m=&c->stack_maps[map_index];
    if(slot_count < (size_t)c->register_count + (size_t)c->local_count) return TURBOJS_IR_INVALID_ARGUMENT;
    for(i=0;i<64u && i<c->register_count;i++) if(m->reference_register_mask&((uint64_t)1u<<i)){
        old_ref=(void*)(uintptr_t)slots[i]; new_ref=relocate(opaque,old_ref); slots[i]=(int64_t)(uintptr_t)new_ref;
    }
    for(i=0;i<64u;i++) if(m->reference_local_mask&((uint64_t)1u<<i)){
        size_t at=(size_t)c->register_count+i;
        if(i>=c->local_count || at>=slot_count) return TURBOJS_IR_INVALID_ARGUMENT;
        old_ref=(void*)(uintptr_t)slots[at]; new_ref=relocate(opaque,old_ref); slots[at]=(int64_t)(uintptr_t)new_ref;
    }
    return TURBOJS_IR_OK;
}
