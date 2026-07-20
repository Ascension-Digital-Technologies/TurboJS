#include "jit.h"
#include "../../runtime/executable_memory.h"
#include "simd_kernels.h"
#include "internal/monotonic_clock.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

struct TurboJSRegionNativeFunction {
    void *code; size_t code_size, allocation_size; size_t argument_count;
    TurboJSLinearScanResult allocation;
    TurboJSSSAGraph value_graph;
    TurboJSRegionObjectLayout object_layout;
    TurboJSRegionElementLayout element_layout;
    uint8_t has_object_layout;
    uint8_t has_element_layout;
    uint8_t value_mode; /* 0=int native, 1=evaluator, 2=thunk, 3=inline property PIC, 4=inline element */
    uint8_t property_pic_cases;
    uint8_t property_pic_is_store;
    uint8_t property_pic_dependency_guards;
    uint8_t inline_element_is_store;
    uint8_t inline_element_kind_guard;
    uint8_t inline_element_generation_guard;
    uint8_t inline_element_bounds_guard;
    uint8_t inline_element_dynamic_index;
    uint8_t inline_element_dynamic_store;
    uint8_t inline_element_loop;
    uint8_t inline_element_loop_length_hoist;
    uint8_t inline_element_loop_base_hoist;
    uint8_t inline_element_loop_bounds_eliminated;
    uint8_t inline_element_loop_unroll_factor;
    uint8_t inline_element_loop_accumulators;
    uint8_t inline_element_float64_loop;
    uint8_t inline_element_transform_loop;
    uint8_t simd_kernel; /* 0=none, 1=f64 sum, 2=f64 transform, 3=f64 binary, 4=f64 bound */
    uint8_t simd_level;
    uint8_t simd_source_arg;
    uint8_t simd_destination_arg;
    uint8_t simd_right_arg;
    uint8_t simd_limit_arg;
    uint8_t simd_scale_arg;
    uint8_t simd_bias_arg;
    uint8_t simd_transform_kind; /* 0=affine, 1=scale-only, 2=bias-only, 3=subtract-only, 4=dual-add, 5=dual-subtract, 6=min, 7=max, 8=clamp */
    uint32_t simd_source_generation;
    uint32_t simd_right_generation;
    uint32_t simd_destination_generation;
};
typedef struct { uint8_t *p; size_t n,c; } Buf;
typedef struct { size_t off; uint32_t target; } Patch;
typedef struct { Patch *p; size_t n,c; } Patches;
typedef struct { int reg; int spill; } Loc;
typedef struct { Loc src, dst; uint8_t is_phi, done; } EdgeMove;
static uint64_t region_now_ns(void){
    return turbojs_monotonic_now_ns();
}
static const unsigned gpr_map[5]={0,1,2,8,9}; /* rax, rcx, rdx, r8, r9 */
static int reserve(Buf*b,size_t n){size_t c;void*p;if(b->n+n<=b->c)return 1;c=b->c?b->c*2:512;while(c<b->n+n)c*=2;p=realloc(b->p,c);if(!p)return 0;b->p=p;b->c=c;return 1;}
static int e8(Buf*b,uint8_t x){if(!reserve(b,1))return 0;b->p[b->n++]=x;return 1;}
static int e32(Buf*b,uint32_t x){int i;for(i=0;i<4;i++)if(!e8(b,(uint8_t)(x>>(8*i))))return 0;return 1;}
static int e64(Buf*b,uint64_t x){int i;for(i=0;i<8;i++)if(!e8(b,(uint8_t)(x>>(8*i))))return 0;return 1;}
static void p32(Buf*b,size_t o,int32_t x){int i;for(i=0;i<4;i++)b->p[o+i]=(uint8_t)((uint32_t)x>>(8*i));}
static int rex(Buf*b,int w,unsigned r,unsigned x,unsigned m){return e8(b,(uint8_t)(0x40|(w?8:0)|((r&8)?4:0)|((x&8)?2:0)|((m&8)?1:0)));}
static int movrr(Buf*b,unsigned d,unsigned s){if(d==s)return 1;return rex(b,1,s,0,d)&&e8(b,0x89)&&e8(b,(uint8_t)(0xC0|((s&7)<<3)|(d&7)));}
static int movi(Buf*b,unsigned d,uint64_t x){return rex(b,1,0,0,d)&&e8(b,(uint8_t)(0xB8+(d&7)))&&e64(b,x);}
static int membp(Buf*b,uint8_t op,unsigned r,int disp){return rex(b,1,r,0,5)&&e8(b,op)&&e8(b,(uint8_t)(0x80|((r&7)<<3)|5))&&e32(b,(uint32_t)disp);}
static int slotdisp(uint32_t s){return -8*(int)(s+1);}
static int loads(Buf*b,unsigned r,uint32_t s){return membp(b,0x8B,r,slotdisp(s));}
static int stores(Buf*b,uint32_t s,unsigned r){return membp(b,0x89,r,slotdisp(s));}
#if defined(__x86_64__) && !defined(_WIN32)
static int memdisp(Buf*b,uint8_t op,unsigned r,unsigned base,int32_t disp){
    if(!rex(b,1,r,0,base)||!e8(b,op))return 0;
    if((base&7)==4){
        if(!e8(b,(uint8_t)(0x80|((r&7)<<3)|4))||!e8(b,(uint8_t)(0x20|(base&7))))return 0;
    }else if(!e8(b,(uint8_t)(0x80|((r&7)<<3)|(base&7))))return 0;
    return e32(b,(uint32_t)disp);
}
static int loadm(Buf*b,unsigned d,unsigned base,int32_t disp){return memdisp(b,0x8B,d,base,disp);}
static int storem(Buf*b,unsigned base,int32_t disp,unsigned s){return memdisp(b,0x89,s,base,disp);}
static int xmm_mem(Buf*b,uint8_t op,unsigned xmm,unsigned base,int32_t disp){
    if(!e8(b,0xF2)||!rex(b,0,xmm,0,base)||!e8(b,0x0F)||!e8(b,op))return 0;
    if((base&7)==4){
        if(!e8(b,(uint8_t)(0x80|((xmm&7)<<3)|4))||!e8(b,(uint8_t)(0x20|(base&7))))return 0;
    }else if(!e8(b,(uint8_t)(0x80|((xmm&7)<<3)|(base&7))))return 0;
    return e32(b,(uint32_t)disp);
}
static int movsd_load(Buf*b,unsigned xmm,unsigned base,int32_t disp){return xmm_mem(b,0x10,xmm,base,disp);}
static int movsd_store(Buf*b,unsigned base,int32_t disp,unsigned xmm){return xmm_mem(b,0x11,xmm,base,disp);}
static int addsd(Buf*b,unsigned d,unsigned s){return e8(b,0xF2)&&rex(b,0,d,0,s)&&e8(b,0x0F)&&e8(b,0x58)&&e8(b,(uint8_t)(0xC0|((d&7)<<3)|(s&7)));}
static int mulsd(Buf*b,unsigned d,unsigned s){return e8(b,0xF2)&&rex(b,0,d,0,s)&&e8(b,0x0F)&&e8(b,0x59)&&e8(b,(uint8_t)(0xC0|((d&7)<<3)|(s&7)));}
static int movq_xmm_gpr(Buf*b,unsigned xmm,unsigned gpr){return e8(b,0x66)&&rex(b,1,xmm,0,gpr)&&e8(b,0x0F)&&e8(b,0x6E)&&e8(b,(uint8_t)(0xC0|((xmm&7)<<3)|(gpr&7)));}
static int xorpd(Buf*b,unsigned d,unsigned s){return e8(b,0x66)&&rex(b,0,d,0,s)&&e8(b,0x0F)&&e8(b,0x57)&&e8(b,(uint8_t)(0xC0|((d&7)<<3)|(s&7)));}
#endif
static int binrr(Buf*b,uint8_t op,unsigned d,unsigned s){return rex(b,1,s,0,d)&&e8(b,op)&&e8(b,(uint8_t)(0xC0|((s&7)<<3)|(d&7)));}
static int binrm(Buf*b,uint8_t op,unsigned d,uint32_t s){return rex(b,1,d,0,5)&&e8(b,op)&&e8(b,(uint8_t)(0x80|((d&7)<<3)|5))&&e32(b,(uint32_t)slotdisp(s));}
static int imulrr(Buf*b,unsigned d,unsigned s){return rex(b,1,d,0,s)&&e8(b,0x0F)&&e8(b,0xAF)&&e8(b,(uint8_t)(0xC0|((d&7)<<3)|(s&7)));}
static int imulrm(Buf*b,unsigned d,uint32_t s){return rex(b,1,d,0,5)&&e8(b,0x0F)&&e8(b,0xAF)&&e8(b,(uint8_t)(0x80|((d&7)<<3)|5))&&e32(b,(uint32_t)slotdisp(s));}
static int binri32(Buf*b,unsigned d,unsigned ext,int32_t x){return rex(b,1,0,0,d)&&e8(b,0x81)&&e8(b,(uint8_t)(0xC0|((ext&7)<<3)|(d&7)))&&e32(b,(uint32_t)x);}
static int imulri32(Buf*b,unsigned d,int32_t x){return rex(b,1,d,0,d)&&e8(b,0x69)&&e8(b,(uint8_t)(0xC0|((d&7)<<3)|(d&7)))&&e32(b,(uint32_t)x);}
static int shiftri8(Buf*b,unsigned d,unsigned ext,uint8_t x){return rex(b,1,0,0,d)&&e8(b,0xC1)&&e8(b,(uint8_t)(0xC0|((ext&7)<<3)|(d&7)))&&e8(b,(uint8_t)(x&63u));}
static int shiftcl(Buf*b,unsigned d,unsigned ext){return rex(b,1,0,0,d)&&e8(b,0xD3)&&e8(b,(uint8_t)(0xC0|((ext&7)<<3)|(d&7)));}
static int addpatch(Patches*q,size_t off,uint32_t t){void*p;if(q->n==q->c){size_t c=q->c?q->c*2:16;p=realloc(q->p,c*sizeof(*q->p));if(!p)return 0;q->p=p;q->c=c;}q->p[q->n++]=(Patch){off,t};return 1;}
static int jmp(Buf*b,Patches*q,uint32_t t){size_t o;if(!e8(b,0xE9))return 0;o=b->n;if(!e32(b,0))return 0;return addpatch(q,o,t);}
static TurboJSIRStatus fail(TurboJSIRDiagnostic*d,TurboJSIRStatus s,size_t i,const char*m){if(d){d->status=s;d->instruction_index=i;d->message=m;}return s;}
static uint32_t pred_index(const TurboJSSSABlock*b,uint32_t pred){uint32_t i;for(i=0;i<b->predecessor_count;i++)if(b->predecessors[i]==pred)return i;return UINT32_MAX;}
static void region_dfs_rpo(const TurboJSSSAGraph*g,uint32_t b,uint8_t*seen,uint32_t*post,size_t*n){uint32_t i;if(b>=g->block_count||seen[b]||g->blocks[b].removed||!g->blocks[b].reachable)return;seen[b]=1;for(i=0;i<g->blocks[b].successor_count;i++)region_dfs_rpo(g,g->blocks[b].successors[i],seen,post,n);post[(*n)++]=b;}
static Loc loc_for_pos(const TurboJSLinearScanResult*a,uint32_t id,uint32_t pos){size_t i;Loc l={-1,-1};
for(i=0;i<a->fragment_count;i++){const TurboJSLiveIntervalFragment*f=&a->fragments[i];if(f->value_id==id&&pos>=f->start&&pos<=f->end){if(f->physical_register>=0&&f->physical_register<5)l.reg=(int)gpr_map[f->physical_register];else if(f->spill_slot>=0)l.spill=f->spill_slot;return l;}}
/* Never manufacture a location outside the value's live interval. Doing so
   makes block-edge transfer generation read values that are not yet defined. */
for(i=0;i<a->interval_count;i++) if(a->intervals[i].value_id==id){const TurboJSLiveInterval*x=&a->intervals[i];if(pos<x->start||pos>x->end)break;if(x->physical_register>=0&&x->physical_register<5)l.reg=(int)gpr_map[x->physical_register];else if(x->spill_slot>=0)l.spill=x->spill_slot;break;}
return l;}
static int same_loc(Loc a,Loc b){return a.reg==b.reg&&a.spill==b.spill;}
static int readloc(Buf*b,Loc l,unsigned scratch){if(l.reg>=0)return movrr(b,scratch,(unsigned)l.reg);if(l.spill>=0)return loads(b,scratch,(uint32_t)l.spill);return 0;}
static int writeloc(Buf*b,Loc l,unsigned src){if(l.reg>=0)return movrr(b,(unsigned)l.reg,src);if(l.spill>=0)return stores(b,(uint32_t)l.spill,src);return 0;}
static int emit_fragment_transitions(Buf*b,const TurboJSLinearScanResult*a,uint32_t pos){size_t i;for(i=0;i<a->fragment_count;i++){const TurboJSLiveIntervalFragment*f=&a->fragments[i];Loc from,to;if(f->start!=pos||pos==0)continue;from=loc_for_pos(a,f->value_id,pos-1);to=loc_for_pos(a,f->value_id,pos);if((from.reg<0&&from.spill<0)||same_loc(from,to))continue;if(!readloc(b,from,11)||!writeloc(b,to,11))return 0;}return 1;}
static int op_right(Buf*b,uint8_t rr,uint8_t rm,unsigned dst,Loc r){if(r.reg>=0)return binrr(b,rr,dst,(unsigned)r.reg);if(r.spill>=0)return binrm(b,rm,dst,(uint32_t)r.spill);return 0;}
static int mul_right(Buf*b,unsigned dst,Loc r){if(r.reg>=0)return imulrr(b,dst,(unsigned)r.reg);if(r.spill>=0)return imulrm(b,dst,(uint32_t)r.spill);return 0;}
static int loc_valid(Loc l){return l.reg>=0||l.spill>=0;}
static int value_is_used(const TurboJSSSAGraph*g,uint32_t id){uint32_t i;if(id>=g->value_count)return 0;for(i=0;i<g->value_count;i++){const TurboJSSSAValue*v=&g->values[i];if(v->removed)continue;if(v->left==id||v->right==id)return 1;}return 0;}
static int value_has_only_branch_use(const TurboJSSSAGraph*g,uint32_t id){uint32_t i,n=0;if(id>=g->value_count)return 0;for(i=0;i<g->value_count;i++){const TurboJSSSAValue*v=&g->values[i];if(v->removed)continue;if(v->left==id||v->right==id){n++;if(v->left!=id||(v->opcode!=TURBOJS_SSA_BRANCH_TRUE&&v->opcode!=TURBOJS_SSA_BRANCH_FALSE))return 0;}}return n==1;}
static int value_i32_constant(const TurboJSSSAGraph*g,uint32_t id,int32_t*out){const TurboJSSSAValue*v;if(id>=g->value_count)return 0;v=&g->values[id];if(v->removed||v->opcode!=TURBOJS_SSA_CONSTANT_I64||v->immediate<INT32_MIN||v->immediate>INT32_MAX)return 0;*out=(int32_t)v->immediate;return 1;}
static int emit_integer_cmp(Buf*b,const TurboJSSSAGraph*g,uint32_t left_id,Loc left,uint32_t right_id,Loc right){int32_t imm;(void)left_id;if(!readloc(b,left,11))return 0;if(value_i32_constant(g,right_id,&imm))return binri32(b,11,7,imm);return op_right(b,0x39,0x3B,11,right);}
static uint8_t comparison_setcc(TurboJSSSAOpcode op){switch(op){case TURBOJS_SSA_LESS_THAN_I64:return 0x9C;case TURBOJS_SSA_LESS_EQUAL_I64:return 0x9E;case TURBOJS_SSA_GREATER_THAN_I64:return 0x9F;case TURBOJS_SSA_GREATER_EQUAL_I64:return 0x9D;case TURBOJS_SSA_EQUAL_I64:return 0x94;default:return 0;}}
static int emit_move(Buf*b,Loc src,Loc dst){if(same_loc(src,dst))return 1;if(src.reg>=0&&dst.reg>=0)return movrr(b,(unsigned)dst.reg,(unsigned)src.reg);if(src.reg>=0&&dst.spill>=0)return stores(b,(uint32_t)dst.spill,(unsigned)src.reg);if(src.spill>=0&&dst.reg>=0)return loads(b,(unsigned)dst.reg,(uint32_t)src.spill);if(src.spill>=0&&dst.spill>=0)return loads(b,11,(uint32_t)src.spill)&&stores(b,(uint32_t)dst.spill,11);return 0;}
static int destination_is_source(const EdgeMove*m,uint32_t n,uint32_t at){uint32_t i;for(i=0;i<n;i++)if(!m[i].done&&i!=at&&same_loc(m[at].dst,m[i].src))return 1;return 0;}
static int schedule_edge_moves(Buf*b,EdgeMove*m,uint32_t n,uint32_t temp_slot,uint32_t*count,uint32_t*cycles){uint32_t left=n;while(left){uint32_t i;int progressed=0;for(i=0;i<n;i++){if(m[i].done)continue;if(!destination_is_source(m,n,i)){if(!emit_move(b,m[i].src,m[i].dst))return 0;m[i].done=1;left--;(*count)++;progressed=1;}}if(progressed)continue;/* A true cycle remains. Snapshot one source and rewrite every matching source to the temporary. */for(i=0;i<n&&m[i].done;i++){ /* find first pending edge move */ }if(i==n)return 0;if(!readloc(b,m[i].src,11)||!stores(b,temp_slot,11))return 0;if(cycles)(*cycles)++;{Loc old=m[i].src,tmp={-1,(int)temp_slot};uint32_t j;for(j=0;j<n;j++)if(!m[j].done&&same_loc(m[j].src,old))m[j].src=tmp;}}return 1;}
static int edge_moves(Buf*b,const TurboJSSSAGraph*g,const TurboJSLinearScanResult*a,uint32_t pred,uint32_t target,uint32_t temp_base,uint32_t*count,uint32_t*cycles){
const TurboJSSSABlock*tb=&g->blocks[target];uint32_t pi=pred_index(tb,pred),i,n=0,sp=a->block_end_positions[pred],dp=a->block_start_positions[target];EdgeMove*m;if(pi==UINT32_MAX)return 0;m=calloc(g->value_count+(tb->value_count?tb->value_count:1),sizeof(*m));if(!m)return 0;
/* Add phi moves first so their destinations can suppress redundant live-through
   transitions. Dead phis generate no edge traffic. */
for(i=tb->first_value;i<tb->first_value+tb->value_count;i++){const TurboJSSSAValue*v=&g->values[i];uint32_t src;Loc sl,dl;if(v->opcode!=TURBOJS_SSA_PHI||v->removed||!value_is_used(g,v->id))continue;src=pi==0?v->left:v->right;if(src==TURBOJS_SSA_NO_VALUE){free(m);return 0;}sl=loc_for_pos(a,src,sp);dl=loc_for_pos(a,v->id,a->value_positions[v->id]);if(!loc_valid(sl)||!loc_valid(dl)){free(m);return 0;}if(!same_loc(sl,dl))m[n++]=(EdgeMove){sl,dl,1,0};}
for(i=0;i<g->value_count;i++){const TurboJSSSAValue*v=&g->values[i];Loc sl,dl;uint32_t j;int overwritten=0;if(v->removed||v->opcode==TURBOJS_SSA_PHI)continue;sl=loc_for_pos(a,i,sp);dl=loc_for_pos(a,i,dp);if(!loc_valid(sl)||!loc_valid(dl)||same_loc(sl,dl))continue;for(j=0;j<n;j++)if(m[j].is_phi&&same_loc(m[j].dst,dl)){overwritten=1;break;}if(!overwritten)m[n++]=(EdgeMove){sl,dl,0,0};}
if(n&&!schedule_edge_moves(b,m,n,temp_base,count,cycles)){free(m);return 0;}free(m);return 1;}
static TurboJSIRStatus turbojs_region_native_compile_register(const TurboJSSSAGraph*g,TurboJSRegionNativeFunction**out,TurboJSRegionNativeStats*st,TurboJSIRDiagnostic*d){
#if !defined(__x86_64__) && !defined(_M_X64)
(void)g;(void)out;(void)st;return fail(d,TURBOJS_IR_UNSUPPORTED,0,"region native compiler requires x86-64");
#else
Buf b={0};Patches q={0};size_t *label=NULL;uint32_t *order=NULL,*post=NULL;uint8_t *seen=NULL;TurboJSRegionNativeFunction*f=NULL;TurboJSLinearScanResult a={0};uint32_t bi,i,maxphi=0,maxedge=0,phis=0,moves=0,cycles=0;uint64_t compile_start=region_now_ns();size_t argc=0,frame,temp_base,args_base,order_count=0,oi;TurboJSIRStatus rs;
if(!g||!out)return fail(d,TURBOJS_IR_INVALID_ARGUMENT,0,"invalid region native input");
*out=NULL;
if(st)memset(st,0,sizeof(*st));
if(!TurboJS_SSAVerify(g))return fail(d,TURBOJS_IR_INVALID_OPCODE,0,"invalid SSA graph");
for(bi=0;bi<g->block_count;bi++){uint32_t n=0;for(i=g->blocks[bi].first_value;i<g->blocks[bi].first_value+g->blocks[bi].value_count;i++){const TurboJSSSAValue*v=&g->values[i];if(v->opcode==TURBOJS_SSA_PHI){n++;phis++;}if(v->opcode==TURBOJS_SSA_ARGUMENT&&(size_t)(v->immediate+1)>argc)argc=(size_t)v->immediate+1;}if(n>maxphi)maxphi=n;}
rs=TurboJS_LinearScanAllocate(g,5,8,&a);if(rs!=TURBOJS_IR_OK)return fail(d,rs,0,"linear scan failed");label=malloc(g->block_count*sizeof(*label));order=malloc(g->block_count*sizeof(*order));post=malloc(g->block_count*sizeof(*post));seen=calloc(g->block_count,1);f=calloc(1,sizeof(*f));if(!label||!order||!post||!seen||!f){rs=TURBOJS_IR_OUT_OF_MEMORY;goto bad;}for(bi=0;bi<g->block_count;bi++)label[bi]=SIZE_MAX;region_dfs_rpo(g,g->entry_block,seen,post,&order_count);for(i=0;i<order_count;i++)order[i]=post[order_count-1-i];for(bi=0;bi<g->block_count;bi++)if(!seen[bi])order[order_count++]=bi;
for(bi=0;bi<g->block_count;bi++){uint32_t si;for(si=0;si<g->blocks[bi].successor_count;si++){uint32_t target=g->blocks[bi].successors[si],sp=a.block_end_positions[bi],dp=a.block_start_positions[target],n=0,pi=pred_index(&g->blocks[target],bi);for(i=0;i<g->value_count;i++){const TurboJSSSAValue*v=&g->values[i];Loc sl,dl;if(v->removed||v->opcode==TURBOJS_SSA_PHI)continue;sl=loc_for_pos(&a,i,sp);dl=loc_for_pos(&a,i,dp);if((sl.reg>=0||sl.spill>=0)&&(dl.reg>=0||dl.spill>=0)&&!same_loc(sl,dl))n++;}if(pi!=UINT32_MAX){for(i=g->blocks[target].first_value;i<g->blocks[target].first_value+g->blocks[target].value_count;i++){const TurboJSSSAValue*v=&g->values[i];uint32_t src;Loc sl,dl;if(v->opcode!=TURBOJS_SSA_PHI)continue;src=pi==0?v->left:v->right;sl=loc_for_pos(&a,src,sp);dl=loc_for_pos(&a,v->id,a.value_positions[v->id]);if((sl.reg>=0||sl.spill>=0)&&(dl.reg>=0||dl.spill>=0)&&!same_loc(sl,dl))n++;}}if(n>maxedge)maxedge=n;}}
temp_base=a.spill_slot_count;args_base=temp_base+(maxedge?1u:0u);frame=(args_base+1u)*8u;frame=(frame+15u)&~15u;
if(!e8(&b,0x55)||!rex(&b,1,5,0,4)||!e8(&b,0x89)||!e8(&b,0xE5))goto oom;
if(frame&&(!rex(&b,1,0,0,4)||!e8(&b,0x81)||!e8(&b,0xEC)||!e32(&b,(uint32_t)frame)))goto oom;
#if defined(_WIN32)
if(!stores(&b,(uint32_t)args_base,1)||!movrr(&b,10,2))goto oom;
#else
if(!stores(&b,(uint32_t)args_base,7)||!movrr(&b,10,6))goto oom;
#endif
for(oi=0;oi<order_count;oi++){bi=order[oi];const TurboJSSSABlock*bl=&g->blocks[bi];int terminated=0;label[bi]=b.n;for(i=bl->first_value;i<bl->first_value+bl->value_count;i++){const TurboJSSSAValue*v=&g->values[i];uint32_t target,other,pos;size_t jo,local;Loc dst,left,right;if(v->removed||v->opcode==TURBOJS_SSA_PHI||v->opcode==TURBOJS_SSA_NOP)continue;pos=a.value_positions[v->id];if(!emit_fragment_transitions(&b,&a,pos))goto oom;dst=loc_for_pos(&a,v->id,pos);left=loc_for_pos(&a,v->left,pos);right=loc_for_pos(&a,v->right,pos);
switch(v->opcode){case TURBOJS_SSA_ARGUMENT:if(!loads(&b,11,(uint32_t)args_base)||!rex(&b,1,11,0,11)||!e8(&b,0x8B)||!e8(&b,0x9B)||!e32(&b,(uint32_t)v->immediate*8u)||!writeloc(&b,dst,11))goto oom;break;
case TURBOJS_SSA_CONSTANT_I64:if(!movi(&b,11,(uint64_t)v->immediate)||!writeloc(&b,dst,11))goto oom;break;
case TURBOJS_SSA_ADD_I64:case TURBOJS_SSA_SUB_I64:case TURBOJS_SSA_MUL_I64:case TURBOJS_SSA_AND_I64:case TURBOJS_SSA_OR_I64:case TURBOJS_SSA_XOR_I64:case TURBOJS_SSA_SHL_I64:case TURBOJS_SSA_SAR_I64:case TURBOJS_SSA_SHR_I64:{int32_t imm;if(!readloc(&b,left,11))goto oom;if(v->opcode==TURBOJS_SSA_SHL_I64||v->opcode==TURBOJS_SSA_SAR_I64||v->opcode==TURBOJS_SSA_SHR_I64){unsigned ext=v->opcode==TURBOJS_SSA_SHL_I64?4u:(v->opcode==TURBOJS_SSA_SHR_I64?5u:7u);if(value_i32_constant(g,v->right,&imm)){if(!shiftri8(&b,11,ext,(uint8_t)imm))goto oom;}else{if(!readloc(&b,right,1)||!shiftcl(&b,11,ext))goto oom;}}else if(value_i32_constant(g,v->right,&imm)){if(v->opcode==TURBOJS_SSA_ADD_I64){if(!binri32(&b,11,0,imm))goto oom;}else if(v->opcode==TURBOJS_SSA_SUB_I64){if(!binri32(&b,11,5,imm))goto oom;}else if(v->opcode==TURBOJS_SSA_MUL_I64){if(!imulri32(&b,11,imm))goto oom;}else if(v->opcode==TURBOJS_SSA_AND_I64){if(!binri32(&b,11,4,imm))goto oom;}else if(v->opcode==TURBOJS_SSA_OR_I64){if(!binri32(&b,11,1,imm))goto oom;}else if(!binri32(&b,11,6,imm))goto oom;}else if(v->opcode==TURBOJS_SSA_ADD_I64){if(!op_right(&b,0x01,0x03,11,right))goto oom;}else if(v->opcode==TURBOJS_SSA_SUB_I64){if(!op_right(&b,0x29,0x2B,11,right))goto oom;}else if(v->opcode==TURBOJS_SSA_MUL_I64){if(!mul_right(&b,11,right))goto oom;}else if(v->opcode==TURBOJS_SSA_AND_I64){if(!op_right(&b,0x21,0x23,11,right))goto oom;}else if(v->opcode==TURBOJS_SSA_OR_I64){if(!op_right(&b,0x09,0x0B,11,right))goto oom;}else if(!op_right(&b,0x31,0x33,11,right))goto oom;if(!writeloc(&b,dst,11))goto oom;break;}
case TURBOJS_SSA_LESS_THAN_I64:case TURBOJS_SSA_LESS_EQUAL_I64:case TURBOJS_SSA_GREATER_THAN_I64:case TURBOJS_SSA_GREATER_EQUAL_I64:case TURBOJS_SSA_EQUAL_I64:{uint8_t cc=comparison_setcc(v->opcode);if(value_has_only_branch_use(g,v->id)&&v->opcode==TURBOJS_SSA_LESS_THAN_I64)break;if(!cc||!emit_integer_cmp(&b,g,v->left,left,v->right,right)||!rex(&b,0,0,0,11)||!e8(&b,0x0F)||!e8(&b,cc)||!e8(&b,0xC3)||!rex(&b,1,11,0,11)||!e8(&b,0x0F)||!e8(&b,0xB6)||!e8(&b,0xDB)||!writeloc(&b,dst,11))goto oom;break;}
case TURBOJS_SSA_JUMP:target=(uint32_t)v->immediate;if(!edge_moves(&b,g,&a,bi,target,temp_base,&moves,&cycles))goto oom;if(!(oi+1<order_count&&order[oi+1]==target)&&!jmp(&b,&q,target))goto oom;terminated=1;break;
case TURBOJS_SSA_BRANCH_TRUE:case TURBOJS_SSA_BRANCH_FALSE:{const TurboJSSSAValue*cv=(v->left<g->value_count)?&g->values[v->left]:NULL;target=(uint32_t)v->immediate;other=bl->successors[0]==target?(bl->successor_count>1?bl->successors[1]:target):bl->successors[0];if(cv&&cv->opcode==TURBOJS_SSA_LESS_THAN_I64&&value_has_only_branch_use(g,cv->id)){uint32_t cp=a.value_positions[cv->id];Loc cl=loc_for_pos(&a,cv->left,cp),cr=loc_for_pos(&a,cv->right,cp);if(!emit_integer_cmp(&b,g,cv->left,cl,cv->right,cr)||!e8(&b,0x0F)||!e8(&b,v->opcode==TURBOJS_SSA_BRANCH_TRUE?0x8C:0x8D))goto oom;}else{if(!readloc(&b,left,11)||!rex(&b,1,11,0,11)||!e8(&b,0x85)||!e8(&b,0xDB)||!e8(&b,0x0F)||!e8(&b,v->opcode==TURBOJS_SSA_BRANCH_TRUE?0x85:0x84))goto oom;}jo=b.n;if(!e32(&b,0))goto oom;if(!edge_moves(&b,g,&a,bi,other,temp_base,&moves,&cycles)||!jmp(&b,&q,other))goto oom;local=b.n;p32(&b,jo,(int32_t)(local-(jo+4)));if(!edge_moves(&b,g,&a,bi,target,temp_base,&moves,&cycles)||!jmp(&b,&q,target))goto oom;terminated=1;break;}
case TURBOJS_SSA_RETURN:if(!readloc(&b,left,11)||!rex(&b,1,11,0,10)||!e8(&b,0x89)||!e8(&b,0x1A)||!e8(&b,0x31)||!e8(&b,0xC0)||!e8(&b,0xC9)||!e8(&b,0xC3))goto oom;terminated=1;break;
default:unsupported_multi:rs=fail(d,TURBOJS_IR_UNSUPPORTED,v->source_instruction,"unsupported multi-block SSA opcode");goto bad;}}
if(!terminated&&bl->successor_count==1){uint32_t target=bl->successors[0];if(!edge_moves(&b,g,&a,bi,target,temp_base,&moves,&cycles))goto oom;if(!(oi+1<order_count&&order[oi+1]==target)&&!jmp(&b,&q,target))goto oom;}}
for(i=0;i<q.n;i++){if(q.p[i].target>=g->block_count||label[q.p[i].target]==SIZE_MAX){rs=fail(d,TURBOJS_IR_INVALID_TARGET,i,"unresolved region branch");goto bad;}p32(&b,q.p[i].off,(int32_t)(label[q.p[i].target]-(q.p[i].off+4)));}
f->allocation_size=b.n;f->code=turbojs_executable_memory_allocate(f->allocation_size);if(!f->code){rs=TURBOJS_IR_OUT_OF_MEMORY;goto bad;}memcpy(f->code,b.p,b.n);if(!turbojs_executable_memory_seal(f->code,f->allocation_size)){rs=TURBOJS_IR_UNSUPPORTED;goto bad;}f->code_size=b.n;f->argument_count=argc;f->allocation=a;memset(&a,0,sizeof(a));*out=f;if(st){size_t z;st->block_count=(uint32_t)g->block_count;st->phi_count=phis;st->edge_move_count=moves;st->cycle_break_count=cycles;st->allocated_intervals=(uint32_t)f->allocation.interval_count;st->fragment_count=(uint32_t)f->allocation.fragment_count;st->split_count=f->allocation.split_count;st->phi_coalesce_candidates=f->allocation.phi_coalesce_candidates;st->phi_coalesce_successes=f->allocation.phi_coalesce_successes;st->phi_coalesce_rejected=f->allocation.phi_coalesce_rejected;st->spill_slots=f->allocation.spill_slot_count;st->native_code_bytes=f->code_size;st->frame_bytes=frame;st->compile_time_ns=region_now_ns()-compile_start;for(z=0;z<f->allocation.interval_count;z++){if(f->allocation.intervals[z].physical_register>=0)st->register_values++;if(f->allocation.intervals[z].spill_slot>=0)st->spilled_intervals++;}}if(d){d->status=TURBOJS_IR_OK;d->message=NULL;d->instruction_index=0;}free(label);free(order);free(post);free(seen);free(q.p);free(b.p);return TURBOJS_IR_OK;
oom:rs=TURBOJS_IR_OUT_OF_MEMORY;
bad:if(f){if(f->code)turbojs_executable_memory_free(f->code,f->allocation_size);free(f);}TurboJS_LinearScanResultDestroy(&a);free(label);free(order);free(post);free(seen);free(q.p);free(b.p);return rs;
#endif
}
static int region_has_property_loads(const TurboJSSSAGraph *g){size_t i;for(i=0;i<g->value_count;i++)if(!g->values[i].removed&&g->values[i].opcode==TURBOJS_SSA_PROPERTY_LOAD)return 1;return 0;}
static int region_has_property_stores(const TurboJSSSAGraph *g){size_t i;for(i=0;i<g->value_count;i++)if(!g->values[i].removed&&g->values[i].opcode==TURBOJS_SSA_PROPERTY_STORE)return 1;return 0;}
static int region_has_element_ops(const TurboJSSSAGraph *g){size_t i;for(i=0;i<g->value_count;i++)if(!g->values[i].removed&&(g->values[i].opcode==TURBOJS_SSA_ELEMENT_LOAD||g->values[i].opcode==TURBOJS_SSA_ELEMENT_STORE))return 1;return 0;}
static int region_has_proven_element_loop(const TurboJSSSAGraph *g){size_t i;for(i=0;i<g->value_count;i++){const TurboJSSSAValue*v=&g->values[i];if(v->opcode==TURBOJS_SSA_ELEMENT_LOAD&&v->element_bounds_proven&&v->element_length_hoisted&&v->element_base_hoisted)return 1;}return 0;}
static TurboJSIRStatus region_value_execute(const TurboJSRegionNativeFunction*f,const TurboJSRegionValue*args,size_t n,const TurboJSRegionValueOps*ops,void*opaque,TurboJSRegionValue*r);

