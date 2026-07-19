#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "jit.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"check failed:%d: %s\n",__LINE__,#x);return 1;}}while(0)
typedef struct A{uint16_t kind,flags;uint32_t generation,length;double values[16];}A;
static int gs(TurboJSRegionValue v,uintptr_t s,void*o){(void)v;(void)s;(void)o;return 0;}
static int ls(TurboJSRegionValue v,uint32_t i,TurboJSRegionValue*out,void*o){(void)v;(void)i;(void)out;(void)o;return 0;}
static int ti(TurboJSRegionValue v,int64_t*out,void*o){(void)o;*out=(int64_t)v;return 1;}
static TurboJSRegionValue fi(int64_t v,void*o){(void)o;return(TurboJSRegionValue)v;}
static uint64_t bits(double x){uint64_t u;memcpy(&u,&x,8);return u;}
static void build(TurboJSSSAGraph*g,int mode){
 size_t vc=mode==2?17:16;memset(g,0,sizeof(*g));g->entry_block=0;g->block_count=g->block_capacity=4;g->blocks=calloc(4,sizeof(*g->blocks));
 g->blocks[0]=(TurboJSSSABlock){.id=0,.first_value=0,.value_count=6,.successors={1},.successor_count=1,.reachable=1};
 g->blocks[1]=(TurboJSSSABlock){.id=1,.first_value=6,.value_count=3,.predecessors={0,2},.predecessor_count=2,.successors={2,3},.successor_count=2,.loop_depth=1,.loop_header=1,.reachable=1};
 g->blocks[2]=(TurboJSSSABlock){.id=2,.first_value=9,.value_count=(uint32_t)(vc-10),.predecessors={1},.predecessor_count=1,.successors={1},.successor_count=1,.loop_depth=1,.reachable=1};
 g->blocks[3]=(TurboJSSSABlock){.id=3,.first_value=(uint32_t)(vc-1),.value_count=1,.predecessors={1},.predecessor_count=1,.reachable=1};
 g->value_count=g->value_capacity=vc;g->values=calloc(vc,sizeof(*g->values));
 g->values[0]=(TurboJSSSAValue){.id=0,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_REFERENCE,.immediate=0};
 g->values[1]=(TurboJSSSAValue){.id=1,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_REFERENCE,.immediate=1};
 g->values[2]=(TurboJSSSAValue){.id=2,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_INT32,.immediate=2};
 g->values[3]=(TurboJSSSAValue){.id=3,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_FLOAT64,.immediate=3};
 g->values[4]=(TurboJSSSAValue){.id=4,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_FLOAT64,.immediate=4};
 g->values[5]=(TurboJSSSAValue){.id=5,.block=0,.opcode=TURBOJS_SSA_CONSTANT_I64,.type=TURBOJS_SSA_TYPE_INT32,.immediate=0};
 g->values[6]=(TurboJSSSAValue){.id=6,.block=1,.opcode=TURBOJS_SSA_PHI,.type=TURBOJS_SSA_TYPE_INT32,.left=5,.right=(uint32_t)(vc-3)};
 g->values[7]=(TurboJSSSAValue){.id=7,.block=1,.opcode=TURBOJS_SSA_LESS_THAN_I64,.type=TURBOJS_SSA_TYPE_BOOLEAN,.left=6,.right=2};
 g->values[8]=(TurboJSSSAValue){.id=8,.block=1,.opcode=TURBOJS_SSA_BRANCH_FALSE,.left=7,.metadata=3};
 g->values[9]=(TurboJSSSAValue){.id=9,.block=2,.opcode=TURBOJS_SSA_ELEMENT_LOAD,.type=TURBOJS_SSA_TYPE_FLOAT64,.left=0,.right=6,.element_kind=TURBOJS_ELEMENT_KIND_TYPED_F64,.element_generation=41,.element_length_value=2,.element_bounds_proven=1,.element_length_hoisted=1,.element_base_hoisted=1};
 if(mode==0) g->values[10]=(TurboJSSSAValue){.id=10,.block=2,.opcode=TURBOJS_SSA_MIN_F64,.type=TURBOJS_SSA_TYPE_FLOAT64,.left=9,.right=4};
 else if(mode==1) g->values[10]=(TurboJSSSAValue){.id=10,.block=2,.opcode=TURBOJS_SSA_MAX_F64,.type=TURBOJS_SSA_TYPE_FLOAT64,.left=9,.right=3};
 else {g->values[10]=(TurboJSSSAValue){.id=10,.block=2,.opcode=TURBOJS_SSA_MAX_F64,.type=TURBOJS_SSA_TYPE_FLOAT64,.left=9,.right=3};g->values[11]=(TurboJSSSAValue){.id=11,.block=2,.opcode=TURBOJS_SSA_MIN_F64,.type=TURBOJS_SSA_TYPE_FLOAT64,.left=10,.right=4};}
 {uint32_t expr=mode==2?11:10,store=mode==2?12:11,one=store+1,step=store+2,jump=store+3,ret=store+4;
 g->values[store]=(TurboJSSSAValue){.id=store,.block=2,.opcode=TURBOJS_SSA_ELEMENT_STORE,.type=TURBOJS_SSA_TYPE_REFERENCE,.left=1,.right=6,.metadata=expr,.element_kind=TURBOJS_ELEMENT_KIND_TYPED_F64,.element_generation=42,.element_flags=TURBOJS_ELEMENT_FLAG_WRITABLE,.element_length_value=2,.element_bounds_proven=1,.element_length_hoisted=1,.element_base_hoisted=1};
 g->values[one]=(TurboJSSSAValue){.id=one,.block=2,.opcode=TURBOJS_SSA_CONSTANT_I64,.type=TURBOJS_SSA_TYPE_INT32,.immediate=1};
 g->values[step]=(TurboJSSSAValue){.id=step,.block=2,.opcode=TURBOJS_SSA_ADD_I64,.type=TURBOJS_SSA_TYPE_INT32,.left=6,.right=one};
 g->values[jump]=(TurboJSSSAValue){.id=jump,.block=2,.opcode=TURBOJS_SSA_JUMP,.metadata=1};
 g->values[ret]=(TurboJSSSAValue){.id=ret,.block=3,.opcode=TURBOJS_SSA_RETURN,.type=TURBOJS_SSA_TYPE_INT32,.left=2};}
}
static int run(int mode){TurboJSSSAGraph g;TurboJSRegionNativeFunction*n=0;TurboJSRegionNativeStats st={0};TurboJSIRDiagnostic d={0};TurboJSRegionValue args[5],r=0;TurboJSRegionValueOps ops={.guard_shape=gs,.load_own_slot=ls,.to_i64=ti,.from_i64=fi};A s={TURBOJS_ELEMENT_KIND_TYPED_F64,0,41,11,{-4,-2,-1,-0.0,1,2,3,4,5,6,NAN}},t={TURBOJS_ELEMENT_KIND_TYPED_F64,TURBOJS_ELEMENT_FLAG_WRITABLE,42,11,{0}};TurboJSRegionElementLayout l={.object_pointer_mask=UINT64_MAX,.kind_offset=offsetof(A,kind),.generation_offset=offsetof(A,generation),.length_offset=offsetof(A,length),.element_storage_offset=offsetof(A,values),.element_stride=8};build(&g,mode);args[0]=(uintptr_t)&s;args[1]=(uintptr_t)&t;args[2]=11;args[3]=bits(-1.5);args[4]=bits(4.5);CHECK(TurboJS_RegionNativeCompileWithElementLayout(&g,&l,&n,&st,&d)==TURBOJS_IR_OK);CHECK(st.inline_element_transform_loops==1);CHECK(mode==0?st.inline_element_min_loops==1:(mode==1?st.inline_element_max_loops==1:st.inline_element_clamp_loops==1));CHECK(TurboJS_RegionNativeInvokeValues(n,args,5,&ops,0,&r)==TURBOJS_IR_OK);for(int i=0;i<11;i++){double e=s.values[i];if(mode==0){if(e>4.5)e=4.5;}else if(mode==1){if(e< -1.5)e=-1.5;}else{if(e< -1.5)e=-1.5;if(e>4.5)e=4.5;}if(isnan(e))CHECK(isnan(t.values[i]));else {CHECK(fabs(t.values[i]-e)<1e-12);if(e==0.0)CHECK(signbit(t.values[i])==signbit(e));}}if(mode==2){args[3]=bits(5.0);args[4]=bits(4.0);CHECK(TurboJS_RegionNativeInvokeValues(n,args,5,&ops,0,&r)==TURBOJS_IR_BAILOUT);}TurboJS_RegionNativeFunctionDestroy(n);TurboJS_SSAGraphDestroy(&g);return 0;}
int main(void){CHECK(run(0)==0);CHECK(run(1)==0);CHECK(run(2)==0);puts("Float64 min/max/clamp SIMD test passed");return 0;}
