#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "jit.h"

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"check failed:%d: %s\n",__LINE__,#x); return 1; } } while(0)

typedef struct MockArrayF64 {
    uint16_t kind;
    uint16_t flags;
    uint32_t generation;
    uint32_t length;
    double values[16];
} MockArrayF64;

static int guard_shape(TurboJSRegionValue v, uintptr_t s, void *o){(void)v;(void)s;(void)o;return 0;}
static int load_slot(TurboJSRegionValue v,uint32_t i,TurboJSRegionValue*out,void*o){(void)v;(void)i;(void)out;(void)o;return 0;}
static int to_i64(TurboJSRegionValue v,int64_t*out,void*o){(void)o;if(!out)return 0;*out=(int64_t)v;return 1;}
static TurboJSRegionValue from_i64(int64_t v,void*o){(void)o;return (TurboJSRegionValue)v;}

static void build_sum_graph(TurboJSSSAGraph *g)
{
    memset(g,0,sizeof(*g));
    g->entry_block=0;g->block_count=g->block_capacity=4;
    g->blocks=calloc(4,sizeof(*g->blocks));
    g->blocks[0]=(TurboJSSSABlock){.id=0,.first_value=0,.value_count=4,.successors={1},.successor_count=1,.reachable=1};
    g->blocks[1]=(TurboJSSSABlock){.id=1,.first_value=4,.value_count=4,.predecessors={0,2},.predecessor_count=2,.successors={2,3},.successor_count=2,.loop_depth=1,.loop_header=1,.reachable=1};
    g->blocks[2]=(TurboJSSSABlock){.id=2,.first_value=8,.value_count=5,.predecessors={1},.predecessor_count=1,.successors={1},.successor_count=1,.loop_depth=1,.loop_header=1,.reachable=1};
    g->blocks[3]=(TurboJSSSABlock){.id=3,.first_value=13,.value_count=1,.predecessors={1},.predecessor_count=1,.reachable=1};
    g->value_count=g->value_capacity=14;g->values=calloc(14,sizeof(*g->values));
    g->values[0]=(TurboJSSSAValue){.id=0,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_REFERENCE,.immediate=0};
    g->values[1]=(TurboJSSSAValue){.id=1,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_INT32,.immediate=1};
    g->values[2]=(TurboJSSSAValue){.id=2,.block=0,.opcode=TURBOJS_SSA_CONSTANT_I64,.type=TURBOJS_SSA_TYPE_INT32,.immediate=0};
    g->values[3]=(TurboJSSSAValue){.id=3,.block=0,.opcode=TURBOJS_SSA_CONSTANT_I64,.type=TURBOJS_SSA_TYPE_FLOAT64,.immediate=0};
    g->values[4]=(TurboJSSSAValue){.id=4,.block=1,.opcode=TURBOJS_SSA_PHI,.type=TURBOJS_SSA_TYPE_INT32,.left=2,.right=11};
    g->values[5]=(TurboJSSSAValue){.id=5,.block=1,.opcode=TURBOJS_SSA_PHI,.type=TURBOJS_SSA_TYPE_FLOAT64,.left=3,.right=9};
    g->values[6]=(TurboJSSSAValue){.id=6,.block=1,.opcode=TURBOJS_SSA_LESS_THAN_I64,.type=TURBOJS_SSA_TYPE_BOOLEAN,.left=4,.right=1};
    g->values[7]=(TurboJSSSAValue){.id=7,.block=1,.opcode=TURBOJS_SSA_BRANCH_FALSE,.type=TURBOJS_SSA_TYPE_BOOLEAN,.left=6,.metadata=3};
    g->values[8]=(TurboJSSSAValue){.id=8,.block=2,.opcode=TURBOJS_SSA_ELEMENT_LOAD,.type=TURBOJS_SSA_TYPE_FLOAT64,.left=0,.right=4,.element_kind=TURBOJS_ELEMENT_KIND_TYPED_F64,.element_generation=11,.element_length_value=1};
    g->values[9]=(TurboJSSSAValue){.id=9,.block=2,.opcode=TURBOJS_SSA_ADD_I64,.type=TURBOJS_SSA_TYPE_FLOAT64,.left=5,.right=8};
    g->values[10]=(TurboJSSSAValue){.id=10,.block=2,.opcode=TURBOJS_SSA_CONSTANT_I64,.type=TURBOJS_SSA_TYPE_INT32,.immediate=1};
    g->values[11]=(TurboJSSSAValue){.id=11,.block=2,.opcode=TURBOJS_SSA_ADD_I64,.type=TURBOJS_SSA_TYPE_INT32,.left=4,.right=10};
    g->values[12]=(TurboJSSSAValue){.id=12,.block=2,.opcode=TURBOJS_SSA_JUMP,.metadata=1};
    g->values[13]=(TurboJSSSAValue){.id=13,.block=3,.opcode=TURBOJS_SSA_RETURN,.type=TURBOJS_SSA_TYPE_FLOAT64,.left=5};
}