#if defined(__x86_64__) && !defined(_WIN32)
typedef TurboJSIRStatus (*RegionValueNativeFn)(const TurboJSRegionValue*,size_t,const TurboJSRegionValueOps*,void*,TurboJSRegionValue*);
static int region_find_direct_property_load(const TurboJSSSAGraph *g,
                                            const TurboJSSSAValue **arg,
                                            const TurboJSSSAValue **load)
{
    size_t i; const TurboJSSSAValue *a=NULL,*l=NULL,*ret=NULL;
    if(!g||g->block_count!=1)return 0;
    for(i=0;i<g->value_count;i++){
        const TurboJSSSAValue*v=&g->values[i];
        if(v->removed||v->opcode==TURBOJS_SSA_NOP)continue;
        if(v->opcode==TURBOJS_SSA_ARGUMENT){if(a)return 0;a=v;}
        else if(v->opcode==TURBOJS_SSA_PROPERTY_LOAD){if(l)return 0;l=v;}
        else if(v->opcode==TURBOJS_SSA_RETURN){if(ret)return 0;ret=v;}
        else return 0;
    }
    if(!a||!l||!ret||l->left!=a->id||ret->left!=l->id||
       !l->property_case_count||l->property_case_count>TURBOJS_PROPERTY_PIC_MAX_CASES)return 0;
    *arg=a;*load=l;return 1;
}

