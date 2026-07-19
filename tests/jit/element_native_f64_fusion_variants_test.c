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
 memset(g,0,sizeof(*g));g->entry_block=0;g->block_count=g->block_capacity=4;g->blocks=calloc(4,sizeof(*g->blocks));
 g->blocks[0]=(TurboJSSSABlock){.id=0,.first_value=0,.value_count=5,.successors={1},.successor_count=1,.reachable=1};
 g->blocks[1]=(TurboJSSSABlock){.id=1,.first_value=5,.value_count=3,.predecessors={0,2},.predecessor_count=2,.successors={2,3},.successor_count=2,.loop_depth=1,.loop_header=1,.reachable=1};
 g->blocks[2]=(TurboJSSSABlock){.id=2,.first_value=8,.value_count=6,.predecessors={1},.predecessor_count=1,.successors={1},.successor_count=1,.loop_depth=1,.reachable=1};
 g->blocks[3]=(TurboJSSSABlock){.id=3,.first_value=14,.value_count=1,.predecessors={1},.predecessor_count=1,.reachable=1};
 g->value_count=g->value_capacity=15;g->values=calloc(15,sizeof(*g->values));
 g->values[0]=(TurboJSSSAValue){.id=0,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_REFERENCE,.immediate=0};
 g->values[1]=(TurboJSSSAValue){.id=1,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_REFERENCE,.immediate=1};
 g->values[2]=(TurboJSSSAValue){.id=2,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_INT32,.immediate=2};
 g->values[3]=(TurboJSSSAValue){.id=3,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_FLOAT64,.immediate=3};
 g->values[4]=(TurboJSSSAValue){.id=4,.block=0,.opcode=TURBOJS_SSA_CONSTANT_I64,.type=TURBOJS_SSA_TYPE_INT32,.immediate=0};
 g->values[5]=(TurboJSSSAValue){.id=5,.block=1,.opcode=TURBOJS_SSA_PHI,.type=TURBOJS_SSA_TYPE_INT32,.left=4,.right=12};
 g->values[6]=(TurboJSSSAValue){.id=6,.block=1,.opcode=TURBOJS_SSA_LESS_THAN_I64,.type=TURBOJS_SSA_TYPE_BOOLEAN,.left=5,.right=2};
 g->values[7]=(TurboJSSSAValue){.id=7,.block=1,.opcode=TURBOJS_SSA_BRANCH_FALSE,.left=6,.metadata=3};
 g->values[8]=(TurboJSSSAValue){.id=8,.block=2,.opcode=TURBOJS_SSA_ELEMENT_LOAD,.type=TURBOJS_SSA_TYPE_FLOAT64,.left=0,.right=5,.element_kind=TURBOJS_ELEMENT_KIND_TYPED_F64,.element_generation=31,.element_length_value=2,.element_bounds_proven=1,.element_length_hoisted=1,.element_base_hoisted=1};
 g->values[9]=(TurboJSSSAValue){.id=9,.block=2,.opcode=mode==1?TURBOJS_SSA_ADD_I64:(mode==2?TURBOJS_SSA_SUB_I64:TURBOJS_SSA_MUL_I64),.type=TURBOJS_SSA_TYPE_FLOAT64,.left=8,.right=3};
 g->values[10]=(TurboJSSSAValue){.id=10,.block=2,.opcode=TURBOJS_SSA_ELEMENT_STORE,.type=TURBOJS_SSA_TYPE_REFERENCE,.left=1,.right=5,.metadata=9,.element_kind=TURBOJS_ELEMENT_KIND_TYPED_F64,.element_generation=32,.element_flags=TURBOJS_ELEMENT_FLAG_WRITABLE,.element_length_value=2,.element_bounds_proven=1,.element_length_hoisted=1,.element_base_hoisted=1};
 g->values[11]=(TurboJSSSAValue){.id=11,.block=2,.opcode=TURBOJS_SSA_CONSTANT_I64,.type=TURBOJS_SSA_TYPE_INT32,.immediate=1};
 g->values[12]=(TurboJSSSAValue){.id=12,.block=2,.opcode=TURBOJS_SSA_ADD_I64,.type=TURBOJS_SSA_TYPE_INT32,.left=5,.right=11};
 g->values[13]=(TurboJSSSAValue){.id=13,.block=2,.opcode=TURBOJS_SSA_JUMP,.metadata=1};
 g->values[14]=(TurboJSSSAValue){.id=14,.block=3,.opcode=TURBOJS_SSA_RETURN,.type=TURBOJS_SSA_TYPE_INT32,.left=2};
}
static int run(int mode){TurboJSSSAGraph g;TurboJSRegionNativeFunction*n=0;TurboJSRegionNativeStats st={0};TurboJSIRDiagnostic d={0};TurboJSRegionValue args[4],r=0;TurboJSRegionValueOps ops={.guard_shape=gs,.load_own_slot=ls,.to_i64=ti,.from_i64=fi};A s={TURBOJS_ELEMENT_KIND_TYPED_F64,0,31,7,{1,2,3,4,5,6,7}},t={TURBOJS_ELEMENT_KIND_TYPED_F64,TURBOJS_ELEMENT_FLAG_WRITABLE,32,7,{0}};TurboJSRegionElementLayout l={.object_pointer_mask=UINT64_MAX,.kind_offset=offsetof(A,kind),.generation_offset=offsetof(A,generation),.length_offset=offsetof(A,length),.element_storage_offset=offsetof(A,values),.element_stride=8};build(&g,mode);args[0]=(uintptr_t)&s;args[1]=(uintptr_t)&t;args[2]=7;args[3]=bits(mode==0?2.0:0.5);CHECK(TurboJS_RegionNativeCompileWithElementLayout(&g,&l,&n,&st,&d)==TURBOJS_IR_OK);CHECK(st.inline_element_transform_loops==1);CHECK(mode==0?st.inline_element_scale_only_loops==1:(mode==1?st.inline_element_bias_only_loops==1:st.inline_element_subtract_only_loops==1));CHECK(TurboJS_RegionNativeInvokeValues(n,args,4,&ops,0,&r)==TURBOJS_IR_OK);for(int i=0;i<7;i++){double e=mode==0?s.values[i]*2.0:(mode==1?s.values[i]+0.5:s.values[i]-0.5);CHECK(fabs(t.values[i]-e)<1e-12);}TurboJS_RegionNativeFunctionDestroy(n);TurboJS_SSAGraphDestroy(&g);return 0;}
int main(void){CHECK(run(0)==0);CHECK(run(1)==0);CHECK(run(2)==0);puts("Float64 fusion variants test passed");return 0;}
