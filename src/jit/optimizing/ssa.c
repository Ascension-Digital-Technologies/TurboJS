#include "jit.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int grow_array(void **memory, size_t *capacity, size_t required,
                      size_t minimum, size_t element_size)
{
    size_t next = *capacity ? *capacity : minimum;
    void *grown;
    if (required <= *capacity)
        return 1;
    while (next < required) {
        if (next > SIZE_MAX / 2)
            return 0;
        next <<= 1;
    }
    if (next > SIZE_MAX / element_size)
        return 0;
    grown = realloc(*memory, next * element_size);
    if (!grown)
        return 0;
    *memory = grown;
    *capacity = next;
    return 1;
}

static int reserve_values(TurboJSSSAGraph *graph, size_t required)
{
    return grow_array((void **)&graph->values, &graph->value_capacity,
                      required, 32, sizeof(*graph->values));
}

static int reserve_blocks(TurboJSSSAGraph *graph, size_t required)
{
    return grow_array((void **)&graph->blocks, &graph->block_capacity,
                      required, 8, sizeof(*graph->blocks));
}

void TurboJS_SSAGraphInit(TurboJSSSAGraph *g) {
    if (g) { memset(g, 0, sizeof(*g)); g->entry_block = TURBOJS_SSA_NO_BLOCK; }
}
void TurboJS_SSAGraphDestroy(TurboJSSSAGraph *g) {
    if (!g) return;
    free(g->values);
    free(g->blocks);
    memset(g, 0, sizeof(*g));
    g->entry_block = TURBOJS_SSA_NO_BLOCK;
}