static int region_find_direct_property_store(const TurboJSSSAGraph *g,
                                             const TurboJSSSAValue **object_arg,
                                             const TurboJSSSAValue **value_arg,
                                             const TurboJSSSAValue **store)
{
    size_t i; const TurboJSSSAValue *a0=NULL,*a1=NULL,*st=NULL,*zero=NULL,*ret=NULL;
    if(!g||g->block_count!=1)return 0;
    for(i=0;i<g->value_count;i++){
        const TurboJSSSAValue*v=&g->values[i];
        if(v->removed||v->opcode==TURBOJS_SSA_NOP)continue;
        if(v->opcode==TURBOJS_SSA_ARGUMENT){
            if(v->immediate==0){if(a0)return 0;a0=v;}
            else if(v->immediate==1){if(a1)return 0;a1=v;}
            else return 0;
        }else if(v->opcode==TURBOJS_SSA_PROPERTY_STORE){if(st)return 0;st=v;}
        else if(v->opcode==TURBOJS_SSA_CONSTANT_I64&&v->immediate==0){if(zero)return 0;zero=v;}
        else if(v->opcode==TURBOJS_SSA_RETURN){if(ret)return 0;ret=v;}
        else return 0;
    }
    if(!a0||!a1||!st||!zero||!ret||st->left!=a0->id||st->right!=a1->id||ret->left!=zero->id||
       !st->property_case_count||st->property_case_count>TURBOJS_PROPERTY_PIC_MAX_CASES)return 0;
    for(i=0;i<st->property_case_count;i++)if(!(st->property_case_flags[i]&TURBOJS_PROPERTY_FEEDBACK_WRITABLE))return 0;
    *object_arg=a0;*value_arg=a1;*store=st;return 1;
}

static int emit_jcc32(Buf*b,uint8_t cc,size_t*patch){
    if(!e8(b,0x0F)||!e8(b,cc))return 0;
    *patch=b->n;
    return e32(b,0);
}
static int emit_cmp_rr(Buf*b,unsigned left,unsigned right){
    return rex(b,1,right,0,left)&&e8(b,0x39)&&e8(b,(uint8_t)(0xC0|((right&7)<<3)|(left&7)));
}
static int emit_test_rr(Buf*b,unsigned reg){
    return rex(b,1,reg,0,reg)&&e8(b,0x85)&&e8(b,(uint8_t)(0xC0|((reg&7)<<3)|(reg&7)));
}
static int emit_call_reg(Buf*b,unsigned reg){
    return rex(b,0,0,0,reg)&&e8(b,0xFF)&&e8(b,(uint8_t)(0xD0|(reg&7)));
}
static int emit_property_dependency_guard(Buf*b,uint16_t generation,uint16_t flags,
                                          size_t *bail_patches,size_t *bail_count){
    size_t skip;
    /* ops is rooted in slot 2; guard_property_dependency is the fourth callback. */
    if(!loads(b,11,2)||!loadm(b,0,11,(int32_t)offsetof(TurboJSRegionValueOps,guard_property_dependency))||
       !emit_test_rr(b,0)||!emit_jcc32(b,0x84,&skip))return 0;
    if(!e8(b,0xBF)||!e32(b,generation)||!e8(b,0xBE)||!e32(b,flags)||!loads(b,2,3)||
       !emit_call_reg(b,0)||!e8(b,0x85)||!e8(b,0xC0)||!emit_jcc32(b,0x84,&bail_patches[(*bail_count)++]))return 0;
    p32(b,skip,(int32_t)(b->n-(skip+4)));
    return 1;
}

static int region_build_inline_property_pic(TurboJSRegionNativeFunction *f,
                                            const TurboJSRegionObjectLayout *layout)
{
    const TurboJSSSAValue *object_arg=NULL,*value_arg=NULL,*property=NULL;
    Buf b={0}; size_t bail_patches[32],bail_count=0;
    size_t bail,i; uint64_t slot_off; int is_store=0;
    if(!f||!layout||!layout->property_stride)return 0;
    if(region_find_direct_property_load(&f->value_graph,&object_arg,&property))is_store=0;
    else if(region_find_direct_property_store(&f->value_graph,&object_arg,&value_arg,&property))is_store=1;
    else return 0;
    if(layout->shape_offset>INT32_MAX||layout->property_storage_offset>INT32_MAX)return 0;
    /* Root args, argc, ops, opaque, result, decoded object, and stored value. */
    if(!e8(&b,0x55)||!e8(&b,0x48)||!e8(&b,0x89)||!e8(&b,0xE5)||
       !e8(&b,0x48)||!e8(&b,0x83)||!e8(&b,0xEC)||!e8(&b,0x40)||
       !stores(&b,0,7)||!stores(&b,1,6)||!stores(&b,2,2)||!stores(&b,3,1)||!stores(&b,4,8))goto fail;
    /* argc check. */
    if(!loads(&b,6,1)||!binri32(&b,6,7,(int32_t)((is_store?value_arg:object_arg)->immediate+1))||
       !emit_jcc32(&b,0x82,&bail_patches[bail_count++]))goto fail;
    if(!loads(&b,7,0)||!loadm(&b,0,7,(int32_t)(object_arg->immediate*8)))goto fail;
    if(is_store){if(!loadm(&b,10,7,(int32_t)(value_arg->immediate*8))||!stores(&b,6,10))goto fail;}
    if(layout->object_tag_mask){
        if(!movrr(&b,10,0)||!movi(&b,11,layout->object_tag_mask)||!binrr(&b,0x21,10,11)||
           !movi(&b,11,layout->object_tag_value)||!emit_cmp_rr(&b,10,11)||
           !emit_jcc32(&b,0x85,&bail_patches[bail_count++]))goto fail;
    }
    if(layout->object_pointer_mask!=UINT64_MAX){if(!movi(&b,11,layout->object_pointer_mask)||!binrr(&b,0x21,0,11))goto fail;}
    if(!stores(&b,5,0)||!loadm(&b,10,0,(int32_t)layout->shape_offset))goto fail;
    for(i=0;i<property->property_case_count;i++){
        size_t shape_miss;
        if(!movi(&b,11,(uint64_t)property->property_shapes[i])||!emit_cmp_rr(&b,10,11)||
           !emit_jcc32(&b,0x85,&shape_miss))goto fail;
        if(!emit_property_dependency_guard(&b,property->property_generations[i],property->property_case_flags[i],
                                           bail_patches,&bail_count))goto fail;
        if(!loads(&b,0,5))goto fail;
        if(layout->property_storage_indirect){if(!loadm(&b,0,0,(int32_t)layout->property_storage_offset))goto fail;}
        else if(layout->property_storage_offset&&!binri32(&b,0,0,(int32_t)layout->property_storage_offset))goto fail;
        slot_off=(uint64_t)property->property_indices[i]*layout->property_stride+layout->property_value_offset;
        if(slot_off>INT32_MAX)goto fail;
        if(is_store){
            if(!loads(&b,10,6)||!storem(&b,0,(int32_t)slot_off,10)||!loads(&b,8,4)||
               !movi(&b,10,0)||!storem(&b,8,0,10))goto fail;
        }else{
            if(!loadm(&b,10,0,(int32_t)slot_off)||!loads(&b,8,4)||!storem(&b,8,0,10))goto fail;
        }
        if(!e8(&b,0x31)||!e8(&b,0xC0)||!e8(&b,0xC9)||!e8(&b,0xC3))goto fail;
        p32(&b,shape_miss,(int32_t)(b.n-(shape_miss+4)));
        /* Reload decoded object shape after dependency callback clobbers caller-save registers. */
        if(i+1<property->property_case_count){if(!loads(&b,0,5)||!loadm(&b,10,0,(int32_t)layout->shape_offset))goto fail;}
    }
    bail=b.n;
    if(!e8(&b,0xB8)||!e32(&b,TURBOJS_IR_BAILOUT)||!e8(&b,0xC9)||!e8(&b,0xC3))goto fail;
    for(i=0;i<bail_count;i++)p32(&b,bail_patches[i],(int32_t)(bail-(bail_patches[i]+4)));
    f->allocation_size=b.n;f->code=turbojs_executable_memory_allocate(b.n);if(!f->code)goto fail;
    memcpy(f->code,b.p,b.n);if(!turbojs_executable_memory_seal(f->code,b.n))goto fail;
    f->code_size=b.n;f->object_layout=*layout;f->has_object_layout=1;
    f->property_pic_cases=property->property_case_count;
    f->property_pic_is_store=(uint8_t)is_store;
    for(i=0;i<property->property_case_count;i++)
        if(property->property_generations[i])f->property_pic_dependency_guards++;
    free(b.p);return 1;
fail:
    if(f&&f->code){turbojs_executable_memory_free(f->code,f->allocation_size);f->code=NULL;}
    free(b.p);return 0;
}


static int region_find_direct_element_load(const TurboJSSSAGraph *g,
                                           const TurboJSSSAValue **arg,
                                           const TurboJSSSAValue **index,
                                           const TurboJSSSAValue **element)
{
    size_t i; const TurboJSSSAValue *a=NULL,*idx=NULL,*el=NULL,*ret=NULL;
    if(!g||g->block_count!=1)return 0;
    for(i=0;i<g->value_count;i++){
        const TurboJSSSAValue*v=&g->values[i];
        if(v->removed||v->opcode==TURBOJS_SSA_NOP)continue;
        if(v->opcode==TURBOJS_SSA_ARGUMENT){
            if(v->immediate==0){if(a)return 0;a=v;}
            else if(v->immediate==1){if(idx)return 0;idx=v;}
            else return 0;
        }else if(v->opcode==TURBOJS_SSA_CONSTANT_I64){if(idx)return 0;idx=v;}
        else if(v->opcode==TURBOJS_SSA_ELEMENT_LOAD){if(el)return 0;el=v;}
        else if(v->opcode==TURBOJS_SSA_RETURN){if(ret)return 0;ret=v;}
        else return 0;
    }
    if(!a||!idx||!el||!ret||
       (idx->opcode==TURBOJS_SSA_CONSTANT_I64&&(idx->immediate<0||idx->immediate>UINT32_MAX))||
       el->left!=a->id||el->right!=idx->id||ret->left!=el->id)return 0;
    *arg=a;*index=idx;*element=el;return 1;
}

static int region_find_direct_element_store(const TurboJSSSAGraph *g,
                                            const TurboJSSSAValue **object_arg,
                                            const TurboJSSSAValue **index,
                                            const TurboJSSSAValue **stored,
                                            const TurboJSSSAValue **element)
{
    size_t i; const TurboJSSSAValue *args[3]={0},*idx=NULL,*val=NULL,*el=NULL,*ret=NULL;
    size_t argc=0;
    if(!g||g->block_count!=1)return 0;
    for(i=0;i<g->value_count;i++){
        const TurboJSSSAValue*v=&g->values[i];
        if(v->removed||v->opcode==TURBOJS_SSA_NOP)continue;
        if(v->opcode==TURBOJS_SSA_ARGUMENT){
            if(argc>=3)return 0;
            args[argc++]=v;
        }else if(v->opcode==TURBOJS_SSA_CONSTANT_I64){
            if(!idx)idx=v; else if(!val)val=v; else return 0;
        }else if(v->opcode==TURBOJS_SSA_ELEMENT_STORE){if(el)return 0;el=v;}
        else if(v->opcode==TURBOJS_SSA_RETURN){if(ret)return 0;ret=v;}
        else return 0;
    }
    if(!args[0]||!el||!ret||!(el->element_flags&TURBOJS_ELEMENT_FLAG_WRITABLE))return 0;
    if(el->left!=args[0]->id)return 0;
    if(el->right>=g->value_count||el->metadata>=g->value_count)return 0;
    idx=&g->values[el->right]; val=&g->values[el->metadata];
    if(!((idx->opcode==TURBOJS_SSA_CONSTANT_I64&&idx->immediate>=0&&idx->immediate<=UINT32_MAX)||
         (idx->opcode==TURBOJS_SSA_ARGUMENT&&idx->type==TURBOJS_SSA_TYPE_INT32)))return 0;
    if(!(val->opcode==TURBOJS_SSA_CONSTANT_I64||val->opcode==TURBOJS_SSA_ARGUMENT))return 0;
    if(ret->left!=val->id)return 0;
    *object_arg=args[0];*index=idx;*stored=val;*element=el;return 1;
}
static int emit_mask_compare_u64(Buf*b,unsigned reg,uint64_t mask,uint64_t expected,
                                 size_t *bail_patches,size_t *bail_count)
{
    if(!movrr(b,10,reg)||!movi(b,11,mask)||!binrr(b,0x21,10,11)||
       !movi(b,11,expected)||!emit_cmp_rr(b,10,11)||
       !emit_jcc32(b,0x85,&bail_patches[(*bail_count)++]))return 0;
    return 1;
}


