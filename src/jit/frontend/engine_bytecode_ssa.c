#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "jit.h"

typedef enum RegionSSAOpcode {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) OP_##id,
#define def(id, size, n_pop, n_push, f)
#include "internal/bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
    OP_COUNT
} RegionSSAOpcode;

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
                            size_t at, const char *message) {
    if (d) { d->status=s; d->instruction_index=at; d->message=message; }
    return s;
}
static uint16_t read_u16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static int32_t read_i32(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}
static int local_index(uint8_t op,const uint8_t*p,uint32_t*out,int*is_get,int*keep){
    *keep=0;
    switch(op){
    case OP_get_loc:case OP_get_loc_check:*out=read_u16(p);*is_get=1;return 1;
    case OP_get_loc8:*out=p[0];*is_get=1;return 1;
    case OP_get_loc0:case OP_get_loc1:case OP_get_loc2:case OP_get_loc3:*out=op-OP_get_loc0;*is_get=1;return 1;
    case OP_put_loc:case OP_put_loc_check:*out=read_u16(p);*is_get=0;return 1;
    case OP_put_loc8:*out=p[0];*is_get=0;return 1;
    case OP_put_loc0:case OP_put_loc1:case OP_put_loc2:case OP_put_loc3:*out=op-OP_put_loc0;*is_get=0;return 1;
    case OP_set_loc:*out=read_u16(p);*is_get=0;*keep=1;return 1;
    case OP_set_loc8:*out=p[0];*is_get=0;*keep=1;return 1;
    case OP_set_loc0:case OP_set_loc1:case OP_set_loc2:case OP_set_loc3:*out=op-OP_set_loc0;*is_get=0;*keep=1;return 1;
    default:return 0;}
}
static int arg_index(uint8_t op,const uint8_t*p,uint32_t*out){
    if(op==OP_get_arg){*out=read_u16(p);return 1;}
    if(op>=OP_get_arg0&&op<=OP_get_arg3){*out=op-OP_get_arg0;return 1;}
    return 0;
}
static int append_value(TurboJSSSAGraph *g, uint32_t block, TurboJSSSAOpcode op,
                        TurboJSSSAType type, uint32_t left, uint32_t right,
                        int64_t immediate, uint32_t source, uint32_t *out) {
    TurboJSSSAValue *v;
    if (g->value_count == g->value_capacity) {
        size_t cap = g->value_capacity ? g->value_capacity * 2 : 32;
        void *p;
        if (cap < g->value_count || cap > SIZE_MAX / sizeof(*g->values)) return 0;
        p = realloc(g->values, cap * sizeof(*g->values)); if (!p) return 0;
        g->values = (TurboJSSSAValue *)p; g->value_capacity = cap;
    }
    v=&g->values[g->value_count]; memset(v,0,sizeof(*v));
    v->id=(uint32_t)g->value_count;v->block=block;v->opcode=op;v->type=type;
    v->left=left;v->right=right;v->immediate=immediate;v->source_instruction=source;
    if (out) *out=v->id;
    g->value_count++;
    g->blocks[block].value_count++;
    return 1;
}
static const TurboJSBytecodeRegionValue *value_at_offset(
    const TurboJSBytecodeRegionStateGraph *s, uint32_t block, uint32_t off) {
    size_t i;
    for(i=0;i<s->value_count;i++)
        if(s->values[i].block==block && s->values[i].source_offset==off &&
           s->values[i].kind==TURBOJS_REGION_VALUE_OPERATION)
            return &s->values[i];
    for(i=0;i<s->value_count;i++)
        if(s->values[i].block==block && s->values[i].source_offset==off &&
           s->values[i].kind!=TURBOJS_REGION_VALUE_PHI &&
           s->values[i].kind!=TURBOJS_REGION_VALUE_INITIAL_LOCAL)
            return &s->values[i];
    return NULL;
}
static TurboJSSSAOpcode map_operation(uint8_t op, TurboJSSSAType *type) {
    *type=TURBOJS_SSA_TYPE_INT64;
    switch(op){
    case OP_add:return TURBOJS_SSA_ADD_I64;
    case OP_sub:return TURBOJS_SSA_SUB_I64;
    case OP_mul:return TURBOJS_SSA_MUL_I64;
    case OP_and:return TURBOJS_SSA_AND_I64;
    case OP_or:return TURBOJS_SSA_OR_I64;
    case OP_xor:return TURBOJS_SSA_XOR_I64;
    case OP_shl:return TURBOJS_SSA_SHL_I64;
    case OP_sar:return TURBOJS_SSA_SAR_I64;
    case OP_shr:return TURBOJS_SSA_SHR_I64;
    case OP_lt:*type=TURBOJS_SSA_TYPE_BOOLEAN;return TURBOJS_SSA_LESS_THAN_I64;
    case OP_lte:*type=TURBOJS_SSA_TYPE_BOOLEAN;return TURBOJS_SSA_LESS_EQUAL_I64;
    case OP_gt:*type=TURBOJS_SSA_TYPE_BOOLEAN;return TURBOJS_SSA_GREATER_THAN_I64;
    case OP_gte:*type=TURBOJS_SSA_TYPE_BOOLEAN;return TURBOJS_SSA_GREATER_EQUAL_I64;
    case OP_eq:*type=TURBOJS_SSA_TYPE_BOOLEAN;return TURBOJS_SSA_EQUAL_I64;
    default:return TURBOJS_SSA_NOP;
    }
}
static int constant_immediate(const TurboJSEngineBytecodeInfo *bc,
                              const TurboJSBytecodeRegionValue *v, int64_t *imm) {
    const uint8_t *p=bc->bytecode+v->source_offset;
    switch(v->opcode){
    case OP_push_0:*imm=0;return 1;case OP_push_1:*imm=1;return 1;
    case OP_push_2:*imm=2;return 1;case OP_push_3:*imm=3;return 1;
    case OP_push_true:*imm=1;return 1;case OP_push_false:*imm=0;return 1;
    case OP_push_i32:*imm=read_i32(p+1);return 1;
    default:return 0;
    }
}