int main(void)
{
    TurboJSSSAGraph g;TurboJSSSAOptimizationStats os;TurboJSRegionNativeStats st={0};
    TurboJSRegionNativeFunction*native=NULL;TurboJSIRDiagnostic d={0};TurboJSRegionValue raw=0,args[2];double result=0;
    TurboJSRegionValueOps ops={.guard_shape=guard_shape,.load_own_slot=load_slot,.to_i64=to_i64,.from_i64=from_i64};
    MockArrayF64 a={TURBOJS_ELEMENT_KIND_TYPED_F64,0,11,7,{0.5,1.25,2.0,3.75,4.5,5.0,6.25}};
    TurboJSRegionElementLayout layout={.object_pointer_mask=UINT64_MAX,.kind_offset=offsetof(MockArrayF64,kind),.generation_offset=offsetof(MockArrayF64,generation),.length_offset=offsetof(MockArrayF64,length),.element_storage_offset=offsetof(MockArrayF64,values),.element_stride=sizeof(double),.element_value_offset=0,.element_storage_indirect=0};
    args[0]=(TurboJSRegionValue)(uintptr_t)&a;args[1]=7;
    build_sum_graph(&g);os=TurboJS_SSAOptimize(&g);
    CHECK(os.element_canonical_inductions==1);CHECK(g.values[8].element_bounds_proven==1);
    CHECK(TurboJS_RegionNativeCompileWithElementLayout(&g,&layout,&native,&st,&d)==TURBOJS_IR_OK);
    CHECK(st.inline_element_float64_loops==1);CHECK(st.inline_element_loop_unroll_factor==2||st.inline_element_loop_unroll_factor==8);CHECK(st.inline_element_loop_accumulators==2);if(st.inline_element_simd_loops){CHECK(st.inline_element_simd_width==4);CHECK(st.inline_element_simd_level==2||st.inline_element_simd_level==3);}
    CHECK(TurboJS_RegionNativeInvokeValues(native,args,2,&ops,NULL,&raw)==TURBOJS_IR_OK);memcpy(&result,&raw,sizeof(result));CHECK(fabs(result-23.25)<1e-12);
    args[1]=5;CHECK(TurboJS_RegionNativeInvokeValues(native,args,2,&ops,NULL,&raw)==TURBOJS_IR_OK);memcpy(&result,&raw,sizeof(result));CHECK(fabs(result-12.0)<1e-12);
    args[1]=8;CHECK(TurboJS_RegionNativeInvokeValues(native,args,2,&ops,NULL,&raw)==TURBOJS_IR_BAILOUT);
    a.generation=12;args[1]=7;CHECK(TurboJS_RegionNativeInvokeValues(native,args,2,&ops,NULL,&raw)==TURBOJS_IR_BAILOUT);
    TurboJS_RegionNativeFunctionDestroy(native);TurboJS_SSAGraphDestroy(&g);
    puts("native Float64 element loop test passed");return 0;
}