static int region_find_element_sum_loop_i64(const TurboJSSSAGraph *g,
                                        const TurboJSSSAValue **object_arg,
                                        const TurboJSSSAValue **limit_arg,
                                        const TurboJSSSAValue **element)
{
    const TurboJSSSAValue *el = NULL, *ret = NULL, *sum_phi = NULL;
    const TurboJSSSAValue *sum_add = NULL, *index_phi = NULL, *cmp = NULL;
    size_t i;
    if(!g || !object_arg || !limit_arg || !element || g->block_count < 3) return 0;
    for(i=0;i<g->value_count;i++){
        const TurboJSSSAValue *v=&g->values[i];
        if(v->opcode==TURBOJS_SSA_ELEMENT_LOAD && v->element_bounds_proven &&
           v->element_length_hoisted && v->element_base_hoisted) el=v;
        else if(v->opcode==TURBOJS_SSA_RETURN) ret=v;
    }
    if(!el || !ret || el->left>=g->value_count || el->right>=g->value_count ||
       ret->left>=g->value_count) return 0;
    *object_arg=&g->values[el->left];
    index_phi=&g->values[el->right];
    sum_phi=&g->values[ret->left];
    if((*object_arg)->opcode!=TURBOJS_SSA_ARGUMENT ||
       index_phi->opcode!=TURBOJS_SSA_PHI || sum_phi->opcode!=TURBOJS_SSA_PHI) return 0;
    if(index_phi->left>=g->value_count || index_phi->right>=g->value_count ||
       sum_phi->left>=g->value_count || sum_phi->right>=g->value_count) return 0;
    if(g->values[index_phi->left].opcode!=TURBOJS_SSA_CONSTANT_I64 ||
       g->values[index_phi->left].immediate!=0 ||
       g->values[sum_phi->left].opcode!=TURBOJS_SSA_CONSTANT_I64 ||
       g->values[sum_phi->left].immediate!=0) return 0;
    sum_add=&g->values[sum_phi->right];
    if(sum_add->opcode!=TURBOJS_SSA_ADD_I64 ||
       !((sum_add->left==sum_phi->id && sum_add->right==el->id) ||
         (sum_add->right==sum_phi->id && sum_add->left==el->id))) return 0;
    for(i=0;i<g->value_count;i++){
        const TurboJSSSAValue *v=&g->values[i];
        if(v->opcode!=TURBOJS_SSA_LESS_THAN_I64) continue;
        if(v->left==index_phi->id && v->right==el->element_length_value){ cmp=v; break; }
    }
    if(!cmp || el->element_length_value>=g->value_count) return 0;
    *limit_arg=&g->values[el->element_length_value];
    if((*limit_arg)->opcode!=TURBOJS_SSA_ARGUMENT || (*limit_arg)->type!=TURBOJS_SSA_TYPE_INT32)
        return 0;
    {
        const TurboJSSSAValue *step=&g->values[index_phi->right];
        const TurboJSSSAValue *one;
        if(step->opcode!=TURBOJS_SSA_ADD_I64) return 0;
        if(step->left==index_phi->id) one=&g->values[step->right];
        else if(step->right==index_phi->id) one=&g->values[step->left];
        else return 0;
        if(one->opcode!=TURBOJS_SSA_CONSTANT_I64 || one->immediate!=1) return 0;
    }
    *element=el;
    return 1;
}


static int region_find_element_sum_loop_f64(const TurboJSSSAGraph *g,
                                             const TurboJSSSAValue **object_arg,
                                             const TurboJSSSAValue **limit_arg,
                                             const TurboJSSSAValue **element)
{
    const TurboJSSSAValue *el=NULL,*ret=NULL,*sum_phi=NULL,*sum_add=NULL,*index_phi=NULL,*cmp=NULL;
    size_t i;
    if(!g||!object_arg||!limit_arg||!element||g->block_count<3)return 0;
    for(i=0;i<g->value_count;i++){
        const TurboJSSSAValue*v=&g->values[i];
        if(v->opcode==TURBOJS_SSA_ELEMENT_LOAD&&v->element_kind==TURBOJS_ELEMENT_KIND_TYPED_F64&&
           v->type==TURBOJS_SSA_TYPE_FLOAT64&&v->element_bounds_proven&&v->element_length_hoisted&&v->element_base_hoisted)el=v;
        else if(v->opcode==TURBOJS_SSA_RETURN)ret=v;
    }
    if(!el||!ret||el->left>=g->value_count||el->right>=g->value_count||ret->left>=g->value_count)return 0;
    *object_arg=&g->values[el->left];index_phi=&g->values[el->right];sum_phi=&g->values[ret->left];
    if((*object_arg)->opcode!=TURBOJS_SSA_ARGUMENT||index_phi->opcode!=TURBOJS_SSA_PHI||sum_phi->opcode!=TURBOJS_SSA_PHI||sum_phi->type!=TURBOJS_SSA_TYPE_FLOAT64)return 0;
    if(index_phi->left>=g->value_count||index_phi->right>=g->value_count||sum_phi->left>=g->value_count||sum_phi->right>=g->value_count)return 0;
    if(g->values[index_phi->left].opcode!=TURBOJS_SSA_CONSTANT_I64||g->values[index_phi->left].immediate!=0||g->values[sum_phi->left].opcode!=TURBOJS_SSA_CONSTANT_I64||g->values[sum_phi->left].immediate!=0)return 0;
    sum_add=&g->values[sum_phi->right];
    if(sum_add->opcode!=TURBOJS_SSA_ADD_I64||sum_add->type!=TURBOJS_SSA_TYPE_FLOAT64||!((sum_add->left==sum_phi->id&&sum_add->right==el->id)||(sum_add->right==sum_phi->id&&sum_add->left==el->id)))return 0;
    for(i=0;i<g->value_count;i++){const TurboJSSSAValue*v=&g->values[i];if(v->opcode==TURBOJS_SSA_LESS_THAN_I64&&v->left==index_phi->id&&v->right==el->element_length_value){cmp=v;break;}}
    if(!cmp||el->element_length_value>=g->value_count)return 0;
    *limit_arg=&g->values[el->element_length_value];if((*limit_arg)->opcode!=TURBOJS_SSA_ARGUMENT||(*limit_arg)->type!=TURBOJS_SSA_TYPE_INT32)return 0;
    {const TurboJSSSAValue*step=&g->values[index_phi->right],*one;if(step->opcode!=TURBOJS_SSA_ADD_I64)return 0;if(step->left==index_phi->id)one=&g->values[step->right];else if(step->right==index_phi->id)one=&g->values[step->left];else return 0;if(one->opcode!=TURBOJS_SSA_CONSTANT_I64||one->immediate!=1)return 0;}
    *element=el;return 1;
}


static int region_find_element_transform_loop_f64(const TurboJSSSAGraph *g,
                                                   const TurboJSSSAValue **src_arg,
                                                   const TurboJSSSAValue **dst_arg,
                                                   const TurboJSSSAValue **limit_arg,
                                                   const TurboJSSSAValue **scale_arg,
                                                   const TurboJSSSAValue **bias_arg,
                                                   uint8_t *transform_kind,
                                                   const TurboJSSSAValue **load,
                                                   const TurboJSSSAValue **store)
{
    const TurboJSSSAValue *ld=NULL,*st=NULL,*expr=NULL,*mul=NULL,*add=NULL,*index_phi=NULL,*cmp=NULL;
    size_t i;
    if(!g||g->block_count<3||!transform_kind)return 0;
    for(i=0;i<g->value_count;i++){
        const TurboJSSSAValue*v=&g->values[i];
        if(v->opcode==TURBOJS_SSA_ELEMENT_LOAD&&v->type==TURBOJS_SSA_TYPE_FLOAT64&&v->element_kind==TURBOJS_ELEMENT_KIND_TYPED_F64)ld=v;
        else if(v->opcode==TURBOJS_SSA_ELEMENT_STORE&&v->element_kind==TURBOJS_ELEMENT_KIND_TYPED_F64)st=v;
    }
    if(!ld||!st||ld->left>=g->value_count||ld->right>=g->value_count||st->left>=g->value_count||st->right!=ld->right||st->metadata>=g->value_count)return 0;
    index_phi=&g->values[ld->right];expr=&g->values[st->metadata];
    if(index_phi->opcode!=TURBOJS_SSA_PHI||expr->type!=TURBOJS_SSA_TYPE_FLOAT64)return 0;
    *src_arg=&g->values[ld->left];*dst_arg=&g->values[st->left];
    if((*src_arg)->opcode!=TURBOJS_SSA_ARGUMENT||(*dst_arg)->opcode!=TURBOJS_SSA_ARGUMENT)return 0;
    *scale_arg=NULL;*bias_arg=NULL;*transform_kind=0;
    if(expr->opcode==TURBOJS_SSA_ADD_I64){
        add=expr;
        if(add->left>=g->value_count||add->right>=g->value_count)return 0;
        if(g->values[add->left].opcode==TURBOJS_SSA_MUL_I64)mul=&g->values[add->left];
        else if(g->values[add->right].opcode==TURBOJS_SSA_MUL_I64)mul=&g->values[add->right];
        if(mul){
            if(mul->type!=TURBOJS_SSA_TYPE_FLOAT64||mul->left>=g->value_count||mul->right>=g->value_count)return 0;
            *scale_arg=(mul->left==ld->id)?&g->values[mul->right]:(mul->right==ld->id?&g->values[mul->left]:NULL);
            *bias_arg=(add->left==mul->id)?&g->values[add->right]:(add->right==mul->id?&g->values[add->left]:NULL);
            if(!*scale_arg||!*bias_arg)return 0;
        }else{
            *bias_arg=(add->left==ld->id)?&g->values[add->right]:(add->right==ld->id?&g->values[add->left]:NULL);
            if(!*bias_arg)return 0;
            *transform_kind=2;
        }
    }else if(expr->opcode==TURBOJS_SSA_MUL_I64){
        mul=expr;
        if(mul->left>=g->value_count||mul->right>=g->value_count)return 0;
        *scale_arg=(mul->left==ld->id)?&g->values[mul->right]:(mul->right==ld->id?&g->values[mul->left]:NULL);
        if(!*scale_arg)return 0;
        *transform_kind=1;
    }else if(expr->opcode==TURBOJS_SSA_SUB_I64){
        if(expr->left!=ld->id||expr->right>=g->value_count)return 0;
        *bias_arg=&g->values[expr->right];
        *transform_kind=3;
    }else if(expr->opcode==TURBOJS_SSA_MIN_F64||expr->opcode==TURBOJS_SSA_MAX_F64){
        const TurboJSSSAValue *inner=NULL,*bound=NULL;
        if(expr->left>=g->value_count||expr->right>=g->value_count)return 0;
        if(expr->left==ld->id)bound=&g->values[expr->right];
        else if(expr->right==ld->id)bound=&g->values[expr->left];
        else {
            inner=&g->values[expr->left];
            if(expr->opcode!=TURBOJS_SSA_MIN_F64||inner->opcode!=TURBOJS_SSA_MAX_F64||inner->left!=ld->id||inner->right>=g->value_count)return 0;
            *scale_arg=&g->values[inner->right];
            *bias_arg=&g->values[expr->right];
            *transform_kind=8;
        }
        if(bound){ if(expr->opcode==TURBOJS_SSA_MIN_F64){*bias_arg=bound;*transform_kind=6;}else{*scale_arg=bound;*transform_kind=7;} }
    }else return 0;
    if((*scale_arg&&((*scale_arg)->opcode!=TURBOJS_SSA_ARGUMENT||(*scale_arg)->type!=TURBOJS_SSA_TYPE_FLOAT64))||
       (*bias_arg&&((*bias_arg)->opcode!=TURBOJS_SSA_ARGUMENT||(*bias_arg)->type!=TURBOJS_SSA_TYPE_FLOAT64)))return 0;
    if(ld->element_length_value>=g->value_count)return 0;
    *limit_arg=&g->values[ld->element_length_value];
    if((*limit_arg)->opcode!=TURBOJS_SSA_ARGUMENT||(*limit_arg)->type!=TURBOJS_SSA_TYPE_INT32)return 0;
    for(i=0;i<g->value_count;i++){const TurboJSSSAValue*v=&g->values[i];if(v->opcode==TURBOJS_SSA_LESS_THAN_I64&&v->left==index_phi->id&&v->right==(*limit_arg)->id){cmp=v;break;}}
    if(!cmp||index_phi->left>=g->value_count||index_phi->right>=g->value_count||g->values[index_phi->left].opcode!=TURBOJS_SSA_CONSTANT_I64||g->values[index_phi->left].immediate!=0)return 0;
    {const TurboJSSSAValue*step=&g->values[index_phi->right],*one;if(step->opcode!=TURBOJS_SSA_ADD_I64)return 0;if(step->left==index_phi->id)one=&g->values[step->right];else if(step->right==index_phi->id)one=&g->values[step->left];else return 0;if(one->opcode!=TURBOJS_SSA_CONSTANT_I64||one->immediate!=1)return 0;}
    *load=ld;*store=st;return 1;
}

static int region_find_element_binary_loop_f64(const TurboJSSSAGraph *g,
                                                const TurboJSSSAValue **left_arg,
                                                const TurboJSSSAValue **right_arg,
                                                const TurboJSSSAValue **dst_arg,
                                                const TurboJSSSAValue **limit_arg,
                                                uint8_t *transform_kind,
                                                const TurboJSSSAValue **left_load,
                                                const TurboJSSSAValue **right_load,
                                                const TurboJSSSAValue **store)
{
    const TurboJSSSAValue *loads[2]={NULL,NULL},*st=NULL,*expr=NULL,*index_phi=NULL,*cmp=NULL;
    size_t i;unsigned count=0;
    if(!g||g->block_count<3||!transform_kind)return 0;
    for(i=0;i<g->value_count;i++){
        const TurboJSSSAValue*v=&g->values[i];
        if(v->opcode==TURBOJS_SSA_ELEMENT_LOAD&&v->type==TURBOJS_SSA_TYPE_FLOAT64&&v->element_kind==TURBOJS_ELEMENT_KIND_TYPED_F64){if(count<2)loads[count]=v;count++;}
        else if(v->opcode==TURBOJS_SSA_ELEMENT_STORE&&v->element_kind==TURBOJS_ELEMENT_KIND_TYPED_F64)st=v;
    }
    if(count!=2||!loads[0]||!loads[1]||!st||loads[0]->right!=loads[1]->right||st->right!=loads[0]->right||st->metadata>=g->value_count)return 0;
    expr=&g->values[st->metadata];index_phi=&g->values[loads[0]->right];
    if(index_phi->opcode!=TURBOJS_SSA_PHI||expr->type!=TURBOJS_SSA_TYPE_FLOAT64||
       !((expr->opcode==TURBOJS_SSA_ADD_I64)||(expr->opcode==TURBOJS_SSA_SUB_I64))||
       expr->left!=loads[0]->id||expr->right!=loads[1]->id)return 0;
    *left_arg=&g->values[loads[0]->left];*right_arg=&g->values[loads[1]->left];*dst_arg=&g->values[st->left];
    if((*left_arg)->opcode!=TURBOJS_SSA_ARGUMENT||(*right_arg)->opcode!=TURBOJS_SSA_ARGUMENT||(*dst_arg)->opcode!=TURBOJS_SSA_ARGUMENT)return 0;
    if(loads[0]->element_length_value>=g->value_count||loads[1]->element_length_value!=loads[0]->element_length_value||st->element_length_value!=loads[0]->element_length_value)return 0;
    *limit_arg=&g->values[loads[0]->element_length_value];
    if((*limit_arg)->opcode!=TURBOJS_SSA_ARGUMENT||(*limit_arg)->type!=TURBOJS_SSA_TYPE_INT32)return 0;
    for(i=0;i<g->value_count;i++){const TurboJSSSAValue*v=&g->values[i];if(v->opcode==TURBOJS_SSA_LESS_THAN_I64&&v->left==index_phi->id&&v->right==(*limit_arg)->id){cmp=v;break;}}
    if(!cmp||index_phi->left>=g->value_count||index_phi->right>=g->value_count||g->values[index_phi->left].opcode!=TURBOJS_SSA_CONSTANT_I64||g->values[index_phi->left].immediate!=0)return 0;
    {const TurboJSSSAValue*step=&g->values[index_phi->right],*one;if(step->opcode!=TURBOJS_SSA_ADD_I64)return 0;if(step->left==index_phi->id)one=&g->values[step->right];else if(step->right==index_phi->id)one=&g->values[step->left];else return 0;if(one->opcode!=TURBOJS_SSA_CONSTANT_I64||one->immediate!=1)return 0;}
    *transform_kind=expr->opcode==TURBOJS_SSA_SUB_I64?5:4;*left_load=loads[0];*right_load=loads[1];*store=st;return 1;
}

