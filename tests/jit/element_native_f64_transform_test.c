#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "jit.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"check failed:%d: %s\n",__LINE__,#x);return 1;}}while(0)
typedef struct MockArrayF64{uint16_t kind,flags;uint32_t generation,length;double values[16];}MockArrayF64;
static int guard_shape(TurboJSRegionValue v,uintptr_t s,void*o){(void)v;(void)s;(void)o;return 0;}
static int load_slot(TurboJSRegionValue v,uint32_t i,TurboJSRegionValue*out,void*o){(void)v;(void)i;(void)out;(void)o;return 0;}
static int to_i64(TurboJSRegionValue v,int64_t*out,void*o){(void)o;if(!out)return 0;*out=(int64_t)v;return 1;}
static TurboJSRegionValue from_i64(int64_t v,void*o){(void)o;return(TurboJSRegionValue)v;}
static uint64_t bits(double x){uint64_t u;memcpy(&u,&x,sizeof(u));return u;}
static void build_graph(TurboJSSSAGraph*g){
 memset(g,0,sizeof(*g));g->entry_block=0;g->block_count=g->block_capacity=4;g->blocks=calloc(4,sizeof(*g->blocks));
 g->blocks[0]=(TurboJSSSABlock){.id=0,.first_value=0,.value_count=6,.successors={1},.successor_count=1,.reachable=1};
 g->blocks[1]=(TurboJSSSABlock){.id=1,.first_value=6,.value_count=3,.predecessors={0,2},.predecessor_count=2,.successors={2,3},.successor_count=2,.loop_depth=1,.loop_header=1,.reachable=1};
 g->blocks[2]=(TurboJSSSABlock){.id=2,.first_value=9,.value_count=7,.predecessors={1},.predecessor_count=1,.successors={1},.successor_count=1,.loop_depth=1,.loop_header=1,.reachable=1};
 g->blocks[3]=(TurboJSSSABlock){.id=3,.first_value=16,.value_count=1,.predecessors={1},.predecessor_count=1,.reachable=1};
 g->value_count=g->value_capacity=17;g->values=calloc(17,sizeof(*g->values));
 g->values[0]=(TurboJSSSAValue){.id=0,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_REFERENCE,.immediate=0};
 g->values[1]=(TurboJSSSAValue){.id=1,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_REFERENCE,.immediate=1};
 g->values[2]=(TurboJSSSAValue){.id=2,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_INT32,.immediate=2};
 g->values[3]=(TurboJSSSAValue){.id=3,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_FLOAT64,.immediate=3};
 g->values[4]=(TurboJSSSAValue){.id=4,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_FLOAT64,.immediate=4};
 g->values[5]=(TurboJSSSAValue){.id=5,.block=0,.opcode=TURBOJS_SSA_CONSTANT_I64,.type=TURBOJS_SSA_TYPE_INT32,.immediate=0};
 g->values[6]=(TurboJSSSAValue){.id=6,.block=1,.opcode=TURBOJS_SSA_PHI,.type=TURBOJS_SSA_TYPE_INT32,.left=5,.right=14};
 g->values[7]=(TurboJSSSAValue){.id=7,.block=1,.opcode=TURBOJS_SSA_LESS_THAN_I64,.type=TURBOJS_SSA_TYPE_BOOLEAN,.left=6,.right=2};
 g->values[8]=(TurboJSSSAValue){.id=8,.block=1,.opcode=TURBOJS_SSA_BRANCH_FALSE,.type=TURBOJS_SSA_TYPE_BOOLEAN,.left=7,.metadata=3};
 g->values[9]=(TurboJSSSAValue){.id=9,.block=2,.opcode=TURBOJS_SSA_ELEMENT_LOAD,.type=TURBOJS_SSA_TYPE_FLOAT64,.left=0,.right=6,.element_kind=TURBOJS_ELEMENT_KIND_TYPED_F64,.element_generation=21,.element_flags=TURBOJS_ELEMENT_FLAG_FAST_MATH,.element_length_value=2,.element_bounds_proven=1,.element_length_hoisted=1,.element_base_hoisted=1};
 g->values[10]=(TurboJSSSAValue){.id=10,.block=2,.opcode=TURBOJS_SSA_MUL_I64,.type=TURBOJS_SSA_TYPE_FLOAT64,.left=9,.right=3};
 g->values[11]=(TurboJSSSAValue){.id=11,.block=2,.opcode=TURBOJS_SSA_ADD_I64,.type=TURBOJS_SSA_TYPE_FLOAT64,.left=10,.right=4};
 g->values[12]=(TurboJSSSAValue){.id=12,.block=2,.opcode=TURBOJS_SSA_ELEMENT_STORE,.type=TURBOJS_SSA_TYPE_REFERENCE,.left=1,.right=6,.metadata=11,.element_kind=TURBOJS_ELEMENT_KIND_TYPED_F64,.element_generation=22,.element_flags=TURBOJS_ELEMENT_FLAG_WRITABLE|TURBOJS_ELEMENT_FLAG_FAST_MATH,.element_length_value=2,.element_bounds_proven=1,.element_length_hoisted=1,.element_base_hoisted=1};
 g->values[13]=(TurboJSSSAValue){.id=13,.block=2,.opcode=TURBOJS_SSA_CONSTANT_I64,.type=TURBOJS_SSA_TYPE_INT32,.immediate=1};
 g->values[14]=(TurboJSSSAValue){.id=14,.block=2,.opcode=TURBOJS_SSA_ADD_I64,.type=TURBOJS_SSA_TYPE_INT32,.left=6,.right=13};
 g->values[15]=(TurboJSSSAValue){.id=15,.block=2,.opcode=TURBOJS_SSA_JUMP,.metadata=1};
 g->values[16]=(TurboJSSSAValue){.id=16,.block=3,.opcode=TURBOJS_SSA_RETURN,.type=TURBOJS_SSA_TYPE_INT32,.left=2};
}
int main(void){TurboJSSSAGraph g;TurboJSRegionNativeFunction*n=NULL;TurboJSRegionNativeStats st={0};TurboJSIRDiagnostic d={0};TurboJSRegionValue raw=0,args[5];TurboJSRegionValueOps ops={.guard_shape=guard_shape,.load_own_slot=load_slot,.to_i64=to_i64,.from_i64=from_i64};
 MockArrayF64 src={TURBOJS_ELEMENT_KIND_TYPED_F64,0,21,7,{1,2,3,4,5,6,7}},dst={TURBOJS_ELEMENT_KIND_TYPED_F64,TURBOJS_ELEMENT_FLAG_WRITABLE,22,7,{0}};
 TurboJSRegionElementLayout layout={.object_pointer_mask=UINT64_MAX,.kind_offset=offsetof(MockArrayF64,kind),.generation_offset=offsetof(MockArrayF64,generation),.length_offset=offsetof(MockArrayF64,length),.element_storage_offset=offsetof(MockArrayF64,values),.element_stride=sizeof(double),.element_value_offset=0,.element_storage_indirect=0};
 args[0]=(TurboJSRegionValue)(uintptr_t)&src;args[1]=(TurboJSRegionValue)(uintptr_t)&dst;args[2]=7;args[3]=bits(2.0);args[4]=bits(0.5);build_graph(&g);
 CHECK(TurboJS_RegionNativeCompileWithElementLayout(&g,&layout,&n,&st,&d)==TURBOJS_IR_OK);CHECK(st.inline_element_transform_loops==1);CHECK(st.inline_element_float64_loops==1);CHECK(st.inline_element_loop_unroll_factor==2||st.inline_element_loop_unroll_factor==8);if(st.inline_element_simd_loops){CHECK(st.inline_element_simd_width==4);CHECK(st.inline_element_simd_level==2||st.inline_element_simd_level==3);CHECK(st.inline_element_simd_fma==(st.inline_element_simd_level==3));}
 CHECK(TurboJS_RegionNativeInvokeValues(n,args,5,&ops,NULL,&raw)==TURBOJS_IR_OK);CHECK(raw==7);for(int i=0;i<7;i++)CHECK(fabs(dst.values[i]-(src.values[i]*2.0+0.5))<1e-12);
 args[2]=8;CHECK(TurboJS_RegionNativeInvokeValues(n,args,5,&ops,NULL,&raw)==TURBOJS_IR_BAILOUT);dst.generation=23;args[2]=7;CHECK(TurboJS_RegionNativeInvokeValues(n,args,5,&ops,NULL,&raw)==TURBOJS_IR_BAILOUT);
 TurboJS_RegionNativeFunctionDestroy(n);TurboJS_SSAGraphDestroy(&g);puts("native Float64 transform loop test passed");return 0;}