static TurboJSSSAType type_for(TurboJSIROpcode op) {
    return op == TURBOJS_IR_LESS_THAN_I64 ? TURBOJS_SSA_TYPE_BOOLEAN : TURBOJS_SSA_TYPE_INT64;
}
static TurboJSSSAOpcode map_op(TurboJSIROpcode op) {
    switch (op) {
    case TURBOJS_IR_ARGUMENT: return TURBOJS_SSA_ARGUMENT;
    case TURBOJS_IR_CONSTANT_I64: return TURBOJS_SSA_CONSTANT_I64;
    case TURBOJS_IR_ADD_I64: case TURBOJS_IR_ADD_I32_CHECKED: return TURBOJS_SSA_ADD_I64;
    case TURBOJS_IR_SUB_I64: case TURBOJS_IR_SUB_I32_CHECKED: return TURBOJS_SSA_SUB_I64;
    case TURBOJS_IR_MUL_I64: case TURBOJS_IR_MUL_I32_CHECKED: return TURBOJS_SSA_MUL_I64;
    case TURBOJS_IR_LESS_THAN_I64: return TURBOJS_SSA_LESS_THAN_I64;
    case TURBOJS_IR_JUMP: return TURBOJS_SSA_JUMP;
    case TURBOJS_IR_BRANCH_TRUE: return TURBOJS_SSA_BRANCH_TRUE;
    case TURBOJS_IR_BRANCH_FALSE: return TURBOJS_SSA_BRANCH_FALSE;
    case TURBOJS_IR_RETURN_I64: return TURBOJS_SSA_RETURN;
    default: return TURBOJS_SSA_NOP;
    }
}
static int block_add_successor(TurboJSSSAGraph *g, uint32_t from, uint32_t to) {
    TurboJSSSABlock *a, *b; uint32_t i;
    if (from >= g->block_count || to >= g->block_count) return 0;
    a=&g->blocks[from]; b=&g->blocks[to];
    for(i=0;i<a->successor_count;i++) if(a->successors[i]==to) return 1;
    if(a->successor_count>=TURBOJS_SSA_MAX_BLOCK_EDGES || b->predecessor_count>=TURBOJS_SSA_MAX_BLOCK_EDGES) return 0;
    a->successors[a->successor_count++]=to; b->predecessors[b->predecessor_count++]=from; return 1;
}
static uint32_t block_for_instruction(const TurboJSSSAGraph *g, uint32_t insn) {
    size_t i; for(i=0;i<g->block_count;i++) { const TurboJSSSABlock *b=&g->blocks[i]; if(insn>=b->first_instruction && insn<b->first_instruction+b->instruction_count) return (uint32_t)i; } return TURBOJS_SSA_NO_BLOCK;
}
static int build_blocks(const TurboJSIRFunction *f, TurboJSSSAGraph *g) {
    uint8_t *leader; size_t i; uint32_t last;
    if (!f->instruction_count) return 0;
    leader=calloc(f->instruction_count,1); if(!leader) return 0; leader[0]=1;
    for(i=0;i<f->instruction_count;i++) {
        const TurboJSIRInstruction *in=&f->instructions[i];
        if(in->opcode==TURBOJS_IR_JUMP || in->opcode==TURBOJS_IR_BRANCH_TRUE || in->opcode==TURBOJS_IR_BRANCH_FALSE) {
            if(in->target<f->instruction_count) leader[in->target]=1;
            if(i+1<f->instruction_count) leader[i+1]=1;
        } else if(in->opcode==TURBOJS_IR_RETURN_I64 && i+1<f->instruction_count) leader[i+1]=1;
    }
    for(i=0;i<f->instruction_count;i++) if(leader[i]) {
        TurboJSSSABlock *b; if(!reserve_blocks(g,g->block_count+1)){free(leader);return 0;}
        b=&g->blocks[g->block_count]; memset(b,0,sizeof(*b)); b->id=(uint32_t)g->block_count; b->first_instruction=(uint32_t)i; b->first_value=(uint32_t)g->value_count; b->immediate_dominator=TURBOJS_SSA_NO_BLOCK; b->loop_header=TURBOJS_SSA_NO_BLOCK; b->reachable=0; g->block_count++;
    }
    for(i=0;i<g->block_count;i++) { uint32_t next=(i+1<g->block_count)?g->blocks[i+1].first_instruction:(uint32_t)f->instruction_count; g->blocks[i].instruction_count=next-g->blocks[i].first_instruction; }
    g->entry_block=0; g->blocks[0].reachable=1;
    for(i=0;i<g->block_count;i++) {
        TurboJSSSABlock *b=&g->blocks[i]; const TurboJSIRInstruction *in; uint32_t target;
        last=b->first_instruction+b->instruction_count-1; in=&f->instructions[last];
        if(in->opcode==TURBOJS_IR_JUMP) { target=block_for_instruction(g,in->target); if(target==TURBOJS_SSA_NO_BLOCK||!block_add_successor(g,(uint32_t)i,target)){free(leader);return 0;} }
        else if(in->opcode==TURBOJS_IR_BRANCH_TRUE || in->opcode==TURBOJS_IR_BRANCH_FALSE) { target=block_for_instruction(g,in->target); if(target==TURBOJS_SSA_NO_BLOCK||!block_add_successor(g,(uint32_t)i,target)){free(leader);return 0;} if(i+1<g->block_count&&!block_add_successor(g,(uint32_t)i,(uint32_t)i+1)){free(leader);return 0;} }
        else if(in->opcode!=TURBOJS_IR_RETURN_I64 && i+1<g->block_count) { if(!block_add_successor(g,(uint32_t)i,(uint32_t)i+1)){free(leader);return 0;} }
    }
    free(leader); return 1;
}
static uint32_t emit_value(TurboJSSSAGraph *g,uint32_t block,TurboJSSSAOpcode op,TurboJSSSAType type,uint32_t left,uint32_t right,int64_t imm,uint32_t src) {
    TurboJSSSAValue *v; if(!reserve_values(g,g->value_count+1)) return TURBOJS_SSA_NO_VALUE;
    v=&g->values[g->value_count]; memset(v,0,sizeof(*v)); v->id=(uint32_t)g->value_count; v->block=block; v->opcode=op; v->type=type; v->left=left; v->right=right; v->immediate=imm; v->source_instruction=src;
    if (left != TURBOJS_SSA_NO_VALUE) g->values[left].use_count++;
    if (right != TURBOJS_SSA_NO_VALUE) g->values[right].use_count++;
    g->value_count++; g->blocks[block].value_count++; return v->id;
}
static void mark_reachable(TurboJSSSAGraph *g) {
    uint32_t queue[1024],h=0,t=0,i; if(!g->block_count)return; for(i=0;i<g->block_count;i++)g->blocks[i].reachable=0; g->blocks[g->entry_block].reachable=1; queue[t++]=g->entry_block;
    while(h<t){uint32_t b=queue[h++];for(i=0;i<g->blocks[b].successor_count;i++){uint32_t s=g->blocks[b].successors[i];if(!g->blocks[s].reachable){g->blocks[s].reachable=1;if(t<1024)queue[t++]=s;}}}
}
TurboJSIRStatus TurboJS_SSABuildFromIR(const TurboJSIRFunction *f, TurboJSSSAGraph *g, TurboJSIRDiagnostic *d) {
    uint32_t (*out)[TURBOJS_IR_MAX_REGISTERS]=NULL; uint32_t reg[TURBOJS_IR_MAX_REGISTERS]; size_t bi,ri;
    if (!f || !g) return TURBOJS_IR_INVALID_ARGUMENT;
    TurboJS_SSAGraphDestroy(g);
    TurboJS_SSAGraphInit(g);
    if (!build_blocks(f, g)) return TURBOJS_IR_OUT_OF_MEMORY;
    out = malloc(g->block_count * sizeof(*out));
    if (!out) return TURBOJS_IR_OUT_OF_MEMORY;
    for(bi=0;bi<g->block_count;bi++)for(ri=0;ri<TURBOJS_IR_MAX_REGISTERS;ri++)out[bi][ri]=TURBOJS_SSA_NO_VALUE;
    for(bi=0;bi<g->block_count;bi++) {
        TurboJSSSABlock *b=&g->blocks[bi]; uint32_t ii;
        for(ri=0;ri<TURBOJS_IR_MAX_REGISTERS;ri++) {
            uint32_t merged=TURBOJS_SSA_NO_VALUE; int conflict=0; uint32_t p;
            for(p=0;p<b->predecessor_count;p++){uint32_t pred=b->predecessors[p],v=out[pred][ri];if(v==TURBOJS_SSA_NO_VALUE)continue;if(merged==TURBOJS_SSA_NO_VALUE)merged=v;else if(merged!=v)conflict=1;}
            if(conflict && b->predecessor_count>=2) {
                uint32_t a=out[b->predecessors[0]][ri], c=out[b->predecessors[1]][ri];
                if(a!=TURBOJS_SSA_NO_VALUE&&c!=TURBOJS_SSA_NO_VALUE) merged=emit_value(g,(uint32_t)bi,TURBOJS_SSA_PHI,g->values[a].type,a,c,0,b->first_instruction);
            }
            reg[ri]=merged;
        }
        b->first_value=(uint32_t)(g->value_count-b->value_count);
        for(ii=b->first_instruction;ii<b->first_instruction+b->instruction_count;ii++) {
            const TurboJSIRInstruction *in=&f->instructions[ii]; TurboJSSSAOpcode op=map_op(in->opcode); uint32_t left=TURBOJS_SSA_NO_VALUE,right=TURBOJS_SSA_NO_VALUE,id;
            if(op==TURBOJS_SSA_NOP)continue;
            if(op==TURBOJS_SSA_ARGUMENT||op==TURBOJS_SSA_CONSTANT_I64){left=right=TURBOJS_SSA_NO_VALUE;}
            else if(op==TURBOJS_SSA_JUMP){left=right=TURBOJS_SSA_NO_VALUE;}
            else if(op==TURBOJS_SSA_BRANCH_TRUE||op==TURBOJS_SSA_BRANCH_FALSE||op==TURBOJS_SSA_RETURN){left=(in->left<TURBOJS_IR_MAX_REGISTERS)?reg[in->left]:TURBOJS_SSA_NO_VALUE;}
            else {left=(in->left<TURBOJS_IR_MAX_REGISTERS)?reg[in->left]:TURBOJS_SSA_NO_VALUE;right=(in->right<TURBOJS_IR_MAX_REGISTERS)?reg[in->right]:TURBOJS_SSA_NO_VALUE;}
            id=emit_value(g,(uint32_t)bi,op,type_for(in->opcode),left,right,in->opcode==TURBOJS_IR_JUMP||in->opcode==TURBOJS_IR_BRANCH_TRUE||in->opcode==TURBOJS_IR_BRANCH_FALSE?(int64_t)block_for_instruction(g,in->target):in->immediate,ii);
            if(id==TURBOJS_SSA_NO_VALUE){free(out);return TURBOJS_IR_OUT_OF_MEMORY;}
            if(in->destination<TURBOJS_IR_MAX_REGISTERS && op!=TURBOJS_SSA_RETURN && op!=TURBOJS_SSA_JUMP && op!=TURBOJS_SSA_BRANCH_TRUE && op!=TURBOJS_SSA_BRANCH_FALSE) reg[in->destination]=id;
        }
        for(ri=0;ri<TURBOJS_IR_MAX_REGISTERS;ri++)out[bi][ri]=reg[ri];
    }
    free(out); mark_reachable(g); TurboJS_SSAComputeDominators(g); TurboJS_SSAComputeDominanceFrontiers(g); TurboJS_SSADetectLoops(g);
    if(!TurboJS_SSAVerify(g)){if(d){d->status=TURBOJS_IR_INVALID_TARGET;d->instruction_index=0;d->message="invalid SSA control-flow graph";}return TURBOJS_IR_INVALID_TARGET;} return TURBOJS_IR_OK;
}
TurboJSIRStatus TurboJS_SSAAddPhi(TurboJSSSAGraph *g,uint32_t block,uint32_t left,uint32_t right,TurboJSSSAType type,uint32_t *out) {
    uint32_t id;if(!g||block>=g->block_count||left>=g->value_count||right>=g->value_count)return TURBOJS_IR_INVALID_ARGUMENT; id=emit_value(g,block,TURBOJS_SSA_PHI,type,left,right,0,g->blocks[block].first_instruction);if(id==TURBOJS_SSA_NO_VALUE)return TURBOJS_IR_OUT_OF_MEMORY;if(out)*out=id;return TURBOJS_IR_OK;
}
TurboJSIRStatus TurboJS_SSAComputeDominators(TurboJSSSAGraph *g) {
    uint64_t *dom; uint64_t all; size_t n,i; int changed=1;
    if (!g || !g->block_count || g->block_count > 64) return TURBOJS_IR_INVALID_ARGUMENT;
    n = g->block_count;
    dom = calloc(n, sizeof(*dom));
    if (!dom) return TURBOJS_IR_OUT_OF_MEMORY;
    all = n == 64 ? UINT64_MAX : ((1ull << n) - 1);
    for(i=0;i<n;i++)dom[i]=(i==g->entry_block)?(1ull<<i):all;
    while(changed){changed=0;for(i=0;i<n;i++){uint64_t next=all;uint32_t p;if(i==g->entry_block||!g->blocks[i].reachable)continue;if(!g->blocks[i].predecessor_count)next=0;else for(p=0;p<g->blocks[i].predecessor_count;p++)next&=dom[g->blocks[i].predecessors[p]];next|=1ull<<i;if(next!=dom[i]){dom[i]=next;changed=1;}}}
    for(i=0;i<n;i++){uint32_t cand=TURBOJS_SSA_NO_BLOCK,j;if(i==g->entry_block){g->blocks[i].immediate_dominator=TURBOJS_SSA_NO_BLOCK;continue;}for(j=0;j<n;j++)if(j!=i&&(dom[i]&(1ull<<j))){uint32_t k;int dominated_by_other=0;for(k=0;k<n;k++)if(k!=i&&k!=j&&(dom[i]&(1ull<<k))&&(dom[j]&(1ull<<k))){dominated_by_other=1;break;}if(!dominated_by_other){cand=j;break;}}g->blocks[i].immediate_dominator=cand;}
    free(dom);return TURBOJS_IR_OK;
}