static int region_configure_simd_f64(TurboJSRegionNativeFunction *f,
                                     const TurboJSRegionElementLayout *layout)
{
    const TurboJSSSAValue *src=NULL,*dst=NULL,*limit=NULL,*scale=NULL,*bias=NULL,*load=NULL,*store=NULL;
    uint8_t transform_kind=0;
    const TurboJSSSAValue *object=NULL,*element=NULL;
    TurboJSX64SIMDLevel level;
    const TurboJSSSAValue *right=NULL,*right_load=NULL;
    if(!f||!layout||layout->element_stride!=sizeof(double))return 0;
    level=turbojs_x64_simd_level();
    if(level<TURBOJS_X64_SIMD_AVX2)return 0;
    if(region_find_element_binary_loop_f64(&f->value_graph,&src,&right,&dst,&limit,&transform_kind,&load,&right_load,&store)){
        if(src->immediate>UINT8_MAX||right->immediate>UINT8_MAX||dst->immediate>UINT8_MAX||limit->immediate>UINT8_MAX)return 0;
        f->simd_kernel=3;f->simd_level=(uint8_t)level;f->simd_source_arg=(uint8_t)src->immediate;f->simd_right_arg=(uint8_t)right->immediate;
        f->simd_destination_arg=(uint8_t)dst->immediate;f->simd_limit_arg=(uint8_t)limit->immediate;f->simd_transform_kind=transform_kind;
        f->simd_source_generation=load->element_generation;f->simd_right_generation=right_load->element_generation;f->simd_destination_generation=store->element_generation;
        f->element_layout=*layout;f->has_element_layout=1;f->inline_element_loop=1;f->inline_element_loop_length_hoist=1;f->inline_element_loop_base_hoist=1;
        f->inline_element_loop_bounds_eliminated=1;f->inline_element_loop_unroll_factor=8;f->inline_element_loop_accumulators=0;f->inline_element_float64_loop=1;f->inline_element_transform_loop=1;return 1;
    }
    if(region_find_element_transform_loop_f64(&f->value_graph,&src,&dst,&limit,&scale,&bias,&transform_kind,&load,&store)){
        if(src->immediate>UINT8_MAX||dst->immediate>UINT8_MAX||limit->immediate>UINT8_MAX||
           (scale&&scale->immediate>UINT8_MAX)||(bias&&bias->immediate>UINT8_MAX))return 0;
        f->simd_kernel=(transform_kind>=6)?4:2;
        level=(transform_kind>=6)?turbojs_x64_simd_level():turbojs_x64_f64_transform_level(
            (load->element_flags & TURBOJS_ELEMENT_FLAG_FAST_MATH)!=0 &&
            (store->element_flags & TURBOJS_ELEMENT_FLAG_FAST_MATH)!=0);
        f->simd_level=(uint8_t)level;
        f->simd_source_arg=(uint8_t)src->immediate;f->simd_destination_arg=(uint8_t)dst->immediate;
        f->simd_limit_arg=(uint8_t)limit->immediate;f->simd_scale_arg=scale?(uint8_t)scale->immediate:UINT8_MAX;
        f->simd_bias_arg=bias?(uint8_t)bias->immediate:UINT8_MAX;f->simd_transform_kind=transform_kind;f->simd_source_generation=load->element_generation;
        f->simd_destination_generation=store->element_generation;f->element_layout=*layout;f->has_element_layout=1;
        f->inline_element_loop=1;f->inline_element_loop_length_hoist=1;f->inline_element_loop_base_hoist=1;
        f->inline_element_loop_bounds_eliminated=1;f->inline_element_loop_unroll_factor=8;
        f->inline_element_loop_accumulators=0;f->inline_element_float64_loop=1;f->inline_element_transform_loop=1;
        return 1;
    }
    if(region_find_element_sum_loop_f64(&f->value_graph,&object,&limit,&element)){
        if(object->immediate>UINT8_MAX||limit->immediate>UINT8_MAX)return 0;
        f->simd_kernel=1;f->simd_level=(uint8_t)level;f->simd_source_arg=(uint8_t)object->immediate;
        f->simd_limit_arg=(uint8_t)limit->immediate;f->simd_source_generation=element->element_generation;
        f->element_layout=*layout;f->has_element_layout=1;f->inline_element_loop=1;
        f->inline_element_loop_length_hoist=1;f->inline_element_loop_base_hoist=1;
        f->inline_element_loop_bounds_eliminated=1;f->inline_element_loop_unroll_factor=8;
        f->inline_element_loop_accumulators=2;f->inline_element_float64_loop=1;
        return 1;
    }
    return 0;
}

static int region_build_inline_element_transform_f64(TurboJSRegionNativeFunction *f,
                                                       const TurboJSRegionElementLayout *layout)
{
    const TurboJSSSAValue *src_arg=NULL,*dst_arg=NULL,*limit_arg=NULL,*scale_arg=NULL,*bias_arg=NULL,*ld=NULL,*st=NULL;
    uint8_t transform_kind=0;
    Buf b={0};size_t bail_patches[24],bail_count=0,tail_patch,exit_patch,loop_start,bail,exit_label,i;
    if(!f||!layout||layout->element_stride<8||!region_find_element_transform_loop_f64(&f->value_graph,&src_arg,&dst_arg,&limit_arg,&scale_arg,&bias_arg,&transform_kind,&ld,&st)||transform_kind!=0)return 0;
    if(layout->kind_offset>INT32_MAX||layout->generation_offset>INT32_MAX||layout->length_offset>INT32_MAX||layout->element_storage_offset>INT32_MAX||layout->element_stride>INT32_MAX||layout->element_value_offset>INT32_MAX)return 0;
    if(!e8(&b,0x55)||!e8(&b,0x48)||!e8(&b,0x89)||!e8(&b,0xE5)||!e8(&b,0x48)||!e8(&b,0x83)||!e8(&b,0xEC)||!e8(&b,0x40)||!stores(&b,0,7)||!stores(&b,1,6)||!stores(&b,2,2)||!stores(&b,3,1)||!stores(&b,4,8))goto fail;
    if(!loads(&b,6,1)||!binri32(&b,6,7,5)||!emit_jcc32(&b,0x82,&bail_patches[bail_count++])||!loads(&b,7,0))goto fail;
    /* Decode and validate source object into stack slot 5. */
    if(!loadm(&b,0,7,(int32_t)(src_arg->immediate*8)))goto fail;
    if(layout->object_tag_mask&&!emit_mask_compare_u64(&b,0,layout->object_tag_mask,layout->object_tag_value,bail_patches,&bail_count))goto fail;
    if(layout->object_pointer_mask!=UINT64_MAX){if(!movi(&b,11,layout->object_pointer_mask)||!binrr(&b,0x21,0,11))goto fail;}
    if(!stores(&b,5,0)||!loadm(&b,1,0,(int32_t)layout->kind_offset)||!emit_mask_compare_u64(&b,1,UINT16_MAX,TURBOJS_ELEMENT_KIND_TYPED_F64,bail_patches,&bail_count)||!loadm(&b,1,0,(int32_t)layout->generation_offset)||!emit_mask_compare_u64(&b,1,UINT32_MAX,ld->element_generation,bail_patches,&bail_count))goto fail;
    /* Decode and validate destination object into stack slot 6. */
    if(!loadm(&b,8,7,(int32_t)(dst_arg->immediate*8)))goto fail;
    if(layout->object_tag_mask&&!emit_mask_compare_u64(&b,8,layout->object_tag_mask,layout->object_tag_value,bail_patches,&bail_count))goto fail;
    if(layout->object_pointer_mask!=UINT64_MAX){if(!movi(&b,11,layout->object_pointer_mask)||!binrr(&b,0x21,8,11))goto fail;}
    if(!stores(&b,6,8)||!loadm(&b,1,8,(int32_t)layout->kind_offset)||!emit_mask_compare_u64(&b,1,UINT16_MAX,TURBOJS_ELEMENT_KIND_TYPED_F64,bail_patches,&bail_count)||!loadm(&b,1,8,(int32_t)layout->generation_offset)||!emit_mask_compare_u64(&b,1,UINT32_MAX,st->element_generation,bail_patches,&bail_count))goto fail;
    /* Load and validate limit against both arrays. */
    if(!loadm(&b,9,7,(int32_t)(limit_arg->immediate*8))||!rex(&b,1,9,0,9)||!e8(&b,0x85)||!e8(&b,0xC9)||!emit_jcc32(&b,0x88,&bail_patches[bail_count++])||!movi(&b,11,UINT32_MAX)||!binrr(&b,0x39,9,11)||!emit_jcc32(&b,0x87,&bail_patches[bail_count++]))goto fail;
    if(!loads(&b,0,5)||!loadm(&b,1,0,(int32_t)layout->length_offset)||!movi(&b,11,UINT32_MAX)||!binrr(&b,0x21,1,11)||!binrr(&b,0x39,1,9)||!emit_jcc32(&b,0x82,&bail_patches[bail_count++]))goto fail;
    if(!loads(&b,8,6)||!loadm(&b,1,8,(int32_t)layout->length_offset)||!movi(&b,11,UINT32_MAX)||!binrr(&b,0x21,1,11)||!binrr(&b,0x39,1,9)||!emit_jcc32(&b,0x82,&bail_patches[bail_count++]))goto fail;
    /* Hoist source/destination backing pointers. */
    if(!loads(&b,0,5))goto fail;
    if(layout->element_storage_indirect){if(!loadm(&b,0,0,(int32_t)layout->element_storage_offset))goto fail;}else if(layout->element_storage_offset&&!binri32(&b,0,0,(int32_t)layout->element_storage_offset))goto fail;
    if(!loads(&b,8,6))goto fail;
    if(layout->element_storage_indirect){if(!loadm(&b,8,8,(int32_t)layout->element_storage_offset))goto fail;}else if(layout->element_storage_offset&&!binri32(&b,8,0,(int32_t)layout->element_storage_offset))goto fail;
    /* Load scale and bias payloads into xmm3/xmm4. */
    if(!loadm(&b,11,7,(int32_t)(scale_arg->immediate*8))||!movq_xmm_gpr(&b,3,11)||!loadm(&b,11,7,(int32_t)(bias_arg->immediate*8))||!movq_xmm_gpr(&b,4,11))goto fail;
    if(!e8(&b,0x45)||!e8(&b,0x31)||!e8(&b,0xD2))goto fail;
    loop_start=b.n;if(!movrr(&b,2,10)||!binri32(&b,2,0,2)||!binrr(&b,0x39,2,9)||!emit_jcc32(&b,0x87,&tail_patch))goto fail;
    /* Two transformed elements per iteration. */
    if(!movrr(&b,2,10)||(layout->element_stride!=1&&!imulri32(&b,2,(int32_t)layout->element_stride))||!movrr(&b,1,2)||!binrr(&b,0x01,2,0)||!binrr(&b,0x01,1,8)||!movsd_load(&b,2,2,(int32_t)layout->element_value_offset)||!mulsd(&b,2,3)||!addsd(&b,2,4)||!movsd_store(&b,1,(int32_t)layout->element_value_offset,2)||!movsd_load(&b,2,2,(int32_t)(layout->element_value_offset+layout->element_stride))||!mulsd(&b,2,3)||!addsd(&b,2,4)||!movsd_store(&b,1,(int32_t)(layout->element_value_offset+layout->element_stride),2)||!binri32(&b,10,0,2)||!e8(&b,0xE9))goto fail;
    {size_t off=b.n;if(!e32(&b,0))goto fail;p32(&b,off,(int32_t)(loop_start-(off+4)));}p32(&b,tail_patch,(int32_t)(b.n-(tail_patch+4)));
    {size_t tail_loop=b.n;if(!binrr(&b,0x39,10,9)||!emit_jcc32(&b,0x83,&exit_patch))goto fail;if(!movrr(&b,2,10)||(layout->element_stride!=1&&!imulri32(&b,2,(int32_t)layout->element_stride))||!movrr(&b,1,2)||!binrr(&b,0x01,2,0)||!binrr(&b,0x01,1,8)||!movsd_load(&b,2,2,(int32_t)layout->element_value_offset)||!mulsd(&b,2,3)||!addsd(&b,2,4)||!movsd_store(&b,1,(int32_t)layout->element_value_offset,2)||!binri32(&b,10,0,1)||!e8(&b,0xE9))goto fail;{size_t off=b.n;if(!e32(&b,0))goto fail;p32(&b,off,(int32_t)(tail_loop-(off+4)));}}
    exit_label=b.n;p32(&b,exit_patch,(int32_t)(exit_label-(exit_patch+4)));if(!loads(&b,8,4)||!storem(&b,8,0,9)||!e8(&b,0x31)||!e8(&b,0xC0)||!e8(&b,0xC9)||!e8(&b,0xC3))goto fail;
    bail=b.n;if(!e8(&b,0xB8)||!e32(&b,TURBOJS_IR_BAILOUT)||!e8(&b,0xC9)||!e8(&b,0xC3))goto fail;for(i=0;i<bail_count;i++)p32(&b,bail_patches[i],(int32_t)(bail-(bail_patches[i]+4)));
    f->allocation_size=b.n;f->code=turbojs_executable_memory_allocate(b.n);if(!f->code)goto fail;memcpy(f->code,b.p,b.n);if(!turbojs_executable_memory_seal(f->code,b.n))goto fail;f->code_size=b.n;f->element_layout=*layout;f->has_element_layout=1;f->inline_element_loop=1;f->inline_element_loop_length_hoist=1;f->inline_element_loop_base_hoist=1;f->inline_element_loop_bounds_eliminated=1;f->inline_element_loop_unroll_factor=2;f->inline_element_loop_accumulators=0;f->inline_element_float64_loop=1;f->inline_element_transform_loop=1;free(b.p);return 1;
fail:if(f&&f->code){turbojs_executable_memory_free(f->code,f->allocation_size);f->code=NULL;}free(b.p);return 0;
}

