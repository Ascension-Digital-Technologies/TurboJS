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
        return 0;
    default:
        return 1;
    }
}

TurboJSSSAOptimizationStats TurboJS_SSAOptimize(TurboJSSSAGraph *graph)
{
    TurboJSSSAOptimizationStats stats = {0, 0, 0, 0, 0, 0};
    size_t i;
    uint32_t *worklist;
    size_t work_count = 0;

    if (!graph)
        return stats;

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