TurboJSIRStatus TurboJS_EngineBytecodeRegionBuildSSA(
    const TurboJSEngineBytecodeInfo *bc, TurboJSSSAGraph *ssa,
    TurboJSIRDiagnostic *d) {
    TurboJSBytecodeRegionStateGraph state; uint32_t *map=NULL; size_t bi,vi;
    TurboJSIRStatus st;
    if(!bc||!ssa)return fail(d,TURBOJS_IR_INVALID_ARGUMENT,0,"invalid region SSA input");
    memset(&state,0,sizeof(state));TurboJS_SSAGraphDestroy(ssa);TurboJS_SSAGraphInit(ssa);
    st=TurboJS_EngineBytecodeBuildRegionStateGraph(bc,&state,d);if(st!=TURBOJS_IR_OK)return st;
    ssa->blocks=(TurboJSSSABlock*)calloc(state.plan.block_count,sizeof(*ssa->blocks));
    map=(uint32_t*)malloc(state.value_count*sizeof(*map));
    if(!ssa->blocks||(!map&&state.value_count)){st=fail(d,TURBOJS_IR_OUT_OF_MEMORY,0,"unable to allocate region SSA");goto done;}
    ssa->block_count=ssa->block_capacity=state.plan.block_count;ssa->entry_block=0;
    for(vi=0;vi<state.value_count;vi++)map[vi]=TURBOJS_SSA_NO_VALUE;
    for(bi=0;bi<ssa->block_count;bi++){
        TurboJSSSABlock *b=&ssa->blocks[bi];const TurboJSBytecodeBlock *cb=&state.plan.cfg.blocks[bi];uint32_t e;
        b->id=(uint32_t)bi;b->first_instruction=cb->first_instruction;b->instruction_count=cb->instruction_count;
        b->first_value=(uint32_t)ssa->value_count;b->reachable=(cb->flags&TURBOJS_BYTECODE_BLOCK_REACHABLE)!=0;
        b->immediate_dominator=TURBOJS_SSA_NO_BLOCK;b->loop_header=TURBOJS_SSA_NO_BLOCK;
        b->predecessor_count=cb->predecessor_count;b->successor_count=cb->successor_count;
        for(e=0;e<cb->predecessor_count;e++)b->predecessors[e]=cb->predecessors[e];
        for(e=0;e<cb->successor_count;e++)b->successors[e]=cb->successors[e];
        for(vi=0;vi<state.value_count;vi++)if(state.values[vi].block==bi&&state.values[vi].kind==TURBOJS_REGION_VALUE_PHI){
            uint32_t id;if(state.values[vi].phi_input_count!=2){st=fail(d,TURBOJS_IR_UNSUPPORTED,state.values[vi].source_offset,"region SSA currently supports two-input phis");goto done;}
            if(!append_value(ssa,(uint32_t)bi,TURBOJS_SSA_PHI,TURBOJS_SSA_TYPE_INT64,
                             TURBOJS_SSA_NO_VALUE,TURBOJS_SSA_NO_VALUE,0,state.values[vi].source_offset,&id)){st=TURBOJS_IR_OUT_OF_MEMORY;goto done;}map[vi]=id;
        }
        for(vi=0;vi<state.value_count;vi++)if(state.values[vi].block==bi&&state.values[vi].kind!=TURBOJS_REGION_VALUE_PHI){
            const TurboJSBytecodeRegionValue *rv=&state.values[vi];TurboJSSSAOpcode op;TurboJSSSAType type=TURBOJS_SSA_TYPE_INT64;int64_t imm=0;uint32_t id;
            if(rv->kind==TURBOJS_REGION_VALUE_INITIAL_LOCAL)op=TURBOJS_SSA_NOP;
            else if(rv->kind==TURBOJS_REGION_VALUE_ARGUMENT){op=TURBOJS_SSA_ARGUMENT;imm=rv->local_or_stack_index;}
            else if(rv->kind==TURBOJS_REGION_VALUE_CONSTANT){if(!constant_immediate(bc,rv,&imm)){st=fail(d,TURBOJS_IR_UNSUPPORTED,rv->source_offset,"unsupported region constant");goto done;}op=TURBOJS_SSA_CONSTANT_I64;}
            else if(rv->kind==TURBOJS_REGION_VALUE_OPERATION)op=map_operation(rv->opcode,&type);
            else if(rv->kind==TURBOJS_REGION_VALUE_HELPER && rv->opcode==OP_object){op=TURBOJS_SSA_VIRTUAL_OBJECT;type=TURBOJS_SSA_TYPE_REFERENCE;}
            else if(rv->kind==TURBOJS_REGION_VALUE_HELPER && rv->opcode==OP_define_field){op=TURBOJS_SSA_VIRTUAL_FIELD_STORE;type=TURBOJS_SSA_TYPE_REFERENCE;imm=(int64_t)rv->metadata;}
            else if(rv->kind==TURBOJS_REGION_VALUE_HELPER && rv->opcode==OP_get_field){op=TURBOJS_SSA_PROPERTY_LOAD;type=TURBOJS_SSA_TYPE_UNKNOWN;imm=(int64_t)rv->metadata;}
            else if(rv->kind==TURBOJS_REGION_VALUE_HELPER && rv->opcode==OP_put_field){op=TURBOJS_SSA_PROPERTY_STORE;type=TURBOJS_SSA_TYPE_UNKNOWN;imm=(int64_t)rv->metadata;}
            else {st=fail(d,TURBOJS_IR_UNSUPPORTED,rv->source_offset,"runtime-helper value cannot enter direct SSA region");goto done;}
            if(op==TURBOJS_SSA_NOP&&rv->kind!=TURBOJS_REGION_VALUE_INITIAL_LOCAL){st=fail(d,TURBOJS_IR_UNSUPPORTED,rv->source_offset,"unsupported direct region operation");goto done;}
            if(!append_value(ssa,(uint32_t)bi,op,type,TURBOJS_SSA_NO_VALUE,TURBOJS_SSA_NO_VALUE,imm,rv->source_offset,&id)){st=TURBOJS_IR_OUT_OF_MEMORY;goto done;}
            ssa->values[id].metadata=rv->metadata;map[vi]=id;
        }
        /* Re-simulate symbolic local/stack state to emit the block terminator. */
        {
            uint32_t *locals=NULL,*stack=NULL,depth=cb->entry_stack_depth,ii;
            locals=(uint32_t*)malloc((state.local_count?state.local_count:1u)*sizeof(uint32_t));
            stack=(uint32_t*)malloc((state.stack_stride?state.stack_stride:1u)*sizeof(uint32_t));
            if(!locals||!stack){free(locals);free(stack);st=fail(d,TURBOJS_IR_OUT_OF_MEMORY,cb->start_offset,"unable to allocate region SSA block state");goto done;}
            memcpy(locals,state.entry_locals+state.blocks[bi].entry_local_offset,state.local_count*sizeof(uint32_t));
            memcpy(stack,state.entry_stack+state.blocks[bi].entry_stack_offset,state.stack_stride*sizeof(uint32_t));
            for(ii=0;ii<cb->instruction_count;ii++){
                uint32_t ino=cb->first_instruction+ii,off=state.plan.cfg.instruction_offsets[ino],idx;uint8_t opbc=bc->bytecode[off];int get=0,keep=0,pop=op_pop[opbc],push=op_push[opbc];const TurboJSBytecodeRegionValue *rv;
                if(local_index(opbc,bc->bytecode+off+1,&idx,&get,&keep)){if(get)stack[depth++]=locals[idx];else{locals[idx]=stack[depth-1];if(!keep)depth--;}continue;}
                if(arg_index(opbc,bc->bytecode+off+1,&idx)){rv=value_at_offset(&state,(uint32_t)bi,off);stack[depth++]=rv?rv->id:TURBOJS_REGION_NO_VALUE;continue;}
                if(opbc==OP_dup){stack[depth]=stack[depth-1];depth++;continue;}if(opbc==OP_drop){depth--;continue;}
                if(opbc==OP_post_inc||opbc==OP_post_dec){rv=value_at_offset(&state,(uint32_t)bi,off);if(!rv||depth>=state.stack_stride){st=fail(d,TURBOJS_IR_UNSUPPORTED,off,"missing symbolic post update value");goto done;}stack[depth++]=rv->id;continue;}
                if(opbc==OP_if_false||opbc==OP_if_true||opbc==OP_if_false8||opbc==OP_if_true8){uint32_t cond=depth?stack[depth-1]:TURBOJS_REGION_NO_VALUE,id;TurboJSSSAOpcode sop=(opbc==OP_if_true||opbc==OP_if_true8)?TURBOJS_SSA_BRANCH_TRUE:TURBOJS_SSA_BRANCH_FALSE;if(!append_value(ssa,(uint32_t)bi,sop,TURBOJS_SSA_TYPE_BOOLEAN,cond,TURBOJS_SSA_NO_VALUE,cb->successors[0],off,&id)){st=TURBOJS_IR_OUT_OF_MEMORY;goto done;}depth--;continue;}
                if(opbc==OP_goto||opbc==OP_goto8||opbc==OP_goto16){uint32_t id;if(!append_value(ssa,(uint32_t)bi,TURBOJS_SSA_JUMP,TURBOJS_SSA_TYPE_UNKNOWN,TURBOJS_SSA_NO_VALUE,TURBOJS_SSA_NO_VALUE,cb->successors[0],off,&id)){st=TURBOJS_IR_OUT_OF_MEMORY;goto done;}continue;}
                if(opbc==OP_return){uint32_t val=depth?stack[depth-1]:TURBOJS_REGION_NO_VALUE,id;if(!append_value(ssa,(uint32_t)bi,TURBOJS_SSA_RETURN,TURBOJS_SSA_TYPE_INT64,val,TURBOJS_SSA_NO_VALUE,0,off,&id)){st=TURBOJS_IR_OUT_OF_MEMORY;goto done;}depth--;continue;}
                if(pop>=0){if((uint32_t)pop>depth){free(locals);free(stack);st=fail(d,TURBOJS_IR_INVALID_OPCODE,off,"region SSA stack underflow");goto done;}depth-=(uint32_t)pop;if(push>0){rv=value_at_offset(&state,(uint32_t)bi,off);if(!rv){free(locals);free(stack);st=fail(d,TURBOJS_IR_UNSUPPORTED,off,"missing symbolic operation value");goto done;}stack[depth++]=rv->id;}}
            }
            free(locals);free(stack);
        }
    }
    /* Resolve symbolic operands after all values, including backedges, exist. */
    for(vi=0;vi<state.value_count;vi++){
        const TurboJSBytecodeRegionValue *rv=&state.values[vi];TurboJSSSAValue *v;
        if(map[vi]==TURBOJS_SSA_NO_VALUE) continue;
        v=&ssa->values[map[vi]];
        if(rv->kind==TURBOJS_REGION_VALUE_PHI){v->left=map[state.phi_inputs[rv->phi_input_start].value];v->right=map[state.phi_inputs[rv->phi_input_start+1].value];}
        else if(rv->kind==TURBOJS_REGION_VALUE_OPERATION||rv->kind==TURBOJS_REGION_VALUE_HELPER){v->left=rv->left==TURBOJS_REGION_NO_VALUE?TURBOJS_SSA_NO_VALUE:map[rv->left];v->right=rv->right==TURBOJS_REGION_NO_VALUE?TURBOJS_SSA_NO_VALUE:map[rv->right];}
    }
    /* Resolve control operands which were stored as symbolic IDs. */
    for(vi=0;vi<ssa->value_count;vi++){
        TurboJSSSAValue *v=&ssa->values[vi];
        if(v->opcode==TURBOJS_SSA_BRANCH_TRUE||v->opcode==TURBOJS_SSA_BRANCH_FALSE||v->opcode==TURBOJS_SSA_RETURN)
            if(v->left!=TURBOJS_SSA_NO_VALUE)v->left=map[v->left];
    }
    for(vi=0;vi<ssa->value_count;vi++){TurboJSSSAValue*v=&ssa->values[vi];if(v->left!=TURBOJS_SSA_NO_VALUE)ssa->values[v->left].use_count++;if(v->right!=TURBOJS_SSA_NO_VALUE)ssa->values[v->right].use_count++;}
    TurboJS_SSAComputeDominators(ssa);TurboJS_SSAComputeDominanceFrontiers(ssa);TurboJS_SSADetectLoops(ssa);
    if(!TurboJS_SSAVerify(ssa)){st=fail(d,TURBOJS_IR_INVALID_OPCODE,0,"emitted region SSA failed verification");goto done;}
    if(d){d->status=TURBOJS_IR_OK;d->instruction_index=0;d->message=NULL;}st=TURBOJS_IR_OK;
done:
    free(map);TurboJS_EngineBytecodeRegionStateGraphDestroy(&state);
    if(st!=TURBOJS_IR_OK)TurboJS_SSAGraphDestroy(ssa);
    return st;
}