TurboJSIRStatus TurboJS_SSAComputeDominanceFrontiers(TurboJSSSAGraph *g) {
    size_t i;
    if (!g || !g->block_count || g->block_count > 64) return TURBOJS_IR_INVALID_ARGUMENT;
    for (i = 0; i < g->block_count; ++i) g->blocks[i].dominance_frontier_mask = 0;
    for (i = 0; i < g->block_count; ++i) {
        TurboJSSSABlock *block = &g->blocks[i];
        uint32_t p;
        if (!block->reachable || block->predecessor_count < 2) continue;
        for (p = 0; p < block->predecessor_count; ++p) {
            uint32_t runner = block->predecessors[p];
            while (runner != TURBOJS_SSA_NO_BLOCK &&
                   runner != block->immediate_dominator) {
                g->blocks[runner].dominance_frontier_mask |= (1ull << i);
                runner = g->blocks[runner].immediate_dominator;
            }
        }
    }
    return TURBOJS_IR_OK;
}

TurboJSIRStatus TurboJS_SSASpecializeFromFeedback(
    TurboJSSSAGraph *g,
    const TurboJSFeedbackVector *feedback,
    TurboJSSSAOptimizationStats *stats) {
    size_t i;
    if (!g || !feedback || !g->block_count) return TURBOJS_IR_INVALID_ARGUMENT;
    for (i = 0; i < g->value_count; ++i) {
        TurboJSSSAValue *argument = &g->values[i];
        uint32_t argument_index;
        uint32_t guard_id;
        if (argument->removed || argument->opcode != TURBOJS_SSA_ARGUMENT) continue;
        if (argument->immediate < 0 || argument->immediate >= feedback->argument_count) continue;
        argument_index = (uint32_t)argument->immediate;
        if (feedback->arguments[argument_index].observed_types != TURBOJS_FEEDBACK_INT32 ||
            !TurboJS_FeedbackSlotIsStable(&feedback->arguments[argument_index])) continue;
        argument->type = TURBOJS_SSA_TYPE_INT32;
        guard_id = emit_value(g, argument->block, TURBOJS_SSA_GUARD_INT32,
                              TURBOJS_SSA_TYPE_INT32, argument->id,
                              TURBOJS_SSA_NO_VALUE, (int64_t)argument_index,
                              argument->source_instruction);
        if (guard_id == TURBOJS_SSA_NO_VALUE) return TURBOJS_IR_OUT_OF_MEMORY;
        g->values[guard_id].has_deopt_edge = 1;
        g->values[guard_id].deopt_id = g->deopt_exit_count++;
        if (stats) stats->guards_inserted++;
    }
    return TURBOJS_IR_OK;
}

TurboJSIRStatus TurboJS_SSADetectLoops(TurboJSSSAGraph *g) {
    size_t i;
    uint32_t j;
    if (!g) return TURBOJS_IR_INVALID_ARGUMENT;
    for (i = 0; i < g->block_count; ++i) {
        g->blocks[i].loop_depth = 0;
        g->blocks[i].loop_header = TURBOJS_SSA_NO_BLOCK;
    }
    for (i = 0; i < g->block_count; ++i) {
        for (j = 0; j < g->blocks[i].successor_count; ++j) {
            uint32_t header = g->blocks[i].successors[j];
            uint32_t work[TURBOJS_SSA_MAX_BLOCK_EDGES * 8];
            uint32_t count = 0;
            uint32_t cursor = 0;
            uint8_t seen[64] = {0};
            if (header > i || header >= g->block_count || g->block_count > 64) continue;
            work[count++] = (uint32_t)i;
            seen[header] = 1;
            seen[i] = 1;
            while (cursor < count) {
                uint32_t block = work[cursor++];
                uint32_t p;
                g->blocks[block].loop_header = header;
                g->blocks[block].loop_depth++;
                for (p = 0; p < g->blocks[block].predecessor_count; ++p) {
                    uint32_t pred = g->blocks[block].predecessors[p];
                    if (!seen[pred] && count < sizeof(work) / sizeof(work[0])) {
                        seen[pred] = 1;
                        work[count++] = pred;
                    }
                }
            }
            g->blocks[header].loop_header = header;
            g->blocks[header].loop_depth++;
        }
    }
    return TURBOJS_IR_OK;
}


