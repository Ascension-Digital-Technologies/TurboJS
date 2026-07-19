#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "jit.h"

typedef enum RegionOpcode {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) OP_##id,
#define def(id, size, n_pop, n_push, f)
#include "internal/bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
    OP_COUNT
} RegionOpcode;

static const int8_t op_pop[] = {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) [OP_##id] = n_pop,
#define def(id, size, n_pop, n_push, f)
#include "internal/bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
};
static const int8_t op_push[] = {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) [OP_##id] = n_push,
#define def(id, size, n_pop, n_push, f)
#include "internal/bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
};

static TurboJSIRStatus fail(TurboJSIRDiagnostic *d, TurboJSIRStatus s,
                            size_t at, const char *m) {
    if (d) { d->status=s; d->instruction_index=at; d->message=m; }
    return s;
}
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static int reserve_values(TurboJSBytecodeRegionStateGraph *g,size_t n){
    TurboJSBytecodeRegionValue *p; size_t c=g->value_capacity?g->value_capacity:32;
    if (n <= g->value_capacity) return 1;
    while (c < n) { if (c > SIZE_MAX / 2) return 0; c *= 2; }
    p=(TurboJSBytecodeRegionValue*)realloc(g->values,c*sizeof(*p));if(!p)return 0;
    g->values=p;g->value_capacity=c;return 1;
}
static int reserve_inputs(TurboJSBytecodeRegionStateGraph *g,size_t n){
    TurboJSBytecodeRegionPhiInput *p; size_t c=g->phi_input_capacity?g->phi_input_capacity:32;
    if (n <= g->phi_input_capacity) return 1;
    while (c < n) { if (c > SIZE_MAX / 2) return 0; c *= 2; }
    p=(TurboJSBytecodeRegionPhiInput*)realloc(g->phi_inputs,c*sizeof(*p));if(!p)return 0;
    g->phi_inputs=p;g->phi_input_capacity=c;return 1;
}
static uint32_t value_new(TurboJSBytecodeRegionStateGraph*g,uint8_t kind,uint32_t block,
                          uint32_t off,uint8_t opcode,uint32_t l,uint32_t r,
                          uint16_t index,int stack){
    TurboJSBytecodeRegionValue*v;uint32_t id;
    if(!reserve_values(g,g->value_count+1))return TURBOJS_REGION_NO_VALUE;
    id=(uint32_t)g->value_count++;v=&g->values[id];memset(v,0,sizeof(*v));
    v->id=id;v->kind=kind;v->block=block;v->source_offset=off;v->opcode=opcode;
    v->left=l;v->right=r;v->local_or_stack_index=index;v->is_stack_value=(uint8_t)stack;
    v->phi_input_start=TURBOJS_REGION_NO_VALUE;return id;
}
static int local_index(uint8_t op,const uint8_t*p,uint32_t*out,int*is_get,int*keep){
    *keep=0;
    switch(op){
    case OP_get_loc:case OP_get_loc_check:*out=u16(p);*is_get=1;return 1;
    case OP_get_loc8:*out=p[0];*is_get=1;return 1;
    case OP_get_loc0:case OP_get_loc1:case OP_get_loc2:case OP_get_loc3:*out=op-OP_get_loc0;*is_get=1;return 1;
    case OP_put_loc:case OP_put_loc_check:*out=u16(p);*is_get=0;return 1;
    case OP_put_loc8:*out=p[0];*is_get=0;return 1;
    case OP_put_loc0:case OP_put_loc1:case OP_put_loc2:case OP_put_loc3:*out=op-OP_put_loc0;*is_get=0;return 1;
    case OP_set_loc:*out=u16(p);*is_get=0;*keep=1;return 1;
    case OP_set_loc8:*out=p[0];*is_get=0;*keep=1;return 1;
    case OP_set_loc0:case OP_set_loc1:case OP_set_loc2:case OP_set_loc3:*out=op-OP_set_loc0;*is_get=0;*keep=1;return 1;
    default:return 0;}
}
static int arg_index(uint8_t op,const uint8_t*p,uint32_t*out){
    if(op==OP_get_arg){*out=u16(p);return 1;} if(op>=OP_get_arg0&&op<=OP_get_arg3){*out=op-OP_get_arg0;return 1;}return 0;
}
static uint32_t find_value(const TurboJSBytecodeRegionStateGraph *g,uint32_t block,uint32_t off,uint8_t kind){size_t i;for(i=0;i<g->value_count;i++)if(g->values[i].block==block&&g->values[i].source_offset==off&&g->values[i].kind==kind)return g->values[i].id;return TURBOJS_REGION_NO_VALUE;}
static uint32_t *eloc(TurboJSBytecodeRegionStateGraph*g,uint32_t b){return g->entry_locals+g->blocks[b].entry_local_offset;}
static uint32_t *xloc(TurboJSBytecodeRegionStateGraph*g,uint32_t b){return g->exit_locals+g->blocks[b].exit_local_offset;}
static uint32_t *estk(TurboJSBytecodeRegionStateGraph*g,uint32_t b){return g->entry_stack+g->blocks[b].entry_stack_offset;}
static uint32_t *xstk(TurboJSBytecodeRegionStateGraph*g,uint32_t b){return g->exit_stack+g->blocks[b].exit_stack_offset;}

