#include "jit.h"
#include "src/jit/backend/arm64/arm64_lowering.h"
#include <stdio.h>
#include <string.h>
#if defined(__aarch64__) && !defined(_WIN32)
#include <sys/mman.h>
#include <unistd.h>
#endif
static void *move_ref(void *opaque, void *reference){return (void *)((uintptr_t)reference+(uintptr_t)opaque);}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"check failed %s:%d: %s\n",__FILE__,__LINE__,#x);return 1;}}while(0)
static TurboJSIRInstruction op(TurboJSIROpcode o,uint16_t d,uint16_t l,uint16_t r,int64_t imm,uint32_t target){TurboJSIRInstruction x;memset(&x,0,sizeof(x));x.opcode=o;x.destination=d;x.left=l;x.right=r;x.immediate=imm;x.target=target;return x;}
int main(void){
    TurboJSIRFunction f;TurboJSArm64Buffer b;TurboJSIRDiagnostic d;uint16_t a,c,cmp,v;
    TurboJS_IRFunctionInit(&f,1);TurboJS_IRFunctionSetLocalCount(&f,1);
    a=TurboJS_IRAllocateRegister(&f);c=TurboJS_IRAllocateRegister(&f);cmp=TurboJS_IRAllocateRegister(&f);v=TurboJS_IRAllocateRegister(&f);
    CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_ARGUMENT,a,0,0,0,0))==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_CONSTANT_I64,c,0,0,10,0))==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_LESS_THAN_I64,cmp,a,c,0,0))==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_BRANCH_FALSE,0,cmp,0,0,6))==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_ADD_I64,v,a,c,0,0))==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_JUMP,0,0,0,0,7))==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_SUB_I64,v,a,c,0,0))==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_RETURN_I64,0,v,0,0,0))==TURBOJS_IR_OK);
    TurboJS_Arm64BufferInit(&b);
    CHECK(TurboJS_Arm64LowerIR(&f,&b,&d)==TURBOJS_IR_OK);
    CHECK(b.count>20u);
    /* Prologue, at least one conditional branch, one unconditional branch, return. */
    CHECK(b.words[0]==0xA9BF53F3u);CHECK(b.words[1]==0xA9BF5BF5u);CHECK(b.words[2]==0xA9BF7BFDu);
    {size_t i;int has_bc=0,has_b=0,has_ret=0;for(i=0;i<b.count;i++){uint32_t w=b.words[i];if((w&0xFF000010u)==0x54000000u)has_bc=1;if((w&0xFC000000u)==0x14000000u)has_b=1;if(w==0xD65F03C0u)has_ret=1;}CHECK(has_bc&&has_b&&has_ret);}
    TurboJS_Arm64BufferDestroy(&b);TurboJS_IRFunctionDestroy(&f);

    /* Float64 arithmetic, ordered comparison, conversion, and return lowering. */
    {
        uint16_t x,y,z,cmpf,zi; int64_t one_bits,two_bits; double one=1.5,two=2.0;
        memcpy(&one_bits,&one,sizeof(one)); memcpy(&two_bits,&two,sizeof(two));
        TurboJS_IRFunctionInit(&f,0);
        x=TurboJS_IRAllocateRegister(&f);y=TurboJS_IRAllocateRegister(&f);
        z=TurboJS_IRAllocateRegister(&f);cmpf=TurboJS_IRAllocateRegister(&f);zi=TurboJS_IRAllocateRegister(&f);
        CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_CONSTANT_F64,x,0,0,one_bits,0))==TURBOJS_IR_OK);
        CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_CONSTANT_F64,y,0,0,two_bits,0))==TURBOJS_IR_OK);
        CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_MUL_F64,z,x,y,0,0))==TURBOJS_IR_OK);
        CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_LESS_THAN_F64,cmpf,x,z,0,0))==TURBOJS_IR_OK);
        CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_F64_TO_I64_TRUNC,zi,z,0,0,0))==TURBOJS_IR_OK);
        CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_RETURN_F64,0,z,0,0,0))==TURBOJS_IR_OK);
        TurboJS_Arm64BufferInit(&b);
        CHECK(TurboJS_Arm64LowerIR(&f,&b,&d)==TURBOJS_IR_OK);
        {size_t i;int has_fmul=0,has_fcmp=0,has_fcvt=0;for(i=0;i<b.count;i++){uint32_t w=b.words[i];
          if((w&0xFFE0FC00u)==0x1E600800u)has_fmul=1;
          if((w&0xFFE0FC1Fu)==0x1E602000u)has_fcmp=1;
          if((w&0xFFFFFC00u)==0x9E780000u)has_fcvt=1;}CHECK(has_fmul&&has_fcmp&&has_fcvt);}
        TurboJS_Arm64BufferDestroy(&b);TurboJS_IRFunctionDestroy(&f);
    }
