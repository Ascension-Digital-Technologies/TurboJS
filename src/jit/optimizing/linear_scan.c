#include "jit.h"
#include <stdlib.h>
#include <string.h>

/* Loop-aware SSA linear scan.
 *
 * Positions are assigned in CFG reverse-postorder, not bytecode order. Phi
 * inputs are uses on predecessor edges, and natural-loop backedges extend
 * values that are live through a loop. Use positions drive spill decisions;
 * loop phis and loop-carried values receive a retention bias so induction
 * variables survive inner-loop pressure.
 */
typedef struct UseVec { uint32_t *p; size_t n, c; } UseVec;
typedef struct SplitRec { uint32_t value_id, position; int16_t reg, spill; } SplitRec;

static int push_use(UseVec *v, uint32_t p) {
    void *q;
    if (v->n == v->c) {
        size_t c = v->c ? v->c * 2u : 4u;
        q = realloc(v->p, c * sizeof(*v->p));
        if (!q) return 0;
        v->p = q; v->c = c;
    }
    v->p[v->n++] = p;
    return 1;
}
static int u32cmp(const void *a,const void *b){uint32_t x=*(const uint32_t*)a,y=*(const uint32_t*)b;return x<y?-1:x>y;}
static int interval_compare(const void *a, const void *b) {
    const TurboJSLiveInterval *x = a, *y = b;
    if (x->start != y->start) return x->start < y->start ? -1 : 1;
    if (x->is_phi != y->is_phi) return x->is_phi ? -1 : 1;
    if (x->loop_depth != y->loop_depth) return x->loop_depth > y->loop_depth ? -1 : 1;
    if (x->end != y->end) return x->end > y->end ? -1 : 1;
    return x->value_id < y->value_id ? -1 : x->value_id != y->value_id;
}
static TurboJSRegisterClass class_for(TurboJSSSAType type) {
    return type == TURBOJS_SSA_TYPE_FLOAT64 ? TURBOJS_REGISTER_CLASS_FLOAT64 : TURBOJS_REGISTER_CLASS_INTEGER;
}
static void dfs_rpo(const TurboJSSSAGraph*g,uint32_t b,uint8_t*seen,uint32_t*post,size_t*n){
    uint32_t i;if(b>=g->block_count||seen[b]||g->blocks[b].removed||!g->blocks[b].reachable)return;seen[b]=1;
    for(i=0;i<g->blocks[b].successor_count;i++) dfs_rpo(g,g->blocks[b].successors[i],seen,post,n);
    post[(*n)++]=b;
}
static int block_dominates(const TurboJSSSAGraph*g,uint32_t dom,uint32_t b){
    size_t guard=0;if(dom==b)return 1;while(b<TURBOJS_SSA_NO_BLOCK&&b<g->block_count&&guard++<=g->block_count){b=g->blocks[b].immediate_dominator;if(b==dom)return 1;}return 0;
}
static uint32_t next_use(const TurboJSLiveInterval*x,const uint32_t*uses,uint32_t pos){
    uint32_t i;for(i=0;i<x->use_count;i++){uint32_t u=uses[x->first_use+i];if(u>=pos)return u;}return UINT32_MAX;
}
static uint64_t keep_score(const TurboJSLiveInterval*x,const uint32_t*uses,uint32_t pos){
    uint32_t n=next_use(x,uses,pos);uint64_t distance=n==UINT32_MAX?UINT32_MAX:(uint64_t)(n-pos);
    uint64_t bias=(uint64_t)x->loop_depth*UINT64_C(1)<<34;
    if(x->is_phi)bias+=UINT64_C(1)<<38;
    if(x->crosses_backedge)bias+=UINT64_C(1)<<40;
    return bias + (UINT64_C(1)<<32) - (distance>UINT32_MAX?UINT32_MAX:distance);
}
TurboJSIRStatus TurboJS_LinearScanAllocate(const TurboJSSSAGraph *graph,
                                            uint16_t integer_registers,
                                            uint16_t float_registers,
                                            TurboJSLinearScanResult *result) {
    TurboJSLiveInterval *intervals, **active;
    UseVec *uv=NULL; uint32_t *post=NULL,*rpo=NULL,*bstart=NULL,*bend=NULL,*vpos=NULL,*flat=NULL,*preferred_phi=NULL; uint8_t*seen=NULL;
    SplitRec *splitv=NULL; size_t splitn=0,splitc=0;
    size_t i,j,npost=0,active_count=0,total_uses=0,off=0; uint16_t spill=0,splits=0;
    if (!graph || !result || !integer_registers || !float_registers) return TURBOJS_IR_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));
    intervals = calloc(graph->value_count ? graph->value_count : 1, sizeof(*intervals));
    active = calloc(graph->value_count ? graph->value_count : 1, sizeof(*active));
    uv=calloc(graph->value_count?graph->value_count:1,sizeof(*uv));post=malloc((graph->block_count?graph->block_count:1)*sizeof(*post));rpo=malloc((graph->block_count?graph->block_count:1)*sizeof(*rpo));
    bstart=calloc(graph->block_count?graph->block_count:1,sizeof(*bstart));bend=calloc(graph->block_count?graph->block_count:1,sizeof(*bend));vpos=calloc(graph->value_count?graph->value_count:1,sizeof(*vpos));preferred_phi=malloc((graph->value_count?graph->value_count:1)*sizeof(*preferred_phi));seen=calloc(graph->block_count?graph->block_count:1,1);
    if(!intervals||!active||!uv||!post||!rpo||!bstart||!bend||!vpos||!preferred_phi||!seen)goto oom;
    for(i=0;i<graph->value_count;i++)preferred_phi[i]=TURBOJS_SSA_NO_VALUE;
    dfs_rpo(graph,graph->entry_block,seen,post,&npost);for(i=0;i<npost;i++)rpo[i]=post[npost-1-i];
    {uint32_t pos=2;for(i=0;i<npost;i++){const TurboJSSSABlock*b=&graph->blocks[rpo[i]];bstart[b->id]=pos;for(j=b->first_value;j<b->first_value+b->value_count;j++)if(!graph->values[j].removed){vpos[j]=pos;pos+=2;}bend[b->id]=pos;pos+=2;}}
    for(i=0;i<graph->value_count;i++){
        const TurboJSSSAValue*v=&graph->values[i];TurboJSLiveInterval*x=&intervals[i];
        x->value_id=(uint32_t)i;x->start=x->end=vpos[i];x->register_class=class_for(v->type);x->physical_register=-1;x->spill_slot=-1;x->loop_depth=v->block<graph->block_count?graph->blocks[v->block].loop_depth:0;x->is_phi=v->opcode==TURBOJS_SSA_PHI;
    }
    for(i=0;i<graph->value_count;i++){
        const TurboJSSSAValue*v=&graph->values[i];uint32_t p=vpos[i];if(v->removed)continue;
        if(v->opcode==TURBOJS_SSA_PHI&&v->block<graph->block_count){const TurboJSSSABlock*b=&graph->blocks[v->block];uint32_t srcs[2]={v->left,v->right};for(j=0;j<b->predecessor_count&&j<2;j++){uint32_t s=srcs[j],up=bend[b->predecessors[j]];if(s!=TURBOJS_SSA_NO_VALUE&&s<graph->value_count){if(!push_use(&uv[s],up))goto oom;if(up>intervals[s].end)intervals[s].end=up;}}}
        else {uint32_t srcs[2]={v->left,v->right};for(j=0;j<2;j++){uint32_t s=srcs[j];if(s!=TURBOJS_SSA_NO_VALUE&&s<graph->value_count){if(!push_use(&uv[s],p))goto oom;if(p>intervals[s].end)intervals[s].end=p;}}}
    }
    /* Build two-address coalescing hints for canonical induction/accumulator
     * updates. A phi input such as `next = phi + imm` may reuse the phi's
     * register at the exact instruction boundary because x64 lowering reads
     * the old value before committing the result. */
    for(i=0;i<graph->value_count;i++){
        const TurboJSSSAValue *phi=&graph->values[i];
        uint32_t srcs[2],si;
        if(phi->removed||phi->opcode!=TURBOJS_SSA_PHI)continue;
        srcs[0]=phi->left;srcs[1]=phi->right;
        for(si=0;si<2;si++){
            uint32_t u=srcs[si];const TurboJSSSAValue*upd;
            if(u==TURBOJS_SSA_NO_VALUE||u>=graph->value_count)continue;
            upd=&graph->values[u];
            if(upd->removed)continue;
            if((upd->opcode==TURBOJS_SSA_ADD_I64||upd->opcode==TURBOJS_SSA_SUB_I64) &&
               (upd->left==phi->id||upd->right==phi->id)){
                preferred_phi[u]=phi->id;result->phi_coalesce_candidates++;
            }
        }
    }
    /* Extend live-through values over natural backedges. */
    for(i=0;i<graph->block_count;i++){const TurboJSSSABlock*pred=&graph->blocks[i];for(j=0;j<pred->successor_count;j++){uint32_t h=pred->successors[j],hp,lp,k;if(h>=graph->block_count||!block_dominates(graph,h,(uint32_t)i))continue;hp=bstart[h];lp=bend[i];for(k=0;k<graph->value_count;k++){TurboJSLiveInterval*x=&intervals[k];if(graph->values[k].removed)continue;if(x->start<=hp&&x->end>=hp){if(x->end<lp)x->end=lp;x->crosses_backedge=1;if(graph->blocks[h].loop_depth>x->loop_depth)x->loop_depth=graph->blocks[h].loop_depth;}}}}
    for(i=0;i<graph->value_count;i++){if(uv[i].n>1)qsort(uv[i].p,uv[i].n,sizeof(uint32_t),u32cmp);total_uses+=uv[i].n;}
    flat=malloc((total_uses?total_uses:1)*sizeof(*flat));if(!flat)goto oom;
    for(i=0;i<graph->value_count;i++){intervals[i].first_use=(uint32_t)off;intervals[i].use_count=(uint32_t)uv[i].n;if(uv[i].n)memcpy(flat+off,uv[i].p,uv[i].n*sizeof(*flat));off+=uv[i].n;}
    qsort(intervals,graph->value_count,sizeof(*intervals),interval_compare);
    for(i=0;i<graph->value_count;i++){
        TurboJSLiveInterval*cur=&intervals[i],*victim=NULL;uint16_t limit=cur->register_class==TURBOJS_REGISTER_CLASS_FLOAT64?float_registers:integer_registers;uint8_t used[64]={0};size_t k=0;
        if(cur->value_id>=graph->value_count||graph->values[cur->value_id].removed)continue;
        j=0;while(j<active_count){if(active[j]->end<cur->start){active[j]=active[--active_count];continue;}j++;}
        for(k=0;k<active_count;k++)if(active[k]->register_class==cur->register_class&&active[k]->physical_register>=0)used[(uint16_t)active[k]->physical_register]=1;
        /* Prefer the loop phi register for a destructive backedge update when
         * the phi dies exactly at this instruction. This removes the edge copy
         * without creating overlapping live values. */
        if(preferred_phi[cur->value_id]!=TURBOJS_SSA_NO_VALUE){
            size_t pk;TurboJSLiveInterval*pref=NULL;
            for(pk=0;pk<active_count;pk++)if(active[pk]->value_id==preferred_phi[cur->value_id]){pref=active[pk];break;}
            if(pref&&pref->register_class==cur->register_class&&pref->physical_register>=0&&pref->end==cur->start){
                cur->physical_register=pref->physical_register;
                active[pk]=active[--active_count];
                result->phi_coalesce_successes++;
            }else result->phi_coalesce_rejected++;
        }
        if(cur->physical_register<0){for(k=0;k<limit&&used[k];k++){ /* find first free register */ }if(k<limit)cur->physical_register=(int16_t)k;
        else {uint64_t cur_score=keep_score(cur,flat,cur->start),victim_score=UINT64_MAX;for(k=0;k<active_count;k++){TurboJSLiveInterval*x=active[k];uint64_t s;if(x->register_class!=cur->register_class||x->physical_register<0)continue;s=keep_score(x,flat,cur->start);if(!victim||s<victim_score){victim=x;victim_score=s;}}
            if(victim&&cur_score>victim_score){
                int16_t oldreg=victim->physical_register, slot=(int16_t)spill++;
                if(splitn==splitc){size_t nc=splitc?splitc*2u:8u;void*np=realloc(splitv,nc*sizeof(*splitv));if(!np)goto oom;splitv=np;splitc=nc;}
                {
                    uint32_t split_pos = cur->start;
                    uint32_t block = graph->values[cur->value_id].block;
                    if (block < graph->block_count &&
                        bstart[block] > victim->start &&
                        bstart[block] < victim->end) {
                        /* A split that enters a CFG block must be established on
                         * every incoming edge. Keep same-block definitions at
                         * their instruction position, but normalize live-through
                         * victims to the block boundary. */
                        split_pos = bstart[block];
                    }
                    splitv[splitn++]=(SplitRec){victim->value_id,split_pos,oldreg,slot};
                }
                cur->physical_register=oldreg;victim->physical_register=-1;victim->spill_slot=slot;splits++;
            }else cur->spill_slot=(int16_t)spill++;
        }}
        active[active_count++]=cur;
    }
    {
        TurboJSLiveIntervalFragment *fr=calloc(graph->value_count+splitn+1,sizeof(*fr));size_t fn=0;
        if(!fr)goto oom;
        for(i=0;i<graph->value_count;i++){TurboJSLiveInterval*x=NULL;size_t z;SplitRec*sr=NULL;
            for(z=0;z<graph->value_count;z++)if(intervals[z].value_id==i){x=&intervals[z];break;}
            if(!x||graph->values[i].removed)continue;
            for(z=0;z<splitn;z++)if(splitv[z].value_id==i){sr=&splitv[z];break;}
            if(sr&&sr->position>x->start){fr[fn++]=(TurboJSLiveIntervalFragment){(uint32_t)i,x->start,sr->position-1,sr->reg,-1};fr[fn++]=(TurboJSLiveIntervalFragment){(uint32_t)i,sr->position,x->end,-1,sr->spill};}
            else fr[fn++]=(TurboJSLiveIntervalFragment){(uint32_t)i,x->start,x->end,x->physical_register,x->spill_slot};
        }
        result->fragments=fr;result->fragment_count=fn;
    }
    for(i=0;i<graph->value_count;i++) free(uv[i].p);
    free(uv);free(active);free(post);free(rpo);free(seen);free(splitv);free(preferred_phi);
    result->intervals=intervals;result->interval_count=graph->value_count;result->use_positions=flat;result->use_position_count=total_uses;
    result->value_positions=vpos;result->value_position_count=graph->value_count;result->block_start_positions=bstart;result->block_end_positions=bend;result->block_position_count=graph->block_count;
    result->integer_register_count=integer_registers;result->float_register_count=float_registers;result->spill_slot_count=spill;result->split_count=splits;return TURBOJS_IR_OK;
oom:
    if(uv){for(i=0;i<graph->value_count;i++)free(uv[i].p);}free(uv);free(intervals);free(active);free(post);free(rpo);free(bstart);free(bend);free(vpos);free(preferred_phi);free(seen);free(flat);free(splitv);return TURBOJS_IR_OUT_OF_MEMORY;
}
void TurboJS_LinearScanResultDestroy(TurboJSLinearScanResult *result) {
    if (!result) return;
    free(result->intervals);free(result->fragments);free(result->use_positions);free(result->value_positions);free(result->block_start_positions);free(result->block_end_positions);
    memset(result, 0, sizeof(*result));
}