void TurboJS_EngineBytecodeRegionStateGraphDestroy(TurboJSBytecodeRegionStateGraph*g){
    if (!g) return;
    TurboJS_EngineBytecodeRegionPlanDestroy(&g->plan); free(g->blocks); free(g->values);
    free(g->phi_inputs);free(g->entry_locals);free(g->exit_locals);free(g->entry_stack);free(g->exit_stack);memset(g,0,sizeof(*g));
}

static uint32_t merge_slot(TurboJSBytecodeRegionStateGraph*g,uint32_t block,uint32_t slot,int stack,
                           uint32_t *phi_map){
    const TurboJSBytecodeBlock*b=&g->plan.cfg.blocks[block];uint32_t merged=TURBOJS_REGION_NO_VALUE;uint32_t i;
    for(i=0;i<b->predecessor_count;i++){
        uint32_t p=b->predecessors[i];uint32_t v=stack?xstk(g,p)[slot]:xloc(g,p)[slot];
        if (v == TURBOJS_REGION_NO_VALUE) continue;
        if (merged == TURBOJS_REGION_NO_VALUE) merged = v;
        else if (merged != v) {
            uint32_t key=block*(g->local_count+g->stack_stride)+(stack?g->local_count+slot:slot);
            if(phi_map[key]==TURBOJS_REGION_NO_VALUE){
                phi_map[key]=value_new(g,TURBOJS_REGION_VALUE_PHI,block,b->start_offset,0,
                                       TURBOJS_REGION_NO_VALUE,TURBOJS_REGION_NO_VALUE,(uint16_t)slot,stack);
                if(phi_map[key]==TURBOJS_REGION_NO_VALUE)return TURBOJS_REGION_NO_VALUE;
                g->active_phi_count++;if(stack)g->blocks[block].active_stack_phis++;else g->blocks[block].active_local_phis++;
            }
            return phi_map[key];
        }
    }
    return merged;
}