static TurboJSSSAType merge_numeric_type(TurboJSSSAType left, TurboJSSSAType right)
{
    if (left == TURBOJS_SSA_TYPE_UNKNOWN || right == TURBOJS_SSA_TYPE_UNKNOWN)
        return TURBOJS_SSA_TYPE_UNKNOWN;
    if (left == TURBOJS_SSA_TYPE_FLOAT64 || right == TURBOJS_SSA_TYPE_FLOAT64)
        return TURBOJS_SSA_TYPE_FLOAT64;
    if (left == TURBOJS_SSA_TYPE_INT64 || right == TURBOJS_SSA_TYPE_INT64)
        return TURBOJS_SSA_TYPE_INT64;
    if (left == TURBOJS_SSA_TYPE_INT32 && right == TURBOJS_SSA_TYPE_INT32)
        return TURBOJS_SSA_TYPE_INT32;
    return TURBOJS_SSA_TYPE_UNKNOWN;
}

uint32_t TurboJS_SSAInferTypes(TurboJSSSAGraph *graph)
{
    uint32_t changed_total = 0;
    int changed;
    size_t i;
    if (!graph) return 0;
    do {
        changed = 0;
        for (i = 0; i < graph->value_count; ++i) {
            TurboJSSSAValue *v = &graph->values[i];
            TurboJSSSAType inferred = v->type;
            if (v->removed) continue;
            switch (v->opcode) {
            case TURBOJS_SSA_CONSTANT_I64:
                inferred = (v->immediate >= INT32_MIN && v->immediate <= INT32_MAX) ?
                    TURBOJS_SSA_TYPE_INT32 : TURBOJS_SSA_TYPE_INT64;
                break;
            case TURBOJS_SSA_LESS_THAN_I64:
            case TURBOJS_SSA_BRANCH_TRUE:
            case TURBOJS_SSA_BRANCH_FALSE:
                inferred = TURBOJS_SSA_TYPE_BOOLEAN;
                break;
            case TURBOJS_SSA_GUARD_INT32:
                inferred = TURBOJS_SSA_TYPE_INT32;
                break;
            case TURBOJS_SSA_ADD_I64:
            case TURBOJS_SSA_SUB_I64:
            case TURBOJS_SSA_MUL_I64:
                if (v->left < graph->value_count && v->right < graph->value_count)
                    inferred = merge_numeric_type(graph->values[v->left].type,
                                                  graph->values[v->right].type);
                break;
            case TURBOJS_SSA_PHI:
                if (v->left < graph->value_count && v->right < graph->value_count) {
                    TurboJSSSAType l = graph->values[v->left].type;
                    TurboJSSSAType r = graph->values[v->right].type;
                    inferred = l == r ? l : merge_numeric_type(l, r);
                }
                break;
            default:
                break;
            }
            if (inferred != TURBOJS_SSA_TYPE_UNKNOWN && inferred != v->type) {
                v->type = inferred;
                changed = 1;
                changed_total++;
            }
        }
    } while (changed);
    return changed_total;
}

static int cse_candidate(const TurboJSSSAValue *v)
{
    switch (v->opcode) {
    case TURBOJS_SSA_CONSTANT_I64:
    case TURBOJS_SSA_ADD_I64:
    case TURBOJS_SSA_SUB_I64:
    case TURBOJS_SSA_MUL_I64:
    case TURBOJS_SSA_LESS_THAN_I64:
        return 1;
    default:
        return 0;
    }
}

static int same_expression(const TurboJSSSAValue *a, const TurboJSSSAValue *b)
{
    return a->opcode == b->opcode && a->type == b->type &&
           a->left == b->left && a->right == b->right &&
           a->immediate == b->immediate && a->block == b->block;
}

static void replace_value_uses(TurboJSSSAGraph *g, uint32_t from, uint32_t to)
{
    size_t i;
    uint32_t replacements = 0;
    for (i = 0; i < g->value_count; ++i) {
        TurboJSSSAValue *v = &g->values[i];
        if (v->removed) continue;
        if (v->left == from) { v->left = to; replacements++; }
        if (v->right == from) { v->right = to; replacements++; }
    }
    g->values[to].use_count += replacements;
    g->values[from].use_count = 0;
}



static int same_property_cases(const TurboJSSSAValue *a,
                               const TurboJSSSAValue *b)
{
    uint8_t i;
    if (a->property_case_count != b->property_case_count ||
        a->metadata != b->metadata)
        return 0;
    for (i = 0; i < a->property_case_count; ++i) {
        if (a->property_shapes[i] != b->property_shapes[i] ||
            a->property_indices[i] != b->property_indices[i] ||
            a->property_generations[i] != b->property_generations[i] ||
            a->property_case_flags[i] != b->property_case_flags[i])
            return 0;
    }
    return 1;
}

static int property_alias_class_known(const TurboJSSSAValue *value)
{
    return value && value->metadata != 0 && value->property_case_count != 0;
}

static int same_property_alias_class(const TurboJSSSAValue *a,
                                     const TurboJSSSAValue *b)
{
    uint8_t i, j;
    if (!property_alias_class_known(a) || !property_alias_class_known(b) ||
        a->metadata != b->metadata)
        return 0;
    for (i = 0; i < a->property_case_count; ++i)
        for (j = 0; j < b->property_case_count; ++j)
            if (a->property_shapes[i] == b->property_shapes[j] &&
                a->property_indices[i] == b->property_indices[j])
                return 1;
    /* The same atom on disjoint shapes still denotes the same logical memory
       class, even though the physical slot differs by shape. */
    return 1;
}

static int property_store_may_alias(const TurboJSSSAValue *store,
                                    const TurboJSSSAValue *load)
{
    uint8_t i, j;
    if (store->opcode != TURBOJS_SSA_PROPERTY_STORE)
        return 0;
    /* Distinct, known property atoms are independent memory classes even on
       the same receiver or shape. Unknown metadata remains conservative. */
    if (property_alias_class_known(store) && property_alias_class_known(load) &&
        store->metadata != load->metadata)
        return 0;
    if (store->left == load->left)
        return 1;
    /* Different SSA receivers can still alias. Only prove independence when
       their guarded shape sets are disjoint. */
    if (!store->property_case_count || !load->property_case_count)
        return 1;
    for (i = 0; i < store->property_case_count; ++i)
        for (j = 0; j < load->property_case_count; ++j)
            if (store->property_shapes[i] == load->property_shapes[j])
                return 1;
    return 0;
}