#if defined(__aarch64__) && !defined(_WIN32)
    /* Execute generated AArch64 code on native ARM64 and under qemu-user CI. */
    {
        typedef int (*Entry)(const int64_t *, int64_t *, void *, TurboJSSafepointController *);
        TurboJSSafepointController controller; int64_t args[2]={20,22}, result=0;
        uint16_t lhs,rhs,sum; size_t bytes,page_size,alloc_size; void *memory; Entry entry;
        TurboJS_IRFunctionInit(&f,2);
        lhs=TurboJS_IRAllocateRegister(&f);rhs=TurboJS_IRAllocateRegister(&f);sum=TurboJS_IRAllocateRegister(&f);
        CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_ARGUMENT,lhs,0,0,0,0))==TURBOJS_IR_OK);
        CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_ARGUMENT,rhs,0,0,1,0))==TURBOJS_IR_OK);
        CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_ADD_I64,sum,lhs,rhs,0,0))==TURBOJS_IR_OK);
        CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_RETURN_I64,0,sum,0,0,0))==TURBOJS_IR_OK);
        TurboJS_Arm64BufferInit(&b); CHECK(TurboJS_Arm64LowerIR(&f,&b,&d)==TURBOJS_IR_OK);
        bytes=b.count*sizeof(uint32_t); page_size=(size_t)sysconf(_SC_PAGESIZE); alloc_size=(bytes+page_size-1u)&~(page_size-1u);
        memory=mmap(NULL,alloc_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); CHECK(memory!=MAP_FAILED);
        memcpy(memory,b.words,bytes); __builtin___clear_cache((char*)memory,(char*)memory+bytes);
        CHECK(mprotect(memory,alloc_size,PROT_READ|PROT_EXEC)==0); memcpy(&entry,&memory,sizeof(entry));
        TurboJS_SafepointControllerInit(&controller); CHECK(entry(args,&result,NULL,&controller)==TURBOJS_IR_OK); CHECK(result==42);
        CHECK(munmap(memory,alloc_size)==0); TurboJS_Arm64BufferDestroy(&b); TurboJS_IRFunctionDestroy(&f);
    }