static int region_build_inline_element_loop_f64(TurboJSRegionNativeFunction *f,
                                                 const TurboJSRegionElementLayout *layout)
{
    const TurboJSSSAValue*object_arg=NULL,*limit_arg=NULL,*element=NULL;
    Buf b={0};size_t bail_patches[16],bail_count=0,tail_patch,exit_patch,loop_start,bail,exit_label,i;
    if(!f||!layout||layout->element_stride<8||!region_find_element_sum_loop_f64(&f->value_graph,&object_arg,&limit_arg,&element))return 0;
    if(layout->kind_offset>INT32_MAX||layout->generation_offset>INT32_MAX||layout->length_offset>INT32_MAX||layout->element_storage_offset>INT32_MAX||layout->element_stride>INT32_MAX||layout->element_value_offset>INT32_MAX)return 0;
    if(!e8(&b,0x55)||!e8(&b,0x48)||!e8(&b,0x89)||!e8(&b,0xE5)||!e8(&b,0x48)||!e8(&b,0x83)||!e8(&b,0xEC)||!e8(&b,0x30)||!stores(&b,0,7)||!stores(&b,1,6)||!stores(&b,2,2)||!stores(&b,3,1)||!stores(&b,4,8))goto fail;
    if(!loads(&b,6,1)||!binri32(&b,6,7,(int32_t)(limit_arg->immediate+1))||!emit_jcc32(&b,0x82,&bail_patches[bail_count++])||!loads(&b,7,0)||!loadm(&b,0,7,(int32_t)(object_arg->immediate*8)))goto fail;
    if(layout->object_tag_mask&&!emit_mask_compare_u64(&b,0,layout->object_tag_mask,layout->object_tag_value,bail_patches,&bail_count))goto fail;
    if(layout->object_pointer_mask!=UINT64_MAX){if(!movi(&b,11,layout->object_pointer_mask)||!binrr(&b,0x21,0,11))goto fail;}
    if(!stores(&b,5,0)||!loadm(&b,0,0,(int32_t)layout->kind_offset)||!emit_mask_compare_u64(&b,0,UINT16_MAX,TURBOJS_ELEMENT_KIND_TYPED_F64,bail_patches,&bail_count))goto fail;
    if(!loads(&b,0,5)||!loadm(&b,0,0,(int32_t)layout->generation_offset)||!emit_mask_compare_u64(&b,0,UINT32_MAX,element->element_generation,bail_patches,&bail_count))goto fail;
    if(!loads(&b,7,0)||!loadm(&b,9,7,(int32_t)(limit_arg->immediate*8))||!rex(&b,1,9,0,9)||!e8(&b,0x85)||!e8(&b,0xC9)||!emit_jcc32(&b,0x88,&bail_patches[bail_count++])||!movi(&b,11,UINT32_MAX)||!binrr(&b,0x39,9,11)||!emit_jcc32(&b,0x87,&bail_patches[bail_count++]))goto fail;
    if(!loads(&b,0,5)||!loadm(&b,1,0,(int32_t)layout->length_offset)||!movi(&b,11,UINT32_MAX)||!binrr(&b,0x21,1,11)||!binrr(&b,0x39,1,9)||!emit_jcc32(&b,0x82,&bail_patches[bail_count++]))goto fail;
    if(!loads(&b,0,5))goto fail;
    if(layout->element_storage_indirect){if(!loadm(&b,0,0,(int32_t)layout->element_storage_offset))goto fail;}else if(layout->element_storage_offset&&!binri32(&b,0,0,(int32_t)layout->element_storage_offset))goto fail;
    if(layout->element_value_offset+layout->element_stride>INT32_MAX)goto fail;
    if(!e8(&b,0x45)||!e8(&b,0x31)||!e8(&b,0xD2)||!xorpd(&b,0,0)||!xorpd(&b,1,1))goto fail;
    loop_start=b.n;if(!movrr(&b,2,10)||!binri32(&b,2,0,2)||!binrr(&b,0x39,2,9)||!emit_jcc32(&b,0x87,&tail_patch))goto fail;
    if(!movrr(&b,2,10)||(layout->element_stride!=1&&!imulri32(&b,2,(int32_t)layout->element_stride))||!binrr(&b,0x01,2,0)||!movsd_load(&b,2,2,(int32_t)layout->element_value_offset)||!addsd(&b,0,2)||!movsd_load(&b,2,2,(int32_t)(layout->element_value_offset+layout->element_stride))||!addsd(&b,1,2)||!binri32(&b,10,0,2)||!e8(&b,0xE9))goto fail;
    {size_t off=b.n;if(!e32(&b,0))goto fail;p32(&b,off,(int32_t)(loop_start-(off+4)));}p32(&b,tail_patch,(int32_t)(b.n-(tail_patch+4)));
    {size_t tail_loop=b.n;if(!binrr(&b,0x39,10,9)||!emit_jcc32(&b,0x83,&exit_patch))goto fail;if(!movrr(&b,2,10)||(layout->element_stride!=1&&!imulri32(&b,2,(int32_t)layout->element_stride))||!binrr(&b,0x01,2,0)||!movsd_load(&b,2,2,(int32_t)layout->element_value_offset)||!addsd(&b,0,2)||!binri32(&b,10,0,1)||!e8(&b,0xE9))goto fail;{size_t off=b.n;if(!e32(&b,0))goto fail;p32(&b,off,(int32_t)(tail_loop-(off+4)));}}
    exit_label=b.n;p32(&b,exit_patch,(int32_t)(exit_label-(exit_patch+4)));if(!addsd(&b,0,1)||!loads(&b,8,4)||!movsd_store(&b,8,0,0)||!e8(&b,0x31)||!e8(&b,0xC0)||!e8(&b,0xC9)||!e8(&b,0xC3))goto fail;
    bail=b.n;if(!e8(&b,0xB8)||!e32(&b,TURBOJS_IR_BAILOUT)||!e8(&b,0xC9)||!e8(&b,0xC3))goto fail;for(i=0;i<bail_count;i++)p32(&b,bail_patches[i],(int32_t)(bail-(bail_patches[i]+4)));
    f->allocation_size=b.n;f->code=turbojs_executable_memory_allocate(b.n);if(!f->code)goto fail;memcpy(f->code,b.p,b.n);if(!turbojs_executable_memory_seal(f->code,b.n))goto fail;f->code_size=b.n;f->element_layout=*layout;f->has_element_layout=1;f->inline_element_loop=1;f->inline_element_loop_length_hoist=1;f->inline_element_loop_base_hoist=1;f->inline_element_loop_bounds_eliminated=1;f->inline_element_loop_unroll_factor=2;f->inline_element_loop_accumulators=2;f->inline_element_float64_loop=1;free(b.p);return 1;
fail:if(f&&f->code){turbojs_executable_memory_free(f->code,f->allocation_size);f->code=NULL;}free(b.p);return 0;
}

static int region_build_inline_element_loop(TurboJSRegionNativeFunction *f,
                                            const TurboJSRegionElementLayout *layout)
{
    const TurboJSSSAValue *object_arg=NULL,*limit_arg=NULL,*element=NULL;
    Buf b={0}; size_t bail_patches[16],bail_count=0,exit_patch,loop_start,bail,exit_label,i;
    if(!f||!layout||!layout->element_stride) return 0;
    if(!region_find_element_sum_loop_i64(&f->value_graph,&object_arg,&limit_arg,&element)) return 0;
    if(layout->kind_offset>INT32_MAX||layout->generation_offset>INT32_MAX||
       layout->length_offset>INT32_MAX||layout->element_storage_offset>INT32_MAX||
       layout->element_stride>INT32_MAX||layout->element_value_offset>INT32_MAX) return 0;
    if(!e8(&b,0x55)||!e8(&b,0x48)||!e8(&b,0x89)||!e8(&b,0xE5)||
       !e8(&b,0x48)||!e8(&b,0x83)||!e8(&b,0xEC)||!e8(&b,0x30)||
       !stores(&b,0,7)||!stores(&b,1,6)||!stores(&b,2,2)||!stores(&b,3,1)||!stores(&b,4,8)) goto fail;
    if(!loads(&b,6,1)||!binri32(&b,6,7,(int32_t)(limit_arg->immediate+1))||
       !emit_jcc32(&b,0x82,&bail_patches[bail_count++])||!loads(&b,7,0)||
       !loadm(&b,0,7,(int32_t)(object_arg->immediate*8))) goto fail;
    if(layout->object_tag_mask&&
       !emit_mask_compare_u64(&b,0,layout->object_tag_mask,layout->object_tag_value,bail_patches,&bail_count)) goto fail;
    if(layout->object_pointer_mask!=UINT64_MAX){
        if(!movi(&b,11,layout->object_pointer_mask)||!binrr(&b,0x21,0,11)) goto fail;
    }
    if(!stores(&b,5,0)) goto fail;
    if(!loadm(&b,0,0,(int32_t)layout->kind_offset)||
       !emit_mask_compare_u64(&b,0,UINT16_MAX,element->element_kind,bail_patches,&bail_count)) goto fail;
    if(!loads(&b,0,5)||!loadm(&b,0,0,(int32_t)layout->generation_offset)||
       !emit_mask_compare_u64(&b,0,UINT32_MAX,element->element_generation,bail_patches,&bail_count)) goto fail;
    if(!loads(&b,7,0)||!loadm(&b,9,7,(int32_t)(limit_arg->immediate*8))||
       !rex(&b,1,9,0,9)||!e8(&b,0x85)||!e8(&b,0xC9)||
       !emit_jcc32(&b,0x88,&bail_patches[bail_count++])||
       !movi(&b,11,UINT32_MAX)||!binrr(&b,0x39,9,11)||
       !emit_jcc32(&b,0x87,&bail_patches[bail_count++])) goto fail;
    if(!loads(&b,0,5)||!loadm(&b,1,0,(int32_t)layout->length_offset)||
       !movi(&b,11,UINT32_MAX)||!binrr(&b,0x21,1,11)||
       !binrr(&b,0x39,1,9)||!emit_jcc32(&b,0x82,&bail_patches[bail_count++])) goto fail;
    if(!loads(&b,0,5)) goto fail;
    if(layout->element_storage_indirect){ if(!loadm(&b,0,0,(int32_t)layout->element_storage_offset)) goto fail; }
    else if(layout->element_storage_offset&&!binri32(&b,0,0,(int32_t)layout->element_storage_offset)) goto fail;
    /* r10=index, r11=accumulator 0, r8=accumulator 1.  Process four
       elements per iteration, alternating accumulators to shorten the
       dependency chain, then finish with a scalar tail. */
    if(layout->element_value_offset + layout->element_stride * 3u > INT32_MAX) goto fail;
    if(!e8(&b,0x45)||!e8(&b,0x31)||!e8(&b,0xD2)||
       !e8(&b,0x45)||!e8(&b,0x31)||!e8(&b,0xDB)||
       !e8(&b,0x45)||!e8(&b,0x31)||!e8(&b,0xC0)) goto fail;
    loop_start=b.n;
    /* If index + 4 > limit, enter the scalar tail. */
    if(!movrr(&b,2,10)||!binri32(&b,2,0,4)||!binrr(&b,0x39,2,9)) goto fail;
    {
        size_t tail_patch;
        if(!emit_jcc32(&b,0x87,&tail_patch)) goto fail;
        /* Address of element[index]. */
        if(!movrr(&b,2,10)|| (layout->element_stride!=1&&!imulri32(&b,2,(int32_t)layout->element_stride))||
           !binrr(&b,0x01,2,0)) goto fail;
        if(!loadm(&b,1,2,(int32_t)layout->element_value_offset)||!binrr(&b,0x01,11,1)||
           !loadm(&b,1,2,(int32_t)(layout->element_value_offset+layout->element_stride))||!binrr(&b,0x01,8,1)||
           !loadm(&b,1,2,(int32_t)(layout->element_value_offset+layout->element_stride*2u))||!binrr(&b,0x01,11,1)||
           !loadm(&b,1,2,(int32_t)(layout->element_value_offset+layout->element_stride*3u))||!binrr(&b,0x01,8,1)||
           !binri32(&b,10,0,4)||!e8(&b,0xE9)) goto fail;
        {
            size_t off=b.n; if(!e32(&b,0)) goto fail;
            p32(&b,off,(int32_t)(loop_start-(off+4)));
        }
        p32(&b,tail_patch,(int32_t)(b.n-(tail_patch+4)));
    }
    /* Scalar tail for limits not divisible by four. */
    {
        size_t tail_loop=b.n;
        if(!binrr(&b,0x39,10,9)||!emit_jcc32(&b,0x83,&exit_patch)) goto fail;
        if(!movrr(&b,2,10)|| (layout->element_stride!=1&&!imulri32(&b,2,(int32_t)layout->element_stride))||
           !binrr(&b,0x01,2,0)||!loadm(&b,2,2,(int32_t)layout->element_value_offset)||
           !binrr(&b,0x01,11,2)||!binri32(&b,10,0,1)||!e8(&b,0xE9)) goto fail;
        {
            size_t off=b.n; if(!e32(&b,0)) goto fail;
            p32(&b,off,(int32_t)(tail_loop-(off+4)));
        }
    }
    exit_label=b.n; p32(&b,exit_patch,(int32_t)(exit_label-(exit_patch+4)));
    if(!binrr(&b,0x01,11,8)||!loads(&b,8,4)||!storem(&b,8,0,11)||!e8(&b,0x31)||!e8(&b,0xC0)||!e8(&b,0xC9)||!e8(&b,0xC3)) goto fail;
    bail=b.n;if(!e8(&b,0xB8)||!e32(&b,TURBOJS_IR_BAILOUT)||!e8(&b,0xC9)||!e8(&b,0xC3)) goto fail;
    for(i=0;i<bail_count;i++) p32(&b,bail_patches[i],(int32_t)(bail-(bail_patches[i]+4)));
    f->allocation_size=b.n;f->code=turbojs_executable_memory_allocate(b.n);if(!f->code)goto fail;
    memcpy(f->code,b.p,b.n);if(!turbojs_executable_memory_seal(f->code,b.n))goto fail;
    f->code_size=b.n;f->element_layout=*layout;f->has_element_layout=1;
    f->inline_element_loop=1;f->inline_element_loop_length_hoist=1;
    f->inline_element_loop_base_hoist=1;f->inline_element_loop_bounds_eliminated=1;
    f->inline_element_loop_unroll_factor=4;f->inline_element_loop_accumulators=2;
    free(b.p);return 1;
fail:
    if(f&&f->code){turbojs_executable_memory_free(f->code,f->allocation_size);f->code=NULL;}
    free(b.p);return 0;
}

static int region_build_inline_element(TurboJSRegionNativeFunction *f,
                                       const TurboJSRegionElementLayout *layout)
{
    const TurboJSSSAValue *object_arg=NULL,*index=NULL,*stored=NULL,*element=NULL;
    Buf b={0}; size_t bail_patches[16],bail_count=0,bail,i;
    uint64_t element_off=0; int is_store=0,dynamic_index=0,dynamic_store=0;
    if(!f||!layout||!layout->element_stride)return 0;
    if(region_find_direct_element_load(&f->value_graph,&object_arg,&index,&element))is_store=0;
    else if(region_find_direct_element_store(&f->value_graph,&object_arg,&index,&stored,&element))is_store=1;
    else return 0;
    if(layout->kind_offset>INT32_MAX||layout->generation_offset>INT32_MAX||
       layout->length_offset>INT32_MAX||layout->element_storage_offset>INT32_MAX)return 0;
    dynamic_index=(index->opcode==TURBOJS_SSA_ARGUMENT);
    dynamic_store=(is_store&&stored&&stored->opcode==TURBOJS_SSA_ARGUMENT);
    if(!dynamic_index){element_off=(uint64_t)(uint32_t)index->immediate*layout->element_stride+layout->element_value_offset;if(element_off>INT32_MAX)return 0;}
    else if(layout->element_stride>INT32_MAX||layout->element_value_offset>INT32_MAX)return 0;
    /* args, argc, ops, opaque, result, decoded object */
    if(!e8(&b,0x55)||!e8(&b,0x48)||!e8(&b,0x89)||!e8(&b,0xE5)||
       !e8(&b,0x48)||!e8(&b,0x83)||!e8(&b,0xEC)||!e8(&b,0x30)||
       !stores(&b,0,7)||!stores(&b,1,6)||!stores(&b,2,2)||!stores(&b,3,1)||!stores(&b,4,8))goto fail;
    if(!loads(&b,6,1)||!binri32(&b,6,7,(int32_t)(object_arg->immediate+1))||
       !emit_jcc32(&b,0x82,&bail_patches[bail_count++])||!loads(&b,7,0)||
       !loadm(&b,0,7,(int32_t)(object_arg->immediate*8)))goto fail;
    if(layout->object_tag_mask&&
       !emit_mask_compare_u64(&b,0,layout->object_tag_mask,layout->object_tag_value,bail_patches,&bail_count))goto fail;
    if(layout->object_pointer_mask!=UINT64_MAX){if(!movi(&b,11,layout->object_pointer_mask)||!binrr(&b,0x21,0,11))goto fail;}
    if(!stores(&b,5,0))goto fail;
    if(!loadm(&b,0,0,(int32_t)layout->kind_offset)||
       !emit_mask_compare_u64(&b,0,UINT16_MAX,element->element_kind,bail_patches,&bail_count))goto fail;
    if(!loads(&b,0,5)||!loadm(&b,0,0,(int32_t)layout->generation_offset)||
       !emit_mask_compare_u64(&b,0,UINT32_MAX,element->element_generation,bail_patches,&bail_count))goto fail;
    if(dynamic_index){
        /* Load dynamic Int32-compatible index from args[1]. */
        if(!loads(&b,7,0)||!loadm(&b,10,7,(int32_t)(index->immediate*8))||
           !rex(&b,1,10,0,10)||!e8(&b,0x85)||!e8(&b,0xD2)||
           !emit_jcc32(&b,0x88,&bail_patches[bail_count++])||
           !movi(&b,11,UINT32_MAX)||!binrr(&b,0x39,10,11)||
           !emit_jcc32(&b,0x87,&bail_patches[bail_count++]))goto fail;
    }
    if(!loads(&b,0,5)||!loadm(&b,0,0,(int32_t)layout->length_offset)||
       !movi(&b,11,UINT32_MAX)||!binrr(&b,0x21,0,11))goto fail;
    if(dynamic_index){
        if(!binrr(&b,0x39,0,10)||!emit_jcc32(&b,0x86,&bail_patches[bail_count++]))goto fail;
    }else if(!binri32(&b,0,7,(int32_t)((uint32_t)index->immediate+1))||
             !emit_jcc32(&b,0x82,&bail_patches[bail_count++]))goto fail;
    if(!loads(&b,0,5))goto fail;
    if(layout->element_storage_indirect){if(!loadm(&b,0,0,(int32_t)layout->element_storage_offset))goto fail;}
    else if(layout->element_storage_offset&&!binri32(&b,0,0,(int32_t)layout->element_storage_offset))goto fail;
    if(dynamic_index){if(layout->element_stride!=1&&!imulri32(&b,10,(int32_t)layout->element_stride))goto fail;if(!binrr(&b,0x01,0,10))goto fail;}
    if(is_store){
        if(dynamic_store){
            if(!loads(&b,7,0)||!loadm(&b,11,7,(int32_t)(stored->immediate*8)))goto fail;
        }else if(!movi(&b,11,(uint64_t)stored->immediate))goto fail;
        if(!storem(&b,0,(int32_t)(dynamic_index?layout->element_value_offset:element_off),11)||
           !loads(&b,8,4)||!storem(&b,8,0,11))goto fail;
    }else{
        if(!loadm(&b,10,0,(int32_t)(dynamic_index?layout->element_value_offset:element_off))||!loads(&b,8,4)||!storem(&b,8,0,10))goto fail;
    }
    if(!e8(&b,0x31)||!e8(&b,0xC0)||!e8(&b,0xC9)||!e8(&b,0xC3))goto fail;
    bail=b.n;if(!e8(&b,0xB8)||!e32(&b,TURBOJS_IR_BAILOUT)||!e8(&b,0xC9)||!e8(&b,0xC3))goto fail;
    for(i=0;i<bail_count;i++)p32(&b,bail_patches[i],(int32_t)(bail-(bail_patches[i]+4)));
    f->allocation_size=b.n;f->code=turbojs_executable_memory_allocate(b.n);if(!f->code)goto fail;
    memcpy(f->code,b.p,b.n);if(!turbojs_executable_memory_seal(f->code,b.n))goto fail;
    f->code_size=b.n;f->element_layout=*layout;f->has_element_layout=1;f->inline_element_is_store=(uint8_t)is_store;
    f->inline_element_kind_guard=1;f->inline_element_generation_guard=1;f->inline_element_bounds_guard=1;f->inline_element_dynamic_index=(uint8_t)dynamic_index;f->inline_element_dynamic_store=(uint8_t)dynamic_store;
    free(b.p);return 1;
fail:
    if(f&&f->code){turbojs_executable_memory_free(f->code,f->allocation_size);f->code=NULL;}
    free(b.p);return 0;
}