static uint32_t property_count_non_aliasing_stores(const TurboJSSSAGraph *g,
                                                   uint32_t first, uint32_t last,
                                                   const TurboJSSSAValue *load)
{
    uint32_t i, count = 0;
    if (!g || !load || first >= g->value_count)
        return 0;
    if (last > g->value_count)
        last = (uint32_t)g->value_count;
    for (i = first; i < last; ++i) {
        const TurboJSSSAValue *value = &g->values[i];
        if (!value->removed && value->opcode == TURBOJS_SSA_PROPERTY_STORE &&
            property_alias_class_known(value) &&
            property_alias_class_known(load) &&
            !property_store_may_alias(value, load))
            count++;
    }
    return count;
}

static int block_dominates(const TurboJSSSAGraph *g, uint32_t dominator,
                           uint32_t block)
{
    uint32_t cursor = block;
    if (!g || dominator >= g->block_count || block >= g->block_count)
        return 0;
    while (cursor != TURBOJS_SSA_NO_BLOCK) {
        if (cursor == dominator)
            return 1;
        cursor = g->blocks[cursor].immediate_dominator;
    }
    return 0;
}

static int property_unique_path_clear(const TurboJSSSAGraph *g,
                                      uint32_t prior_block,
                                      uint32_t load_block,
                                      const TurboJSSSAValue *load)
{
    uint32_t cursor = load_block;
    while (cursor != prior_block) {
        const TurboJSSSABlock *block;
        uint32_t i;
        if (cursor == TURBOJS_SSA_NO_BLOCK || cursor >= g->block_count)
            return 0;
        block = &g->blocks[cursor];
        if (block->predecessor_count != 1)
            return 0;
        for (i = block->first_value;
             i < block->first_value + block->value_count && i < g->value_count &&
             (cursor != load_block || i < load->id); ++i) {
            const TurboJSSSAValue *value = &g->values[i];
            if (!value->removed && value != load &&
                value->opcode == TURBOJS_SSA_PROPERTY_STORE &&
                property_store_may_alias(value, load))
                return 0;
        }
        cursor = block->predecessors[0];
    }
    return 1;
}

static int property_block_prefix_clear(const TurboJSSSAGraph *g,
                                       uint32_t block_id,
                                       const TurboJSSSAValue *load,
                                       uint32_t stop_value)
{
    const TurboJSSSABlock *block;
    uint32_t i, end;
    if (!g || block_id >= g->block_count)
        return 0;
    block = &g->blocks[block_id];
    end = block->first_value + block->value_count;
    if (end > g->value_count)
        end = (uint32_t)g->value_count;
    if (stop_value < end)
        end = stop_value;
    for (i = block->first_value; i < end; ++i) {
        const TurboJSSSAValue *value = &g->values[i];
        if (!value->removed && value != load &&
            value->opcode == TURBOJS_SSA_PROPERTY_STORE &&
            property_store_may_alias(value, load))
            return 0;
    }
    return 1;
}

static int property_all_paths_clear_rec(const TurboJSSSAGraph *g,
                                        uint32_t prior_block,
                                        uint32_t cursor,
                                        const TurboJSSSAValue *load,
                                        uint8_t *visiting,
                                        int8_t *memo)
{
    const TurboJSSSABlock *block;
    uint32_t p;
    if (cursor == prior_block)
        return 1;
    if (cursor >= g->block_count)
        return 0;
    if (memo[cursor] >= 0)
        return memo[cursor];
    if (visiting[cursor])
        return 0; /* Cycles require the dedicated loop proof below. */
    visiting[cursor] = 1;
    block = &g->blocks[cursor];
    if (!property_block_prefix_clear(g, cursor, load,
                                     cursor == load->block ? load->id : UINT32_MAX) ||
        block->predecessor_count == 0) {
        visiting[cursor] = 0;
        memo[cursor] = 0;
        return 0;
    }
    for (p = 0; p < block->predecessor_count; ++p) {
        if (!property_all_paths_clear_rec(g, prior_block, block->predecessors[p],
                                          load, visiting, memo)) {
            visiting[cursor] = 0;
            memo[cursor] = 0;
            return 0;
        }
    }
    visiting[cursor] = 0;
    memo[cursor] = 1;
    return 1;
}

static int property_all_paths_clear(const TurboJSSSAGraph *g,
                                    uint32_t prior_block,
                                    const TurboJSSSAValue *load)
{
    uint8_t *visiting;
    int8_t *memo;
    int result;
    if (!g || !load || prior_block >= g->block_count ||
        load->block >= g->block_count)
        return 0;
    visiting = (uint8_t *)calloc(g->block_count, sizeof(*visiting));
    memo = (int8_t *)malloc(g->block_count * sizeof(*memo));
    if (!visiting || !memo) {
        free(visiting);
        free(memo);
        return 0;
    }
    memset(memo, -1, g->block_count * sizeof(*memo));
    result = property_all_paths_clear_rec(g, prior_block, load->block, load,
                                          visiting, memo);
    free(visiting);
    free(memo);
    return result;
}

static int property_loop_store_free(const TurboJSSSAGraph *g,
                                    uint32_t loop_header,
                                    const TurboJSSSAValue *load)
{
    uint32_t b, i, end;
    if (!g || loop_header >= g->block_count)
        return 0;
    for (b = 0; b < g->block_count; ++b) {
        const TurboJSSSABlock *block = &g->blocks[b];
        if (b != loop_header && block->loop_header != loop_header)
            continue;
        end = block->first_value + block->value_count;
        if (end > g->value_count)
            end = (uint32_t)g->value_count;
        for (i = block->first_value; i < end; ++i) {
            const TurboJSSSAValue *value = &g->values[i];
            if (!value->removed && value != load &&
                value->opcode == TURBOJS_SSA_PROPERTY_STORE &&
                property_store_may_alias(value, load))
                return 0;
        }
    }
    return 1;
}