TurboJSIRStatus TurboJS_EngineBytecodeBuildRegionStateGraph(
    const TurboJSEngineBytecodeInfo*bc,TurboJSBytecodeRegionStateGraph*g,TurboJSIRDiagnostic*d){
    size_t nslots,bi;uint32_t *phi_map=NULL,*ins_value=NULL,*queue=NULL;uint8_t*queued=NULL;size_t qh=0,qt=0;TurboJSIRStatus st;
    if (!bc || !g) return fail(d, TURBOJS_IR_INVALID_ARGUMENT, 0, "invalid region state input");
    memset(g, 0, sizeof(*g));
    st=TurboJS_EngineBytecodeBuildRegionPlan(bc,&g->plan,d);if(st!=TURBOJS_IR_OK)return st;
    g->local_count=bc->local_count;g->stack_stride=g->plan.cfg.maximum_stack_depth?g->plan.cfg.maximum_stack_depth:1;
    g->blocks=(TurboJSBytecodeRegionStateBlock*)calloc(g->plan.block_count,sizeof(*g->blocks));
    nslots=g->plan.block_count*(size_t)(g->local_count+g->stack_stride);
    g->entry_locals=(uint32_t*)malloc(g->plan.block_count*(size_t)g->local_count*sizeof(uint32_t));
    g->exit_locals=(uint32_t*)malloc(g->plan.block_count*(size_t)g->local_count*sizeof(uint32_t));
    g->entry_stack=(uint32_t*)malloc(g->plan.block_count*(size_t)g->stack_stride*sizeof(uint32_t));
    g->exit_stack=(uint32_t*)malloc(g->plan.block_count*(size_t)g->stack_stride*sizeof(uint32_t));
    phi_map=(uint32_t*)malloc(nslots*sizeof(uint32_t));ins_value=(uint32_t*)malloc(g->plan.cfg.instruction_count*sizeof(uint32_t));
    queue=(uint32_t*)malloc(g->plan.block_count*16u*sizeof(uint32_t));queued=(uint8_t*)calloc(g->plan.block_count,1);
    if(!g->blocks||(!g->entry_locals&&g->local_count)||(!g->exit_locals&&g->local_count)||!g->entry_stack||!g->exit_stack||!phi_map||!ins_value||!queue||!queued){st=fail(d,TURBOJS_IR_OUT_OF_MEMORY,0,"unable to allocate region state graph");goto done;}
    for(bi=0;bi<g->plan.block_count;bi++){g->blocks[bi].entry_local_offset=(uint32_t)(bi*g->local_count);g->blocks[bi].exit_local_offset=(uint32_t)(bi*g->local_count);g->blocks[bi].entry_stack_offset=(uint32_t)(bi*g->stack_stride);g->blocks[bi].exit_stack_offset=(uint32_t)(bi*g->stack_stride);}
    for(bi=0;bi<g->plan.block_count*(size_t)g->local_count;bi++)g->entry_locals[bi]=g->exit_locals[bi]=TURBOJS_REGION_NO_VALUE;
    for(bi=0;bi<g->plan.block_count*(size_t)g->stack_stride;bi++)g->entry_stack[bi]=g->exit_stack[bi]=TURBOJS_REGION_NO_VALUE;
    for (bi = 0; bi < nslots; bi++) phi_map[bi] = TURBOJS_REGION_NO_VALUE;
    for (bi = 0; bi < g->plan.cfg.instruction_count; bi++) ins_value[bi] = TURBOJS_REGION_NO_VALUE;
    for(bi=0;bi<g->local_count;bi++){uint32_t v=value_new(g,TURBOJS_REGION_VALUE_INITIAL_LOCAL,0,0,0,TURBOJS_REGION_NO_VALUE,TURBOJS_REGION_NO_VALUE,(uint16_t)bi,0);if(v==TURBOJS_REGION_NO_VALUE){st=fail(d,TURBOJS_IR_OUT_OF_MEMORY,0,"unable to allocate initial local values");goto done;}eloc(g,0)[bi]=v;}
    queue[qt++]=0;queued[0]=1;
    while(qh<qt){uint32_t bidx=queue[qh++],depth=g->plan.cfg.blocks[bidx].entry_stack_depth,ii;uint32_t locals[256],stack[256];int changed=0;queued[bidx]=0;
        if(g->local_count>256||g->stack_stride>256){st=fail(d,TURBOJS_IR_UNSUPPORTED,0,"region state exceeds temporary limits");goto done;}
        memcpy(locals,eloc(g,bidx),g->local_count*sizeof(uint32_t));memcpy(stack,estk(g,bidx),g->stack_stride*sizeof(uint32_t));
        for(ii=0;ii<g->plan.cfg.blocks[bidx].instruction_count;ii++){
            uint32_t ino=g->plan.cfg.blocks[bidx].first_instruction+ii,off=g->plan.cfg.instruction_offsets[ino],idx;uint8_t op=bc->bytecode[off];int get=0,keep=0,pop=op_pop[op],push=op_push[op];uint32_t left=TURBOJS_REGION_NO_VALUE,right=TURBOJS_REGION_NO_VALUE,v;
            if(local_index(op,bc->bytecode+off+1,&idx,&get,&keep)){
                if(idx>=g->local_count){st=fail(d,TURBOJS_IR_INVALID_REGISTER,off,"local index outside region state");goto done;}
                if(get){if(depth>=g->stack_stride){st=fail(d,TURBOJS_IR_INVALID_REGISTER,off,"region stack overflow");goto done;}stack[depth++]=locals[idx];}
                else {if(!depth){st=fail(d,TURBOJS_IR_INVALID_OPCODE,off,"region stack underflow");goto done;}locals[idx]=stack[depth-1];if(!keep)depth--;}
                continue;
            }
            if(arg_index(op,bc->bytecode+off+1,&idx)){
                if(ins_value[ino]==TURBOJS_REGION_NO_VALUE)ins_value[ino]=value_new(g,TURBOJS_REGION_VALUE_ARGUMENT,bidx,off,op,TURBOJS_REGION_NO_VALUE,TURBOJS_REGION_NO_VALUE,(uint16_t)idx,1);
                if(ins_value[ino]==TURBOJS_REGION_NO_VALUE){st=fail(d,TURBOJS_IR_OUT_OF_MEMORY,off,"unable to allocate argument value");goto done;}stack[depth++]=ins_value[ino];continue;
            }
            if(op==OP_dup){if(!depth){st=fail(d,TURBOJS_IR_INVALID_OPCODE,off,"region dup underflow");goto done;}stack[depth]=stack[depth-1];depth++;continue;}
            if(op==OP_drop){if(!depth){st=fail(d,TURBOJS_IR_INVALID_OPCODE,off,"region drop underflow");goto done;}depth--;continue;}
            if(op==OP_get_field){
                if(!depth){st=fail(d,TURBOJS_IR_INVALID_OPCODE,off,"region property load underflow");goto done;}
                if(ins_value[ino]==TURBOJS_REGION_NO_VALUE){
                    ins_value[ino]=value_new(g,TURBOJS_REGION_VALUE_HELPER,bidx,off,op,stack[depth-1],TURBOJS_REGION_NO_VALUE,0,1);
                    if(ins_value[ino]!=TURBOJS_REGION_NO_VALUE)g->values[ins_value[ino]].metadata=u32(bc->bytecode+off+1);
                }else g->values[ins_value[ino]].left=stack[depth-1];
                if(ins_value[ino]==TURBOJS_REGION_NO_VALUE){st=fail(d,TURBOJS_IR_OUT_OF_MEMORY,off,"unable to allocate property load");goto done;}
                stack[depth-1]=ins_value[ino];continue;
            }
            if(op==OP_put_field){
                if(depth<2){st=fail(d,TURBOJS_IR_INVALID_OPCODE,off,"region property store underflow");goto done;}
                if(ins_value[ino]==TURBOJS_REGION_NO_VALUE){
                    ins_value[ino]=value_new(g,TURBOJS_REGION_VALUE_HELPER,bidx,off,op,stack[depth-2],stack[depth-1],0,0);
                    if(ins_value[ino]!=TURBOJS_REGION_NO_VALUE)g->values[ins_value[ino]].metadata=u32(bc->bytecode+off+1);
                }else{g->values[ins_value[ino]].left=stack[depth-2];g->values[ins_value[ino]].right=stack[depth-1];}
                if(ins_value[ino]==TURBOJS_REGION_NO_VALUE){st=fail(d,TURBOJS_IR_OUT_OF_MEMORY,off,"unable to allocate property store");goto done;}
                depth-=2;continue;
            }
            if(op==OP_post_inc||op==OP_post_dec){uint32_t one,calc;if(!depth||depth>=g->stack_stride){st=fail(d,TURBOJS_IR_INVALID_OPCODE,off,"region post update stack error");goto done;}one=find_value(g,bidx,off,TURBOJS_REGION_VALUE_CONSTANT);if(one==TURBOJS_REGION_NO_VALUE)one=value_new(g,TURBOJS_REGION_VALUE_CONSTANT,bidx,off,OP_push_1,TURBOJS_REGION_NO_VALUE,TURBOJS_REGION_NO_VALUE,0,1);calc=find_value(g,bidx,off,TURBOJS_REGION_VALUE_OPERATION);if(calc==TURBOJS_REGION_NO_VALUE)calc=value_new(g,TURBOJS_REGION_VALUE_OPERATION,bidx,off,op==OP_post_inc?OP_add:OP_sub,stack[depth-1],one,0,1);else{g->values[calc].left=stack[depth-1];g->values[calc].right=one;}if(one==TURBOJS_REGION_NO_VALUE||calc==TURBOJS_REGION_NO_VALUE){st=fail(d,TURBOJS_IR_OUT_OF_MEMORY,off,"unable to allocate post update values");goto done;}stack[depth++]=calc;continue;}
            if(pop>=0){if(depth<(uint32_t)pop){st=fail(d,TURBOJS_IR_INVALID_OPCODE,off,"region operation underflow");goto done;}if(pop>0)right=stack[depth-1];if(pop>1)left=stack[depth-2];depth-=(uint32_t)pop;
                if(push>0){uint8_t kind=(op==OP_push_i32||op==OP_push_0||op==OP_push_1||op==OP_push_2||op==OP_push_3||op==OP_push_true||op==OP_push_false||op==OP_undefined||op==OP_null)?TURBOJS_REGION_VALUE_CONSTANT:((op==OP_add||op==OP_sub||op==OP_mul||op==OP_lt)?TURBOJS_REGION_VALUE_OPERATION:TURBOJS_REGION_VALUE_HELPER);
                    if(ins_value[ino]==TURBOJS_REGION_NO_VALUE)ins_value[ino]=value_new(g,kind,bidx,off,op,left,right,0,1);else{g->values[ins_value[ino]].left=left;g->values[ins_value[ino]].right=right;}
                    v=ins_value[ino];if(v==TURBOJS_REGION_NO_VALUE){st=fail(d,TURBOJS_IR_OUT_OF_MEMORY,off,"unable to allocate operation value");goto done;}stack[depth++]=v;
                }
            }
        }
        if(memcmp(xloc(g,bidx),locals,g->local_count*sizeof(uint32_t))){memcpy(xloc(g,bidx),locals,g->local_count*sizeof(uint32_t));changed=1;}
        if(memcmp(xstk(g,bidx),stack,g->stack_stride*sizeof(uint32_t))){memcpy(xstk(g,bidx),stack,g->stack_stride*sizeof(uint32_t));changed=1;}
        if(changed){uint32_t si;for(si=0;si<g->plan.cfg.blocks[bidx].successor_count;si++){uint32_t s=g->plan.cfg.blocks[bidx].successors[si],j;int sch=0;for(j=0;j<g->local_count;j++){uint32_t m=merge_slot(g,s,j,0,phi_map);if(eloc(g,s)[j]!=m){eloc(g,s)[j]=m;sch=1;}}for(j=0;j<g->plan.cfg.blocks[s].entry_stack_depth;j++){uint32_t m=merge_slot(g,s,j,1,phi_map);if(estk(g,s)[j]!=m){estk(g,s)[j]=m;sch=1;}}if(sch&&!queued[s]){if(qt>=g->plan.block_count*16u){st=fail(d,TURBOJS_IR_UNSUPPORTED,g->plan.cfg.blocks[s].start_offset,"region state did not converge");goto done;}queue[qt++]=s;queued[s]=1;}}}
    }
    for(bi=0;bi<g->value_count;bi++)if(g->values[bi].kind==TURBOJS_REGION_VALUE_PHI){TurboJSBytecodeRegionValue*v=&g->values[bi];const TurboJSBytecodeBlock*b=&g->plan.cfg.blocks[v->block];uint32_t pi;v->phi_input_start=(uint32_t)g->phi_input_count;if(!reserve_inputs(g,g->phi_input_count+b->predecessor_count)){st=fail(d,TURBOJS_IR_OUT_OF_MEMORY,b->start_offset,"unable to allocate phi inputs");goto done;}for(pi=0;pi<b->predecessor_count;pi++){uint32_t pred=b->predecessors[pi],val=v->is_stack_value?xstk(g,pred)[v->local_or_stack_index]:xloc(g,pred)[v->local_or_stack_index];g->phi_inputs[g->phi_input_count].predecessor_block=pred;g->phi_inputs[g->phi_input_count].value=val;g->phi_input_count++;v->phi_input_count++;}}
    if(d){d->status=TURBOJS_IR_OK;d->instruction_index=0;d->message=NULL;}st=TURBOJS_IR_OK;
done:free(phi_map);free(ins_value);free(queue);free(queued);if(st!=TURBOJS_IR_OK)TurboJS_EngineBytecodeRegionStateGraphDestroy(g);return st;
}