static int region_build_value_thunk(TurboJSRegionNativeFunction *f)
{
    Buf b={0};
    /* SysV entry: rdi=args, rsi=n, rdx=ops, rcx=opaque, r8=result.
       Helper: rdi=f, rsi=args, rdx=n, rcx=ops, r8=opaque, r9=result.
       Root all opaque JSValue inputs in the native frame before the helper call. */
    if(!e8(&b,0x55) || !e8(&b,0x48) || !e8(&b,0x89) || !e8(&b,0xE5) ||
       !e8(&b,0x48) || !e8(&b,0x83) || !e8(&b,0xEC) || !e8(&b,0x30))
        goto fail;
    if(!stores(&b,0,7) || !stores(&b,1,6) || !stores(&b,2,2) ||
       !stores(&b,3,1) || !stores(&b,4,8))
        goto fail;
    if(!movi(&b,7,(uint64_t)(uintptr_t)f) ||
       !loads(&b,6,0) || !loads(&b,2,1) || !loads(&b,1,2) ||
       !loads(&b,8,3) || !loads(&b,9,4) ||
       !movi(&b,0,(uint64_t)(uintptr_t)&region_value_execute) ||
       !e8(&b,0xFF) || !e8(&b,0xD0) || !e8(&b,0xC9) || !e8(&b,0xC3))
        goto fail;
    f->allocation_size=b.n;
    f->code=turbojs_executable_memory_allocate(f->allocation_size);
    if(!f->code) goto fail;
    memcpy(f->code,b.p,b.n);
    if(!turbojs_executable_memory_seal(f->code,f->allocation_size)) goto fail;
    f->code_size=b.n;
    free(b.p);
    return 1;
fail:
    if(f->code){turbojs_executable_memory_free(f->code,f->allocation_size);f->code=NULL;}
    free(b.p);
    return 0;
}
#endif

static TurboJSIRStatus region_clone_value_graph(const TurboJSSSAGraph*g,TurboJSRegionNativeFunction**out,TurboJSRegionNativeStats*st,TurboJSIRDiagnostic*d){
TurboJSRegionNativeFunction*f;size_t i,argc=0;uint64_t compile_start=region_now_ns();if(!g||!out)return TURBOJS_IR_INVALID_ARGUMENT;*out=NULL;
for(i=0;i<g->value_count;i++){const TurboJSSSAValue*v=&g->values[i];if(v->removed)continue;if((v->opcode==TURBOJS_SSA_PROPERTY_LOAD||v->opcode==TURBOJS_SSA_PROPERTY_STORE)&&(!v->property_case_count||v->property_case_count>TURBOJS_PROPERTY_PIC_MAX_CASES))return fail(d,TURBOJS_IR_UNSUPPORTED,i,"property operation requires guarded PIC feedback");if(v->opcode==TURBOJS_SSA_PROPERTY_STORE&&!(v->property_case_flags[0]&TURBOJS_PROPERTY_FEEDBACK_WRITABLE))return fail(d,TURBOJS_IR_UNSUPPORTED,i,"property store requires writable feedback");if(v->opcode==TURBOJS_SSA_ARGUMENT&&(size_t)(v->immediate+1)>argc)argc=(size_t)v->immediate+1;}
f=(TurboJSRegionNativeFunction*)calloc(1,sizeof(*f));if(!f)return TURBOJS_IR_OUT_OF_MEMORY;
f->value_graph.values=(TurboJSSSAValue*)malloc(g->value_count*sizeof(*g->values));f->value_graph.blocks=(TurboJSSSABlock*)malloc(g->block_count*sizeof(*g->blocks));if((g->value_count&&!f->value_graph.values)||(g->block_count&&!f->value_graph.blocks)){free(f->value_graph.values);free(f->value_graph.blocks);free(f);return TURBOJS_IR_OUT_OF_MEMORY;}
memcpy(f->value_graph.values,g->values,g->value_count*sizeof(*g->values));memcpy(f->value_graph.blocks,g->blocks,g->block_count*sizeof(*g->blocks));f->value_graph.value_count=f->value_graph.value_capacity=g->value_count;f->value_graph.block_count=f->value_graph.block_capacity=g->block_count;f->value_graph.entry_block=g->entry_block;f->value_graph.deopt_exit_count=g->deopt_exit_count;f->argument_count=argc;f->value_mode=1;
#if defined(__x86_64__) && !defined(_WIN32)
if(region_build_value_thunk(f))f->value_mode=2;
#endif
*out=f;if(st){memset(st,0,sizeof(*st));st->block_count=(uint32_t)g->block_count;st->native_code_bytes=f->code_size;st->frame_bytes=f->value_mode==2?48:0;st->compile_time_ns=region_now_ns()-compile_start;}if(d){d->status=TURBOJS_IR_OK;d->message=NULL;d->instruction_index=0;}return TURBOJS_IR_OK;}
TurboJSIRStatus TurboJS_RegionNativeCompile(const TurboJSSSAGraph*g,TurboJSRegionNativeFunction**out,TurboJSRegionNativeStats*st,TurboJSIRDiagnostic*d){
    if(region_has_property_loads(g)||region_has_property_stores(g)||region_has_element_ops(g))return region_clone_value_graph(g,out,st,d);
    return turbojs_region_native_compile_register(g,out,st,d);
}
TurboJSIRStatus TurboJS_RegionNativeCompileWithObjectLayout(const TurboJSSSAGraph*g,const TurboJSRegionObjectLayout*layout,TurboJSRegionNativeFunction**out,TurboJSRegionNativeStats*st,TurboJSIRDiagnostic*d){
    TurboJSIRStatus rs;
#if defined(__x86_64__) && !defined(_WIN32)
    uint64_t start=region_now_ns();
#endif
    if(!layout)return TurboJS_RegionNativeCompile(g,out,st,d);
    rs=region_clone_value_graph(g,out,st,d);if(rs!=TURBOJS_IR_OK)return rs;
#if defined(__x86_64__) && !defined(_WIN32)
    if(*out&&(region_has_property_loads(g)||region_has_property_stores(g))){
        TurboJSRegionNativeFunction*f=*out;
        if(f->code){turbojs_executable_memory_free(f->code,f->allocation_size);f->code=NULL;f->code_size=f->allocation_size=0;}
        if(g->value_count&&region_build_inline_property_pic(f,layout)){f->value_mode=3;if(st){st->native_code_bytes=f->code_size;st->frame_bytes=64;st->inline_property_pic_cases=f->property_pic_cases;st->inline_property_loads=f->property_pic_is_store?0:1;st->inline_property_stores=f->property_pic_is_store?1:0;st->inline_property_dependency_guards=f->property_pic_dependency_guards;st->compile_time_ns=region_now_ns()-start;}return TURBOJS_IR_OK;}
        if(region_build_value_thunk(f))f->value_mode=2;
    }
#endif
    return TURBOJS_IR_OK;
}
TurboJSIRStatus TurboJS_RegionNativeCompileWithElementLayout(const TurboJSSSAGraph*g,const TurboJSRegionElementLayout*layout,TurboJSRegionNativeFunction**out,TurboJSRegionNativeStats*st,TurboJSIRDiagnostic*d){
    TurboJSIRStatus rs;
#if defined(__x86_64__) && !defined(_WIN32)
    uint64_t start=region_now_ns();
#endif
    if(!layout)return TurboJS_RegionNativeCompile(g,out,st,d);
    rs=region_clone_value_graph(g,out,st,d);if(rs!=TURBOJS_IR_OK)return rs;
#if defined(__x86_64__) && !defined(_WIN32)
    if(*out&&(region_has_element_ops(g)||region_has_proven_element_loop(g))){
        TurboJSRegionNativeFunction*f=*out;
        if(f->code){turbojs_executable_memory_free(f->code,f->allocation_size);f->code=NULL;f->code_size=f->allocation_size=0;}
        if(g->value_count&&region_configure_simd_f64(f,layout)){
            f->value_mode=5;
            if(st){st->native_code_bytes=0;st->frame_bytes=0;st->inline_element_loops=1;st->inline_element_loop_length_hoists=1;st->inline_element_loop_base_hoists=1;st->inline_element_loop_bounds_eliminated=1;st->inline_element_loop_unroll_factor=f->inline_element_loop_unroll_factor;st->inline_element_loop_accumulators=f->inline_element_loop_accumulators;st->inline_element_float64_loops=1;st->inline_element_transform_loops=f->inline_element_transform_loop?1u:0u;st->inline_element_scale_only_loops=f->simd_transform_kind==1?1u:0u;st->inline_element_bias_only_loops=f->simd_transform_kind==2?1u:0u;st->inline_element_subtract_only_loops=f->simd_transform_kind==3?1u:0u;st->inline_element_dual_source_loops=(f->simd_transform_kind==4||f->simd_transform_kind==5)?1u:0u;st->inline_element_dual_source_subtract_loops=f->simd_transform_kind==5?1u:0u;st->inline_element_min_loops=f->simd_transform_kind==6?1u:0u;st->inline_element_max_loops=f->simd_transform_kind==7?1u:0u;st->inline_element_clamp_loops=f->simd_transform_kind==8?1u:0u;st->inline_element_float64_operations=f->inline_element_transform_loop?16u:8u;st->inline_element_simd_loops=1;st->inline_element_simd_width=4;st->inline_element_simd_level=f->simd_level;st->inline_element_simd_fma=f->simd_level>=TURBOJS_X64_SIMD_FMA3?1u:0u;st->inline_element_simd_fast_math=st->inline_element_simd_fma;st->compile_time_ns=region_now_ns()-start;}
            return TURBOJS_IR_OK;
        }
        if(g->value_count&&(region_build_inline_element_transform_f64(f,layout)||region_build_inline_element_loop_f64(f,layout)||region_build_inline_element_loop(f,layout))){
            f->value_mode=4;
            if(st){st->native_code_bytes=f->code_size;st->frame_bytes=48;st->inline_element_loops=1;st->inline_element_loop_length_hoists=1;st->inline_element_loop_base_hoists=1;st->inline_element_loop_bounds_eliminated=1;st->inline_element_loop_unroll_factor=f->inline_element_loop_unroll_factor;st->inline_element_loop_accumulators=f->inline_element_loop_accumulators;st->inline_element_float64_loops=f->inline_element_float64_loop?1u:0u;st->inline_element_transform_loops=f->inline_element_transform_loop?1u:0u;st->inline_element_scale_only_loops=f->simd_transform_kind==1?1u:0u;st->inline_element_bias_only_loops=f->simd_transform_kind==2?1u:0u;st->inline_element_subtract_only_loops=f->simd_transform_kind==3?1u:0u;st->inline_element_dual_source_loops=(f->simd_transform_kind==4||f->simd_transform_kind==5)?1u:0u;st->inline_element_dual_source_subtract_loops=f->simd_transform_kind==5?1u:0u;st->inline_element_min_loops=f->simd_transform_kind==6?1u:0u;st->inline_element_max_loops=f->simd_transform_kind==7?1u:0u;st->inline_element_clamp_loops=f->simd_transform_kind==8?1u:0u;st->inline_element_float64_operations=f->inline_element_transform_loop?4u:(f->inline_element_float64_loop?2u:0u);st->compile_time_ns=region_now_ns()-start;}
            return TURBOJS_IR_OK;
        }
        if(g->value_count&&region_build_inline_element(f,layout)){
            f->value_mode=4;
            if(st){st->native_code_bytes=f->code_size;st->frame_bytes=48;st->inline_element_loads=f->inline_element_is_store?0:1;st->inline_element_stores=f->inline_element_is_store?1:0;st->inline_element_kind_guards=f->inline_element_kind_guard;st->inline_element_generation_guards=f->inline_element_generation_guard;st->inline_element_bounds_guards=f->inline_element_bounds_guard;st->inline_element_dynamic_indexes=f->inline_element_dynamic_index;st->inline_element_dynamic_stores=f->inline_element_dynamic_store;st->compile_time_ns=region_now_ns()-start;}
            return TURBOJS_IR_OK;
        }
        if(region_build_value_thunk(f))f->value_mode=2;
    }
#endif
    return TURBOJS_IR_OK;
}
TurboJSIRStatus TurboJS_RegionNativeInvoke(const TurboJSRegionNativeFunction*f,const int64_t*args,size_t n,int64_t*r){typedef int(*Fn)(const int64_t*,int64_t*);if(!f||!r||n<f->argument_count||(!args&&n))return TURBOJS_IR_INVALID_ARGUMENT;if(f->value_mode)return TURBOJS_IR_UNSUPPORTED;return ((Fn)f->code)(args,r)==0?TURBOJS_IR_OK:TURBOJS_IR_BAILOUT;}
static TurboJSIRStatus region_value_execute(const TurboJSRegionNativeFunction*f,const TurboJSRegionValue*args,size_t n,const TurboJSRegionValueOps*ops,void*opaque,TurboJSRegionValue*r){
TurboJSRegionValue*vals;uint8_t*done;uint32_t block;size_t steps=0,i;if(!f||!r||!ops||!ops->guard_shape||!ops->load_own_slot||!ops->to_i64||!ops->from_i64||!f->value_mode||n<f->argument_count||(!args&&n))return TURBOJS_IR_INVALID_ARGUMENT;
vals=(TurboJSRegionValue*)calloc(f->value_graph.value_count,sizeof(*vals));done=(uint8_t*)calloc(f->value_graph.value_count,1);if(!vals||!done){free(vals);free(done);return TURBOJS_IR_OUT_OF_MEMORY;}block=f->value_graph.entry_block;
while(block<f->value_graph.block_count&&steps++<10000000u){const TurboJSSSABlock*b=&f->value_graph.blocks[block];uint32_t next=TURBOJS_SSA_NO_BLOCK;for(i=b->first_value;i<(size_t)b->first_value+b->value_count;i++){const TurboJSSSAValue*v=&f->value_graph.values[i];int64_t a,c;if(v->removed)continue;switch(v->opcode){
case TURBOJS_SSA_ARGUMENT:if((size_t)v->immediate>=n)goto bailout;vals[v->id]=args[v->immediate];done[v->id]=1;break;
case TURBOJS_SSA_CONSTANT_I64:vals[v->id]=ops->from_i64(v->immediate,opaque);done[v->id]=1;break;
case TURBOJS_SSA_PROPERTY_LOAD:{uint8_t k,matched=0;if(!done[v->left])goto bailout;for(k=0;k<v->property_case_count;k++){if(ops->guard_shape(vals[v->left],v->property_shapes[k],opaque)){if(ops->guard_property_dependency&&!ops->guard_property_dependency(v->property_generations[k],v->property_case_flags[k],opaque))goto bailout;if(!ops->load_own_slot(vals[v->left],v->property_indices[k],&vals[v->id],opaque))goto bailout;matched=1;break;}}if(!matched)goto bailout;done[v->id]=1;break;}
case TURBOJS_SSA_PROPERTY_STORE:{uint8_t k,matched=0;if(!done[v->left]||!done[v->right]||!ops->store_own_slot)goto bailout;for(k=0;k<v->property_case_count;k++){if(ops->guard_shape(vals[v->left],v->property_shapes[k],opaque)){if(ops->guard_property_dependency&&!ops->guard_property_dependency(v->property_generations[k],v->property_case_flags[k],opaque))goto bailout;if(!ops->store_own_slot(vals[v->left],v->property_indices[k],vals[v->right],opaque))goto bailout;matched=1;break;}}if(!matched)goto bailout;break;}
case TURBOJS_SSA_ELEMENT_LOAD:{int64_t index;uint32_t length;if(!done[v->left]||!done[v->right]||!ops->guard_elements||!ops->array_length||!ops->load_element||!ops->to_i64(vals[v->right],&index,opaque)||index<0||index>UINT32_MAX)goto bailout;if(!ops->guard_elements(vals[v->left],v->element_kind,v->element_generation,v->element_flags,opaque)||!ops->array_length(vals[v->left],&length,opaque)||(uint32_t)index>=length||!ops->load_element(vals[v->left],(uint32_t)index,&vals[v->id],opaque))goto bailout;done[v->id]=1;break;}
case TURBOJS_SSA_ELEMENT_STORE:{int64_t index;uint32_t length;if(!done[v->left]||!done[v->right]||!done[v->metadata]||!ops->guard_elements||!ops->array_length||!ops->store_element||!ops->to_i64(vals[v->right],&index,opaque)||index<0||index>UINT32_MAX)goto bailout;if(!(v->element_flags&TURBOJS_ELEMENT_FLAG_WRITABLE)||!ops->guard_elements(vals[v->left],v->element_kind,v->element_generation,v->element_flags,opaque)||!ops->array_length(vals[v->left],&length,opaque)||(uint32_t)index>=length||!ops->store_element(vals[v->left],(uint32_t)index,vals[v->metadata],opaque))goto bailout;break;}
case TURBOJS_SSA_ADD_I64:case TURBOJS_SSA_SUB_I64:case TURBOJS_SSA_MUL_I64:case TURBOJS_SSA_AND_I64:case TURBOJS_SSA_OR_I64:case TURBOJS_SSA_XOR_I64:case TURBOJS_SSA_SHL_I64:case TURBOJS_SSA_SAR_I64:case TURBOJS_SSA_SHR_I64:case TURBOJS_SSA_LESS_THAN_I64:case TURBOJS_SSA_LESS_EQUAL_I64:case TURBOJS_SSA_GREATER_THAN_I64:case TURBOJS_SSA_GREATER_EQUAL_I64:case TURBOJS_SSA_EQUAL_I64:if(!done[v->left]||!done[v->right]||!ops->to_i64(vals[v->left],&a,opaque)||!ops->to_i64(vals[v->right],&c,opaque))goto bailout;if(v->opcode==TURBOJS_SSA_ADD_I64)a+=c;else if(v->opcode==TURBOJS_SSA_SUB_I64)a-=c;else if(v->opcode==TURBOJS_SSA_MUL_I64)a*=c;else if(v->opcode==TURBOJS_SSA_AND_I64)a&=c;else if(v->opcode==TURBOJS_SSA_OR_I64)a|=c;else if(v->opcode==TURBOJS_SSA_XOR_I64)a^=c;else if(v->opcode==TURBOJS_SSA_SHL_I64)a=(int64_t)((uint64_t)a<<((uint64_t)c&63u));else if(v->opcode==TURBOJS_SSA_SAR_I64)a>>=((uint64_t)c&63u);else if(v->opcode==TURBOJS_SSA_SHR_I64)a=(int64_t)((uint64_t)a>>((uint64_t)c&63u));else if(v->opcode==TURBOJS_SSA_LESS_THAN_I64)a=a<c;else if(v->opcode==TURBOJS_SSA_LESS_EQUAL_I64)a=a<=c;else if(v->opcode==TURBOJS_SSA_GREATER_THAN_I64)a=a>c;else if(v->opcode==TURBOJS_SSA_GREATER_EQUAL_I64)a=a>=c;else a=a==c;vals[v->id]=ops->from_i64(a,opaque);done[v->id]=1;break;
case TURBOJS_SSA_JUMP:next=v->metadata;break;
case TURBOJS_SSA_BRANCH_TRUE:case TURBOJS_SSA_BRANCH_FALSE:if(!done[v->left]||!ops->to_i64(vals[v->left],&a,opaque))goto bailout;next=((v->opcode==TURBOJS_SSA_BRANCH_TRUE)?(a!=0):(a==0))?v->metadata:(b->successor_count>1?b->successors[1]:TURBOJS_SSA_NO_BLOCK);break;
case TURBOJS_SSA_RETURN:if(!done[v->left])goto bailout;*r=vals[v->left];free(vals);free(done);return TURBOJS_IR_OK;
case TURBOJS_SSA_PHI:case TURBOJS_SSA_NOP:case TURBOJS_SSA_GUARD_INT32:break;
default:goto bailout;}}
if(next==TURBOJS_SSA_NO_BLOCK){if(b->successor_count==1)next=b->successors[0];else goto bailout;}block=next;}
bailout:free(vals);free(done);return TURBOJS_IR_BAILOUT;}