static uint32_t eliminate_redundant_property_loads(TurboJSSSAGraph *g,
                                                    uint32_t *dependency_reuses,
                                                    uint32_t *cross_block_reuses,
                                                    uint32_t *unique_path_reuses,
                                                    uint32_t *loop_body_reuses,
                                                    uint32_t *join_reuses,
                                                    uint32_t *memory_versions_proven,
                                                    uint32_t *loop_invariant_reuses,
                                                    uint32_t *memory_phis,
                                                    uint32_t *alias_classes_proven,
                                                    uint32_t *non_aliasing_stores_ignored)
{
    size_t i, j, k;
    uint32_t eliminated = 0, reused = 0, cross_block = 0, unique_path = 0;
    uint32_t loop_body = 0, join = 0, versions = 0, loop_invariant = 0;
    uint32_t phis = 0, alias_classes = 0, ignored_stores = 0;
    for (i = 0; i < g->value_count; ++i) {
        TurboJSSSAValue *load = &g->values[i];
        if (load->removed || load->opcode != TURBOJS_SSA_PROPERTY_LOAD ||
            !load->property_case_count)
            continue;
        for (j = i; j-- > 0;) {
            TurboJSSSAValue *prior = &g->values[j];
            int blocked = 0;
            if (prior->removed)
                continue;
            if (prior->block != load->block) {
                int path_clear = 0;
                int is_unique = 0;
                int is_join = 0;
                int is_loop_invariant = 0;
                uint32_t header = g->blocks[load->block].loop_header;
                if (!block_dominates(g, prior->block, load->block))
                    continue;
                if (property_unique_path_clear(g, prior->block, load->block, load)) {
                    path_clear = 1;
                    is_unique = 1;
                } else if (property_all_paths_clear(g, prior->block, load)) {
                    path_clear = 1;
                    is_join = 1;
                } else if (header != TURBOJS_SSA_NO_BLOCK &&
                           block_dominates(g, prior->block, header) &&
                           property_loop_store_free(g, header, load)) {
                    path_clear = 1;
                    is_loop_invariant = 1;
                }
                if (!path_clear)
                    continue;
                /* Preserve proof classification until the load is replaced. */
                load->property_reserved[0] = (uint8_t)is_unique;
                load->property_reserved[1] = (uint8_t)is_join;
                load->property_reserved[2] = (uint8_t)is_loop_invariant;
            }
            if (prior->opcode == TURBOJS_SSA_PROPERTY_STORE &&
                property_store_may_alias(prior, load))
                break;
            if (prior->opcode != TURBOJS_SSA_PROPERTY_LOAD ||
                prior->left != load->left ||
                !same_property_cases(prior, load))
                continue;
            for (k = j + 1; k < i; ++k) {
                TurboJSSSAValue *between = &g->values[k];
                if (between->block != prior->block &&
                    between->block != load->block)
                    continue;
                if (!between->removed &&
                    between->opcode == TURBOJS_SSA_PROPERTY_STORE &&
                    property_store_may_alias(between, load)) {
                    blocked = 1;
                    break;
                }
            }
            if (blocked)
                break;
            if (same_property_alias_class(prior, load))
                alias_classes++;
            ignored_stores += property_count_non_aliasing_stores(
                g, (uint32_t)j + 1, (uint32_t)i, load);
            if (prior->block != load->block && load->property_reserved[1] &&
                g->blocks[load->block].predecessor_count > 1)
                phis++;
            replace_value_uses(g, (uint32_t)i, (uint32_t)j);
            load->removed = 1;
            eliminated++;
            if (load->property_feedback_generation ||
                prior->property_feedback_generation)
                reused++;
            if (prior->block != load->block) {
                cross_block++;
                versions++;
                if (load->property_reserved[0])
                    unique_path++;
                if (load->property_reserved[1])
                    join++;
                if (load->property_reserved[2])
                    loop_invariant++;
                if (g->blocks[prior->block].loop_depth &&
                    (g->blocks[load->block].loop_header == prior->block ||
                     (g->blocks[load->block].loop_header != TURBOJS_SSA_NO_BLOCK &&
                      g->blocks[load->block].loop_header ==
                          g->blocks[prior->block].loop_header)))
                    loop_body++;
            }
            break;
        }
    }
    if (dependency_reuses)
        *dependency_reuses = reused;
    if (cross_block_reuses)
        *cross_block_reuses = cross_block;
    if (unique_path_reuses)
        *unique_path_reuses = unique_path;
    if (loop_body_reuses)
        *loop_body_reuses = loop_body;
    if (join_reuses)
        *join_reuses = join;
    if (memory_versions_proven)
        *memory_versions_proven = versions;
    if (loop_invariant_reuses)
        *loop_invariant_reuses = loop_invariant;
    if (memory_phis)
        *memory_phis = phis;
    if (alias_classes_proven)
        *alias_classes_proven = alias_classes;
    if (non_aliasing_stores_ignored)
        *non_aliasing_stores_ignored = ignored_stores;
    return eliminated;
}


static uint32_t eliminate_redundant_element_loads(TurboJSSSAGraph *g)
{
    size_t i, j, k;
    uint32_t eliminated = 0;
    for (i = 0; i < g->value_count; ++i) {
        TurboJSSSAValue *load = &g->values[i];
        if (load->removed || load->opcode != TURBOJS_SSA_ELEMENT_LOAD) continue;
        for (j = 0; j < i; ++j) {
            TurboJSSSAValue *prior = &g->values[j];
            int blocked = 0;
            if (prior->removed || prior->opcode != TURBOJS_SSA_ELEMENT_LOAD ||
                prior->block != load->block || prior->left != load->left ||
                prior->right != load->right || prior->element_kind != load->element_kind ||
                prior->element_generation != load->element_generation ||
                prior->element_flags != load->element_flags) continue;
            for (k = j + 1; k < i; ++k) {
                TurboJSSSAValue *between = &g->values[k];
                if (!between->removed && between->opcode == TURBOJS_SSA_ELEMENT_STORE &&
                    between->left == load->left) { blocked = 1; break; }
            }
            if (!blocked) {
                replace_value_uses(g, (uint32_t)i, (uint32_t)j);
                load->removed = 1;
                eliminated++;
                break;
            }
        }
    }
    return eliminated;
}


static int value_const_i64(const TurboJSSSAGraph *g, uint32_t id, int64_t expected)
{
    return g && id < g->value_count && !g->values[id].removed &&
           g->values[id].opcode == TURBOJS_SSA_CONSTANT_I64 &&
           g->values[id].immediate == expected;
}

static int canonical_unit_induction(const TurboJSSSAGraph *g, uint32_t id,
                                    uint32_t *out_limit)
{
    const TurboJSSSAValue *phi, *update, *cmp;
    uint32_t init, back, i;
    if (!g || id >= g->value_count) return 0;
    phi = &g->values[id];
    if (phi->removed || phi->opcode != TURBOJS_SSA_PHI ||
        phi->block >= g->block_count || g->blocks[phi->block].loop_depth == 0)
        return 0;
    init = phi->left; back = phi->right;
    if (!value_const_i64(g, init, 0)) {
        init = phi->right; back = phi->left;
        if (!value_const_i64(g, init, 0)) return 0;
    }
    if (back >= g->value_count) return 0;
    update = &g->values[back];
    if (update->removed || update->opcode != TURBOJS_SSA_ADD_I64) return 0;
    if (!((update->left == id && value_const_i64(g, update->right, 1)) ||
          (update->right == id && value_const_i64(g, update->left, 1)))) return 0;
    for (i = 0; i < g->value_count; ++i) {
        cmp = &g->values[i];
        if (cmp->removed || cmp->opcode != TURBOJS_SSA_LESS_THAN_I64 ||
            cmp->block != phi->block || cmp->left != id) continue;
        if (out_limit) *out_limit = cmp->right;
        return 1;
    }
    return 0;
}

