#include "jit.h"
#include "../../runtime/executable_memory.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct TurboJSRegionNativeFunction {
    void *code; size_t code_size, allocation_size; size_t argument_count;
    TurboJSLinearScanResult allocation;
};
typedef struct { uint8_t *p; size_t n,c; } Buf;
typedef struct { size_t off; uint32_t target; } Patch;
typedef struct { Patch *p; size_t n,c; } Patches;
static int reserve(Buf*b,size_t n){size_t c;void*p;if(b->n+n<=b->c)return 1;c=b->c?b->c*2:512;while(c<b->n+n)c*=2;p=realloc(b->p,c);if(!p)return 0;b->p=p;b->c=c;return 1;}
static int e8(Buf*b,uint8_t x){if(!reserve(b,1))return 0;b->p[b->n++]=x;return 1;}
static int e32(Buf*b,uint32_t x){int i;for(i=0;i<4;i++)if(!e8(b,(uint8_t)(x>>(8*i))))return 0;return 1;}
static int e64(Buf*b,uint64_t x){int i;for(i=0;i<8;i++)if(!e8(b,(uint8_t)(x>>(8*i))))return 0;return 1;}
static void p32(Buf*b,size_t o,int32_t x){int i;for(i=0;i<4;i++)b->p[o+i]=(uint8_t)((uint32_t)x>>(8*i));}
static int rex(Buf*b,int w,unsigned r,unsigned x,unsigned m){return e8(b,(uint8_t)(0x40|(w?8:0)|((r&8)?4:0)|((x&8)?2:0)|((m&8)?1:0)));}
static int movrr(Buf*b,unsigned d,unsigned s){return rex(b,1,s,0,d)&&e8(b,0x89)&&e8(b,(uint8_t)(0xC0|((s&7)<<3)|(d&7)));}
static int movi(Buf*b,unsigned d,uint64_t x){return rex(b,1,0,0,d)&&e8(b,(uint8_t)(0xB8+(d&7)))&&e64(b,x);}
static int mem(Buf*b,uint8_t op,unsigned r,int disp){return rex(b,1,r,0,5)&&e8(b,op)&&e8(b,(uint8_t)(0x80|((r&7)<<3)|5))&&e32(b,(uint32_t)disp);}
static int slotdisp(uint32_t s){return -8*(int)(s+1);}
static int load(Buf*b,unsigned r,uint32_t s){return mem(b,0x8B,r,slotdisp(s));}
static int store(Buf*b,uint32_t s,unsigned r){return mem(b,0x89,r,slotdisp(s));}
static int bin(Buf*b,uint8_t op,unsigned d,unsigned s){return rex(b,1,s,0,d)&&e8(b,op)&&e8(b,(uint8_t)(0xC0|((s&7)<<3)|(d&7)));}
static int imul(Buf*b,unsigned d,unsigned s){return rex(b,1,d,0,s)&&e8(b,0x0F)&&e8(b,0xAF)&&e8(b,(uint8_t)(0xC0|((d&7)<<3)|(s&7)));}
static int addpatch(Patches*q,size_t off,uint32_t t){void*p;if(q->n==q->c){size_t c=q->c?q->c*2:16;p=realloc(q->p,c*sizeof(*q->p));if(!p)return 0;q->p=p;q->c=c;}q->p[q->n++]=(Patch){off,t};return 1;}
static int jmp(Buf*b,Patches*q,uint32_t t){size_t o;if(!e8(b,0xE9))return 0;o=b->n;if(!e32(b,0))return 0;return addpatch(q,o,t);}
static TurboJSIRStatus fail(TurboJSIRDiagnostic*d,TurboJSIRStatus s,size_t i,const char*m){if(d){d->status=s;d->instruction_index=i;d->message=m;}return s;}
static uint32_t pred_index(const TurboJSSSABlock*b,uint32_t pred){uint32_t i;for(i=0;i<b->predecessor_count;i++)if(b->predecessors[i]==pred)return i;return UINT32_MAX;}
static int edge_moves(Buf*b,const TurboJSSSAGraph*g,uint32_t pred,uint32_t target,uint32_t temp_base,uint32_t*count){const TurboJSSSABlock*tb=&g->blocks[target];uint32_t pi=pred_index(tb,pred),i,k=0;if(pi==UINT32_MAX)return 0;for(i=tb->first_value;i<tb->first_value+tb->value_count;i++){const TurboJSSSAValue*v=&g->values[i];uint32_t src;if(v->opcode!=TURBOJS_SSA_PHI)continue;src=pi==0?v->left:v->right;if(src==TURBOJS_SSA_NO_VALUE||!load(b,0,src)||!store(b,temp_base+k,0))return 0;k++;}
k=0;for(i=tb->first_value;i<tb->first_value+tb->value_count;i++){const TurboJSSSAValue*v=&g->values[i];if(v->opcode!=TURBOJS_SSA_PHI)continue;if(!load(b,0,temp_base+k)||!store(b,v->id,0))return 0;k++;}*count+=k;return 1;}
TurboJSIRStatus turbojs_region_native_compile_stack(const TurboJSSSAGraph*g,TurboJSRegionNativeFunction**out,TurboJSRegionNativeStats*st,TurboJSIRDiagnostic*d){
#if !defined(__x86_64__) && !defined(_M_X64)
(void)g;(void)out;(void)st;return fail(d,TURBOJS_IR_UNSUPPORTED,0,"region native compiler requires x86-64");
#else
Buf b={0};Patches q={0};size_t *label=NULL;TurboJSRegionNativeFunction*f=NULL;TurboJSLinearScanResult a={0};uint32_t bi,i,maxphi=0,phis=0,moves=0;size_t argc=0,frame,temp_base;TurboJSIRStatus rs;
if(!g||!out)return fail(d,TURBOJS_IR_INVALID_ARGUMENT,0,"invalid region native input");
*out=NULL;
if(st)memset(st,0,sizeof(*st));
if(!TurboJS_SSAVerify(g))return fail(d,TURBOJS_IR_INVALID_OPCODE,0,"invalid SSA graph");
for(bi=0;bi<g->block_count;bi++){uint32_t n=0;for(i=g->blocks[bi].first_value;i<g->blocks[bi].first_value+g->blocks[bi].value_count;i++){const TurboJSSSAValue*v=&g->values[i];if(v->opcode==TURBOJS_SSA_PHI){n++;phis++;}if(v->opcode==TURBOJS_SSA_ARGUMENT&&(size_t)(v->immediate+1)>argc)argc=(size_t)v->immediate+1;}if(n>maxphi)maxphi=n;}
rs=TurboJS_LinearScanAllocate(g,6,8,&a);if(rs!=TURBOJS_IR_OK)return fail(d,rs,0,"linear scan failed");label=malloc(g->block_count*sizeof(*label));f=calloc(1,sizeof(*f));if(!label||!f){rs=TURBOJS_IR_OUT_OF_MEMORY;goto bad;}for(bi=0;bi<g->block_count;bi++)label[bi]=SIZE_MAX;
temp_base=(uint32_t)g->value_count;frame=(g->value_count+maxphi)*8u;frame=(frame+15u)&~15u;
if(!e8(&b,0x55)||!rex(&b,1,5,0,4)||!e8(&b,0x89)||!e8(&b,0xE5))goto oom;
if(frame&&(!rex(&b,1,0,0,4)||!e8(&b,0x81)||!e8(&b,0xEC)||!e32(&b,(uint32_t)frame)))goto oom;
#if defined(_WIN32)
if(!movrr(&b,11,1)||!movrr(&b,10,2))goto oom;
#else
if(!movrr(&b,11,7)||!movrr(&b,10,6))goto oom;
#endif
for(bi=0;bi<g->block_count;bi++){const TurboJSSSABlock*bl=&g->blocks[bi];int terminated=0;label[bi]=b.n;for(i=bl->first_value;i<bl->first_value+bl->value_count;i++){const TurboJSSSAValue*v=&g->values[i];uint32_t target,other;size_t jo,local;
if(v->removed||v->opcode==TURBOJS_SSA_PHI||v->opcode==TURBOJS_SSA_NOP)continue;
switch(v->opcode){case TURBOJS_SSA_ARGUMENT:if(!rex(&b,1,0,0,11)||!e8(&b,0x8B)||!e8(&b,0x83)||!e32(&b,(uint32_t)v->immediate*8u)||!store(&b,v->id,0))goto oom;break;
case TURBOJS_SSA_CONSTANT_I64:if(!movi(&b,0,(uint64_t)v->immediate)||!store(&b,v->id,0))goto oom;break;
case TURBOJS_SSA_ADD_I64:case TURBOJS_SSA_SUB_I64:case TURBOJS_SSA_MUL_I64:case TURBOJS_SSA_AND_I64:case TURBOJS_SSA_OR_I64:case TURBOJS_SSA_XOR_I64:if(!load(&b,0,v->left)||!load(&b,1,v->right))goto oom;if(v->opcode==TURBOJS_SSA_ADD_I64){if(!bin(&b,0x01,0,1))goto oom;}else if(v->opcode==TURBOJS_SSA_SUB_I64){if(!bin(&b,0x29,0,1))goto oom;}else if(v->opcode==TURBOJS_SSA_MUL_I64){if(!imul(&b,0,1))goto oom;}else if(v->opcode==TURBOJS_SSA_AND_I64){if(!bin(&b,0x21,0,1))goto oom;}else if(v->opcode==TURBOJS_SSA_OR_I64){if(!bin(&b,0x09,0,1))goto oom;}else if(!bin(&b,0x31,0,1))goto oom;if(!store(&b,v->id,0))goto oom;break;
case TURBOJS_SSA_LESS_THAN_I64:case TURBOJS_SSA_LESS_EQUAL_I64:case TURBOJS_SSA_GREATER_THAN_I64:case TURBOJS_SSA_GREATER_EQUAL_I64:case TURBOJS_SSA_EQUAL_I64:{uint8_t cc=v->opcode==TURBOJS_SSA_LESS_THAN_I64?0x9C:v->opcode==TURBOJS_SSA_LESS_EQUAL_I64?0x9E:v->opcode==TURBOJS_SSA_GREATER_THAN_I64?0x9F:v->opcode==TURBOJS_SSA_GREATER_EQUAL_I64?0x9D:0x94;if(!load(&b,0,v->left)||!load(&b,1,v->right)||!bin(&b,0x39,0,1)||!e8(&b,0x0F)||!e8(&b,cc)||!e8(&b,0xC0)||!e8(&b,0x48)||!e8(&b,0x0F)||!e8(&b,0xB6)||!e8(&b,0xC0)||!store(&b,v->id,0))goto oom;break;}
case TURBOJS_SSA_JUMP:target=(uint32_t)v->immediate;if(!edge_moves(&b,g,bi,target,temp_base,&moves)||!jmp(&b,&q,target))goto oom;terminated=1;break;
case TURBOJS_SSA_BRANCH_TRUE:case TURBOJS_SSA_BRANCH_FALSE:target=(uint32_t)v->immediate;other=bl->successors[0]==target?(bl->successor_count>1?bl->successors[1]:target):bl->successors[0];if(!load(&b,0,v->left)||!rex(&b,1,0,0,0)||!e8(&b,0x85)||!e8(&b,0xC0)||!e8(&b,0x0F)||!e8(&b,v->opcode==TURBOJS_SSA_BRANCH_TRUE?0x85:0x84))goto oom;jo=b.n;if(!e32(&b,0))goto oom;if(!edge_moves(&b,g,bi,other,temp_base,&moves)||!jmp(&b,&q,other))goto oom;local=b.n;p32(&b,jo,(int32_t)(local-(jo+4)));if(!edge_moves(&b,g,bi,target,temp_base,&moves)||!jmp(&b,&q,target))goto oom;terminated=1;break;
case TURBOJS_SSA_RETURN:if(!load(&b,0,v->left)||!rex(&b,1,0,0,10)||!e8(&b,0x89)||!e8(&b,0x02)||!e8(&b,0x31)||!e8(&b,0xC0)||!e8(&b,0xC9)||!e8(&b,0xC3))goto oom;terminated=1;break;
default:rs=fail(d,TURBOJS_IR_UNSUPPORTED,v->source_instruction,"unsupported multi-block SSA opcode");goto bad;}}
if(!terminated&&bl->successor_count==1){uint32_t target=bl->successors[0];if(!edge_moves(&b,g,bi,target,temp_base,&moves)||!jmp(&b,&q,target))goto oom;}
}
for(i=0;i<q.n;i++){if(q.p[i].target>=g->block_count||label[q.p[i].target]==SIZE_MAX){rs=fail(d,TURBOJS_IR_INVALID_TARGET,i,"unresolved region branch");goto bad;}p32(&b,q.p[i].off,(int32_t)(label[q.p[i].target]-(q.p[i].off+4)));}
f->allocation_size=b.n;f->code=turbojs_executable_memory_allocate(f->allocation_size);if(!f->code){rs=TURBOJS_IR_OUT_OF_MEMORY;goto bad;}memcpy(f->code,b.p,b.n);if(!turbojs_executable_memory_seal(f->code,f->allocation_size)){rs=TURBOJS_IR_UNSUPPORTED;goto bad;}f->code_size=b.n;f->argument_count=argc;f->allocation=a;memset(&a,0,sizeof(a));*out=f;if(st){size_t z;st->block_count=(uint32_t)g->block_count;st->phi_count=phis;st->edge_move_count=moves;st->allocated_intervals=(uint32_t)f->allocation.interval_count;st->spill_slots=f->allocation.spill_slot_count;for(z=0;z<f->allocation.interval_count;z++)if(f->allocation.intervals[z].spill_slot>=0)st->spilled_intervals++;st->native_code_bytes=f->code_size;}if(d){d->status=TURBOJS_IR_OK;d->message=NULL;d->instruction_index=0;}free(label);free(q.p);free(b.p);return TURBOJS_IR_OK;
oom:rs=TURBOJS_IR_OUT_OF_MEMORY;
bad:if(f){if(f->code)turbojs_executable_memory_free(f->code,f->allocation_size);free(f);}TurboJS_LinearScanResultDestroy(&a);free(label);free(q.p);free(b.p);return rs;
#endif
}