#endif

    /* Runtime-helper calls, loop safepoints, stack maps, and deopt sites. */
    {
        TurboJSArm64Compilation cmeta; uint16_t zero, one, loopv, helperv;
        TurboJS_IRFunctionInit(&f,0); TurboJS_IRFunctionSetLocalCount(&f,1);
        zero=TurboJS_IRAllocateRegister(&f); one=TurboJS_IRAllocateRegister(&f);
        loopv=TurboJS_IRAllocateRegister(&f); helperv=TurboJS_IRAllocateRegister(&f);
        CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_CONSTANT_I64,zero,0,0,0,0))==TURBOJS_IR_OK);
        CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_CONSTANT_I64,one,0,0,1,0))==TURBOJS_IR_OK);
        CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_LOCAL_SET,0,zero,0,0,0))==TURBOJS_IR_OK);
        CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_LOCAL_GET,loopv,0,0,0,0))==TURBOJS_IR_OK);
        TurboJS_IRFunctionSetRegisterKind(&f,helperv,TURBOJS_VALUE_HEAP_REFERENCE);
        TurboJS_IRFunctionSetLocalKind(&f,0,TURBOJS_VALUE_HEAP_REFERENCE);
        CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_RUNTIME_HELPER,helperv,0,0,7,0))==TURBOJS_IR_OK);
        CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_BRANCH_FALSE,0,loopv,0,0,3))==TURBOJS_IR_OK);
        CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_RETURN_I64,0,helperv,0,0,0))==TURBOJS_IR_OK);
        TurboJS_Arm64CompilationInit(&cmeta);
        CHECK(TurboJS_Arm64LowerIREx(&f,&cmeta,&d)==TURBOJS_IR_OK);
        CHECK(cmeta.uses_runtime_helpers==1); CHECK(cmeta.uses_safepoints==1);
        CHECK(cmeta.stack_map_count>=3u); CHECK(cmeta.deopt_site_count>=2u);
        { size_t mi; int found_refs=0; for(mi=0;mi<cmeta.stack_map_count;mi++){
            if(cmeta.stack_maps[mi].reference_register_mask & ((uint64_t)1u<<helperv)){
                int64_t frame[TURBOJS_IR_MAX_REGISTERS*2]={0}; frame[helperv]=0x1000; frame[cmeta.register_count]=0x2000;
                CHECK(TurboJS_Arm64RelocateFrameReferences(&cmeta,mi,frame,cmeta.register_count+cmeta.local_count,move_ref,(void*)(uintptr_t)0x10)==TURBOJS_IR_OK);
                CHECK(frame[helperv]==0x1010); CHECK(frame[cmeta.register_count]==0x2010); found_refs=1; break;
            }} CHECK(found_refs); }
        {size_t i;int has_blr=0;for(i=0;i<cmeta.code.count;i++)if((cmeta.code.words[i]&0xFFFFFC1Fu)==0xD63F0000u)has_blr=1;CHECK(has_blr);}
        TurboJS_Arm64CompilationDestroy(&cmeta); TurboJS_IRFunctionDestroy(&f);
    }
    /* Checked Int32 operations emit overflow exits and bailout metadata. */
    {
        TurboJSArm64Compilation checked; uint16_t lhs,rhs,sum;
        TurboJS_IRFunctionInit(&f,2); lhs=TurboJS_IRAllocateRegister(&f); rhs=TurboJS_IRAllocateRegister(&f); sum=TurboJS_IRAllocateRegister(&f);
        CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_ARGUMENT,lhs,0,0,0,0))==TURBOJS_IR_OK);
        CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_ARGUMENT,rhs,0,0,1,0))==TURBOJS_IR_OK);
        CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_ADD_I32_CHECKED,sum,lhs,rhs,0,0))==TURBOJS_IR_OK);
        CHECK(TurboJS_IREmit(&f,op(TURBOJS_IR_RETURN_I64,0,sum,0,0,0))==TURBOJS_IR_OK);
        TurboJS_Arm64CompilationInit(&checked); CHECK(TurboJS_Arm64LowerIREx(&f,&checked,&d)==TURBOJS_IR_OK);
        CHECK(checked.deopt_site_count>=1u); CHECK(checked.deopt_sites[0].reason==TURBOJS_BAILOUT_INTEGER_OVERFLOW);
        {size_t i;int has_adds=0,has_vc=0;for(i=0;i<checked.code.count;i++){uint32_t w=checked.code.words[i];if((w&0xFF200000u)==0x2B000000u)has_adds=1;if((w&0xFF00001Fu)==(0x54000000u|TURBOJS_ARM64_VC))has_vc=1;}CHECK(has_adds&&has_vc);}
        TurboJS_Arm64CompilationDestroy(&checked); TurboJS_IRFunctionDestroy(&f);
    }
    puts("ARM64 precise references, relocation, checked overflow, CFG, helper, safepoint, and deopt lowering passed");return 0;
}