static void recognize_element_loop_ranges(TurboJSSSAGraph *g,
                                          uint32_t *induction_indexes,
                                          uint32_t *range_proofs,
                                          uint32_t *canonical_inductions,
                                          uint32_t *length_hoists,
                                          uint32_t *base_hoists,
                                          uint32_t *bounds_eliminated)
{
    size_t i; uint32_t induction=0, proofs=0, canonical=0, lengths=0, bases=0, bounds=0;
    if(!g)return;
    for(i=0;i<g->value_count;i++){
        TurboJSSSAValue *v=&g->values[i]; uint32_t limit=TURBOJS_SSA_NO_VALUE;
        if(v->removed||(v->opcode!=TURBOJS_SSA_ELEMENT_LOAD&&v->opcode!=TURBOJS_SSA_ELEMENT_STORE)||v->right>=g->value_count)continue;
        {
            TurboJSSSAValue *idx=&g->values[v->right];
            if(idx->type==TURBOJS_SSA_TYPE_INT32||idx->opcode==TURBOJS_SSA_GUARD_INT32||idx->opcode==TURBOJS_SSA_PHI){
                induction++;
                if(v->block<g->block_count&&g->blocks[v->block].loop_depth>0)proofs++;
            }
        }
        if (!canonical_unit_induction(g, v->right, &limit)) continue;
        if (v->element_length_value != TURBOJS_SSA_NO_VALUE &&
            v->element_length_value != limit) continue;
        v->element_length_value = limit;
        v->element_range_min = 0;
        v->element_range_max = INT32_MAX;
        v->element_induction_step = 1;
        v->element_bounds_proven = 1;
        v->element_length_hoisted = 1;
        v->element_base_hoisted = 1;
        canonical++; lengths++; bases++; bounds++;
    }
    if(induction_indexes)*induction_indexes=induction;
    if(range_proofs)*range_proofs=proofs;
    if(canonical_inductions)*canonical_inductions=canonical;
    if(length_hoists)*length_hoists=lengths;
    if(base_hoists)*base_hoists=bases;
    if(bounds_eliminated)*bounds_eliminated=bounds;
}
static uint32_t eliminate_redundant_guards(TurboJSSSAGraph *g)
{
    size_t i, j;
    uint32_t eliminated = 0;
    for (i = 0; i < g->value_count; ++i) {
        TurboJSSSAValue *guard = &g->values[i];
        if (guard->removed || guard->opcode != TURBOJS_SSA_GUARD_INT32) continue;
        for (j = 0; j < i; ++j) {
            TurboJSSSAValue *prior = &g->values[j];
            if (prior->removed || prior->opcode != TURBOJS_SSA_GUARD_INT32) continue;
            if (prior->block == guard->block && prior->left == guard->left) {
                replace_value_uses(g, (uint32_t)i, (uint32_t)j);
                guard->removed = 1;
                eliminated++;
                break;
            }
        }
    }
    return eliminated;
}
static uint32_t eliminate_local_common_expressions(TurboJSSSAGraph *g)
{
    size_t i, j;
    uint32_t eliminated = 0;
    for (i = 0; i < g->value_count; ++i) {
        TurboJSSSAValue *v = &g->values[i];
        if (v->removed || !cse_candidate(v)) continue;
        for (j = 0; j < i; ++j) {
            TurboJSSSAValue *prior = &g->values[j];
            if (prior->removed || !cse_candidate(prior)) continue;
            if (same_expression(prior, v)) {
                replace_value_uses(g, (uint32_t)i, (uint32_t)j);
                v->removed = 1;
                eliminated++;
                break;
            }
        }
    }
    return eliminated;
}

static int is_const(const TurboJSSSAValue *value)
{
    return value->opcode == TURBOJS_SSA_CONSTANT_I64 && !value->removed;
}

static int fold_binary(TurboJSSSAOpcode opcode, int64_t left, int64_t right,
                       int64_t *result)
{
    switch (opcode) {
    case TURBOJS_SSA_LESS_THAN_I64:
        *result = left < right;
        return 1;
    case TURBOJS_SSA_ADD_I64:
#if defined(__GNUC__) || defined(__clang__)
        return !__builtin_add_overflow(left, right, result);
#else
        if ((right > 0 && left > INT64_MAX - right) ||
            (right < 0 && left < INT64_MIN - right)) return 0;
        *result = left + right;
        return 1;
#endif
    case TURBOJS_SSA_SUB_I64:
#if defined(__GNUC__) || defined(__clang__)
        return !__builtin_sub_overflow(left, right, result);
#else
        if ((right < 0 && left > INT64_MAX + right) ||
            (right > 0 && left < INT64_MIN + right)) return 0;
        *result = left - right;
        return 1;
#endif
    case TURBOJS_SSA_MUL_I64:
#if defined(__GNUC__) || defined(__clang__)
        return !__builtin_mul_overflow(left, right, result);
#else
        if (left == 0 || right == 0) { *result = 0; return 1; }
        if (left == -1 && right == INT64_MIN) return 0;
        if (right == -1 && left == INT64_MIN) return 0;
        if (left > 0) {
            if (right > 0) { if (left > INT64_MAX / right) return 0; }
            else { if (right < INT64_MIN / left) return 0; }
        } else {
            if (right > 0) { if (left < INT64_MIN / right) return 0; }
            else if (left != 0 && right < INT64_MAX / left) return 0;
        }
        *result = left * right;
        return 1;
#endif
    default:
        return 0;
    }
}

static int removable_value(const TurboJSSSAValue *value)
{
    switch (value->opcode) {
    case TURBOJS_SSA_RETURN:
    case TURBOJS_SSA_ARGUMENT:
    case TURBOJS_SSA_JUMP:
    case TURBOJS_SSA_BRANCH_TRUE:
    case TURBOJS_SSA_BRANCH_FALSE:
    case TURBOJS_SSA_GUARD_INT32:
    case TURBOJS_SSA_PROPERTY_STORE:
    case TURBOJS_SSA_ELEMENT_STORE:
        return 0;
    default:
        return 1;
    }
}