static int region_read_u16(const uint8_t *p,size_t off,uint16_t*out){if(!p||!out)return 0;memcpy(out,p+off,sizeof(*out));return 1;}
static int region_read_u32(const uint8_t *p,size_t off,uint32_t*out){if(!p||!out)return 0;memcpy(out,p+off,sizeof(*out));return 1;}
static int region_simd_array(const TurboJSRegionNativeFunction*f,TurboJSRegionValue value,uint32_t generation,
                             const double **data,uint32_t *length)
{
    uintptr_t raw=(uintptr_t)value,ptr;const uint8_t*object;uint16_t kind;uint32_t gen,len;
    const TurboJSRegionElementLayout*l=&f->element_layout;
    if(l->object_tag_mask&&((raw&l->object_tag_mask)!=l->object_tag_value))return 0;
    ptr=raw&(uintptr_t)l->object_pointer_mask;if(!ptr)return 0;object=(const uint8_t*)ptr;
    if(!region_read_u16(object,l->kind_offset,&kind)||kind!=TURBOJS_ELEMENT_KIND_TYPED_F64)return 0;
    if(!region_read_u32(object,l->generation_offset,&gen)||gen!=generation)return 0;
    if(!region_read_u32(object,l->length_offset,&len))return 0;
    if(l->element_storage_indirect){uintptr_t storage=0;memcpy(&storage,object+l->element_storage_offset,sizeof(storage));if(!storage)return 0;*data=(const double*)(storage+l->element_value_offset);}
    else *data=(const double*)(object+l->element_storage_offset+l->element_value_offset);
    *length=len;return 1;
}
static TurboJSIRStatus region_simd_invoke(const TurboJSRegionNativeFunction*f,const TurboJSRegionValue*args,size_t n,TurboJSRegionValue*r)
{
    const double *source=NULL,*right=NULL;double *destination=NULL;uint32_t source_length=0,right_length=0,destination_length=0;int64_t limit;double value;
    if(!f||!args||!r||f->simd_limit_arg>=n||f->simd_source_arg>=n)return TURBOJS_IR_INVALID_ARGUMENT;
    limit=(int64_t)args[f->simd_limit_arg];if(limit<0||limit>UINT32_MAX)return TURBOJS_IR_BAILOUT;
    if(!region_simd_array(f,args[f->simd_source_arg],f->simd_source_generation,&source,&source_length)||(uint32_t)limit>source_length)return TURBOJS_IR_BAILOUT;
    if(f->simd_kernel==1){value=turbojs_x64_f64_sum(source,(size_t)limit,(TurboJSX64SIMDLevel)f->simd_level);memcpy(r,&value,sizeof(value));return TURBOJS_IR_OK;}
    if(f->simd_kernel==2){double scale,bias;const double*dst_const;uintptr_t sb,se,db,de;
        if(f->simd_destination_arg>=n||(f->simd_scale_arg!=UINT8_MAX&&f->simd_scale_arg>=n)||(f->simd_bias_arg!=UINT8_MAX&&f->simd_bias_arg>=n))return TURBOJS_IR_INVALID_ARGUMENT;
        if(!region_simd_array(f,args[f->simd_destination_arg],f->simd_destination_generation,&dst_const,&destination_length)||(uint32_t)limit>destination_length)return TURBOJS_IR_BAILOUT;
        destination=(double*)(uintptr_t)dst_const;scale=1.0;bias=0.0;if(f->simd_scale_arg!=UINT8_MAX)memcpy(&scale,&args[f->simd_scale_arg],sizeof(scale));if(f->simd_bias_arg!=UINT8_MAX)memcpy(&bias,&args[f->simd_bias_arg],sizeof(bias));if(f->simd_transform_kind==3)bias=-bias;
        sb=(uintptr_t)source;se=sb+(size_t)limit*sizeof(double);db=(uintptr_t)destination;de=db+(size_t)limit*sizeof(double);
        if(source!=destination&&sb<de&&db<se)return TURBOJS_IR_BAILOUT;
        turbojs_x64_f64_transform(source,destination,(size_t)limit,scale,bias,(TurboJSX64SIMDLevel)f->simd_level);*r=(TurboJSRegionValue)limit;return TURBOJS_IR_OK;
    }
    if(f->simd_kernel==3){const double*dst_const;uintptr_t lb,le,rb,re,db,de;
        if(f->simd_right_arg>=n||f->simd_destination_arg>=n)return TURBOJS_IR_INVALID_ARGUMENT;
        if(!region_simd_array(f,args[f->simd_right_arg],f->simd_right_generation,&right,&right_length)||(uint32_t)limit>right_length)return TURBOJS_IR_BAILOUT;
        if(!region_simd_array(f,args[f->simd_destination_arg],f->simd_destination_generation,&dst_const,&destination_length)||(uint32_t)limit>destination_length)return TURBOJS_IR_BAILOUT;
        destination=(double*)(uintptr_t)dst_const;lb=(uintptr_t)source;le=lb+(size_t)limit*sizeof(double);rb=(uintptr_t)right;re=rb+(size_t)limit*sizeof(double);db=(uintptr_t)destination;de=db+(size_t)limit*sizeof(double);
        if((source!=destination&&lb<de&&db<le)||(right!=destination&&rb<de&&db<re))return TURBOJS_IR_BAILOUT;
        turbojs_x64_f64_binary(source,right,destination,(size_t)limit,f->simd_transform_kind==5,(TurboJSX64SIMDLevel)f->simd_level);*r=(TurboJSRegionValue)limit;return TURBOJS_IR_OK;
    }
    if(f->simd_kernel==4){double lower=-1.7976931348623157e308,upper=1.7976931348623157e308;const double*dst_const;uintptr_t sb,se,db,de;int mode;
        if(f->simd_destination_arg>=n||(f->simd_scale_arg!=UINT8_MAX&&f->simd_scale_arg>=n)||(f->simd_bias_arg!=UINT8_MAX&&f->simd_bias_arg>=n))return TURBOJS_IR_INVALID_ARGUMENT;
        if(!region_simd_array(f,args[f->simd_destination_arg],f->simd_destination_generation,&dst_const,&destination_length)||(uint32_t)limit>destination_length)return TURBOJS_IR_BAILOUT;
        destination=(double*)(uintptr_t)dst_const;if(f->simd_scale_arg!=UINT8_MAX)memcpy(&lower,&args[f->simd_scale_arg],sizeof(lower));if(f->simd_bias_arg!=UINT8_MAX)memcpy(&upper,&args[f->simd_bias_arg],sizeof(upper));
        if(f->simd_transform_kind==6)mode=0;else if(f->simd_transform_kind==7)mode=1;else mode=2;if(mode==2&&lower>upper)return TURBOJS_IR_BAILOUT;
        sb=(uintptr_t)source;se=sb+(size_t)limit*sizeof(double);db=(uintptr_t)destination;de=db+(size_t)limit*sizeof(double);if(source!=destination&&sb<de&&db<se)return TURBOJS_IR_BAILOUT;
        turbojs_x64_f64_bound(source,destination,(size_t)limit,lower,upper,mode,(TurboJSX64SIMDLevel)f->simd_level);*r=(TurboJSRegionValue)limit;return TURBOJS_IR_OK;
    }
    return TURBOJS_IR_UNSUPPORTED;
}

TurboJSIRStatus TurboJS_RegionNativeInvokeValues(const TurboJSRegionNativeFunction*f,const TurboJSRegionValue*args,size_t n,const TurboJSRegionValueOps*ops,void*opaque,TurboJSRegionValue*r){
if(!f)return TURBOJS_IR_INVALID_ARGUMENT;
#if defined(__x86_64__) && !defined(_WIN32)
if(f->value_mode==5)return region_simd_invoke(f,args,n,r);
if((f->value_mode==2||f->value_mode==3||f->value_mode==4)&&f->code)return ((RegionValueNativeFn)f->code)(args,n,ops,opaque,r);
#endif
return region_value_execute(f,args,n,ops,opaque,r);
}
void TurboJS_RegionNativeFunctionDestroy(TurboJSRegionNativeFunction*f){if(!f)return;if(f->code)turbojs_executable_memory_free(f->code,f->allocation_size);TurboJS_LinearScanResultDestroy(&f->allocation);free(f->value_graph.values);free(f->value_graph.blocks);free(f);}
size_t TurboJS_RegionNativeCodeSize(const TurboJSRegionNativeFunction*f){return f?f->code_size:0;}

int TurboJS_RegionNativeWriteCode(const TurboJSRegionNativeFunction*f,const char*path){FILE*fp;if(!f||!path)return 0;fp=fopen(path,"wb");if(!fp)return 0;if(fwrite(f->code,1,f->code_size,fp)!=f->code_size){fclose(fp);return 0;}return fclose(fp)==0;}
int TurboJS_RegionNativeWriteAllocation(const TurboJSRegionNativeFunction*f,const char*path){FILE*fp;size_t i;if(!f||!path)return 0;fp=fopen(path,"w");if(!fp)return 0;fprintf(fp,"intervals=%zu fragments=%zu spills=%u splits=%u phi_coalesce=%u/%u rejected=%u code_bytes=%zu\n",f->allocation.interval_count,f->allocation.fragment_count,(unsigned)f->allocation.spill_slot_count,(unsigned)f->allocation.split_count,f->allocation.phi_coalesce_successes,f->allocation.phi_coalesce_candidates,f->allocation.phi_coalesce_rejected,f->code_size);for(i=0;i<f->allocation.interval_count;i++){const TurboJSLiveInterval*v=&f->allocation.intervals[i];fprintf(fp,"interval v%u [%u,%u] uses=%u loop=%u backedge=%u reg=%d spill=%d\n",v->value_id,v->start,v->end,v->use_count,v->loop_depth,v->crosses_backedge,v->physical_register,v->spill_slot);}for(i=0;i<f->allocation.fragment_count;i++){const TurboJSLiveIntervalFragment*v=&f->allocation.fragments[i];fprintf(fp,"fragment v%u [%u,%u] reg=%d spill=%d\n",v->value_id,v->start,v->end,v->physical_register,v->spill_slot);}return fclose(fp)==0;}