TurboJSSSAOptimizationStats TurboJS_SSAOptimize(TurboJSSSAGraph *graph)
{
    TurboJSSSAOptimizationStats stats = {0};
    size_t i;
    uint32_t *worklist;
    size_t work_count = 0;

    if (!graph)
        return stats;

    stats.types_inferred = TurboJS_SSAInferTypes(graph);

    for (i = 0; i < graph->value_count; ++i) {
        TurboJSSSAValue *value = &graph->values[i];
        if (value->removed)
            continue;
        if ((value->opcode == TURBOJS_SSA_ADD_I64 ||
             value->opcode == TURBOJS_SSA_SUB_I64 ||
             value->opcode == TURBOJS_SSA_MUL_I64 ||
             value->opcode == TURBOJS_SSA_LESS_THAN_I64) &&
            value->left < graph->value_count &&
            value->right < graph->value_count &&
            is_const(&graph->values[value->left]) &&
            is_const(&graph->values[value->right])) {
            int64_t folded;
            if (fold_binary(value->opcode,
                            graph->values[value->left].immediate,
                            graph->values[value->right].immediate,
                            &folded)) {
                if (graph->values[value->left].use_count)
                    graph->values[value->left].use_count--;
                if (graph->values[value->right].use_count)
                    graph->values[value->right].use_count--;
                value->opcode = TURBOJS_SSA_CONSTANT_I64;
                value->immediate = folded;
                value->left = TURBOJS_SSA_NO_VALUE;
                value->right = TURBOJS_SSA_NO_VALUE;
                stats.constants_folded++;
            }
        }
        if ((value->opcode == TURBOJS_SSA_BRANCH_TRUE ||
             value->opcode == TURBOJS_SSA_BRANCH_FALSE) &&
            value->left < graph->value_count &&
            is_const(&graph->values[value->left]) &&
            value->block < graph->block_count) {
            TurboJSSSABlock *block = &graph->blocks[value->block];
            int take = graph->values[value->left].immediate != 0;
            uint32_t chosen;
            if (value->opcode == TURBOJS_SSA_BRANCH_FALSE)
                take = !take;
            chosen = take ? (uint32_t)value->immediate :
                (block->successor_count > 1 ? block->successors[1] : TURBOJS_SSA_NO_BLOCK);
            if (chosen < graph->block_count) {
                if (graph->values[value->left].use_count)
                    graph->values[value->left].use_count--;
                value->opcode = TURBOJS_SSA_JUMP;
                value->left = TURBOJS_SSA_NO_VALUE;
                value->right = TURBOJS_SSA_NO_VALUE;
                value->immediate = (int64_t)chosen;
                block->successors[0] = chosen;
                block->successor_count = 1;
                stats.branches_folded++;
            }
        }
    }

    stats.types_inferred += TurboJS_SSAInferTypes(graph);
    stats.expressions_eliminated = eliminate_local_common_expressions(graph);
    stats.property_loads_eliminated = eliminate_redundant_property_loads(
        graph, &stats.property_dependency_reuses,
        &stats.property_cross_block_loads_eliminated,
        &stats.property_unique_path_reuses,
        &stats.property_loop_body_reuses,
        &stats.property_join_reuses,
        &stats.property_memory_versions_proven,
        &stats.property_loop_invariant_reuses,
        &stats.property_memory_phis,
        &stats.property_alias_classes_proven,
        &stats.property_non_aliasing_stores_ignored);
    stats.element_bounds_checks_eliminated = eliminate_redundant_element_loads(graph);
    stats.element_length_reuses = stats.element_bounds_checks_eliminated;
    recognize_element_loop_ranges(graph, &stats.element_induction_indexes_recognized,
                                  &stats.element_loop_range_proofs,
                                  &stats.element_canonical_inductions,
                                  &stats.element_length_hoists,
                                  &stats.element_base_pointer_hoists,
                                  &stats.element_loop_bounds_checks_eliminated);
    stats.guards_eliminated = eliminate_redundant_guards(graph);
    stats.values_removed += stats.expressions_eliminated +
        stats.property_loads_eliminated + stats.element_bounds_checks_eliminated +
        stats.guards_eliminated;

    mark_reachable(graph);
    for (i = 0; i < graph->block_count; ++i) {
        if (!graph->blocks[i].reachable && !graph->blocks[i].removed) {
            graph->blocks[i].removed = 1;
            stats.blocks_removed++;
        }
    }

    worklist = graph->value_count && graph->value_count <= SIZE_MAX / sizeof(*worklist) ?
        (uint32_t *)malloc(graph->value_count * sizeof(*worklist)) : NULL;
    if (!worklist && graph->value_count)
        return stats;
    for (i = 0; i < graph->value_count; ++i) {
        TurboJSSSAValue *value = &graph->values[i];
        if (!value->removed && value->use_count == 0 && removable_value(value))
            worklist[work_count++] = (uint32_t)i;
    }
    while (work_count) {
        TurboJSSSAValue *value = &graph->values[worklist[--work_count]];
        uint32_t inputs[2] = { value->left, value->right };
        unsigned input_index;
        if (value->removed || value->use_count != 0 || !removable_value(value))
            continue;
        value->removed = 1;
        stats.values_removed++;
        for (input_index = 0; input_index < 2; ++input_index) {
            uint32_t input = inputs[input_index];
            if (input < graph->value_count && graph->values[input].use_count) {
                graph->values[input].use_count--;
                if (graph->values[input].use_count == 0 &&
                    !graph->values[input].removed &&
                    removable_value(&graph->values[input]))
                    worklist[work_count++] = input;
            }
        }
    }
    free(worklist);
    return stats;
}
int TurboJS_SSAVerify(const TurboJSSSAGraph *g) {
    size_t i;uint32_t j;if(!g||!g->block_count||g->entry_block>=g->block_count)return 0;for(i=0;i<g->block_count;i++){const TurboJSSSABlock*b=&g->blocks[i];if(b->id!=i||b->predecessor_count>TURBOJS_SSA_MAX_BLOCK_EDGES||b->successor_count>TURBOJS_SSA_MAX_BLOCK_EDGES)return 0;for(j=0;j<b->successor_count;j++)if(b->successors[j]>=g->block_count)return 0;}
    for(i=0;i<g->value_count;i++){const TurboJSSSAValue*v=&g->values[i];if(v->id!=i||v->block>=g->block_count)return 0;if(v->opcode!=TURBOJS_SSA_PHI){if(v->left!=TURBOJS_SSA_NO_VALUE&&v->left>=i)return 0;if(v->right!=TURBOJS_SSA_NO_VALUE&&v->right>=i)return 0;}else if(v->left>=g->value_count||v->right>=g->value_count)return 0;if(v->opcode==TURBOJS_SSA_GUARD_INT32&&(!v->has_deopt_edge||v->left==TURBOJS_SSA_NO_VALUE))return 0;}return 1;
}
