#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "jit.h"

typedef struct MockArray {
    uint16_t kind;
    uint16_t flags;
    uint32_t generation;
    uint32_t length;
    TurboJSRegionValue values[8];
} MockArray;

static int guard_shape(TurboJSRegionValue v, uintptr_t s, void *o){(void)v;(void)s;(void)o;return 0;}
static int load_slot(TurboJSRegionValue v,uint32_t i,TurboJSRegionValue*out,void*o){(void)v;(void)i;(void)out;(void)o;return 0;}
static int store_slot(TurboJSRegionValue v,uint32_t i,TurboJSRegionValue x,void*o){(void)v;(void)i;(void)x;(void)o;return 0;}
static int guard_elements(TurboJSRegionValue v,uint16_t k,uint32_t g,uint16_t f,void*o){MockArray*a=(MockArray*)(uintptr_t)v;(void)o;return a&&a->kind==k&&a->generation==g&&(a->flags&f)==f;}
static int load_element(TurboJSRegionValue v,uint32_t i,TurboJSRegionValue*out,void*o){MockArray*a=(MockArray*)(uintptr_t)v;(void)o;if(!a||!out||i>=a->length)return 0;*out=a->values[i];return 1;}
static int store_element(TurboJSRegionValue v,uint32_t i,TurboJSRegionValue x,void*o){MockArray*a=(MockArray*)(uintptr_t)v;(void)o;if(!a||i>=a->length)return 0;a->values[i]=x;return 1;}
static int array_length(TurboJSRegionValue v,uint32_t*out,void*o){MockArray*a=(MockArray*)(uintptr_t)v;(void)o;if(!a||!out)return 0;*out=a->length;return 1;}
static int to_i64(TurboJSRegionValue v,int64_t*out,void*o){(void)o;if(!out)return 0;*out=(int64_t)v;return 1;}
static TurboJSRegionValue from_i64(int64_t v,void*o){(void)o;return (TurboJSRegionValue)v;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"check failed:%d: %s\n",__LINE__,#x);return 1;}}while(0)

static void build_graph(TurboJSSSAGraph*g,int store){
    memset(g,0,sizeof(*g));g->entry_block=0;g->block_count=g->block_capacity=1;g->blocks=calloc(1,sizeof(*g->blocks));g->blocks[0].reachable=1;g->blocks[0].value_count=store?5:4;
    g->value_count=g->value_capacity=store?5:4;g->values=calloc(g->value_count,sizeof(*g->values));
    g->values[0]=(TurboJSSSAValue){.id=0,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_REFERENCE,.immediate=0};
    g->values[1]=(TurboJSSSAValue){.id=1,.block=0,.opcode=TURBOJS_SSA_CONSTANT_I64,.type=TURBOJS_SSA_TYPE_INT64,.immediate=2};
    if(store){
      g->values[2]=(TurboJSSSAValue){.id=2,.block=0,.opcode=TURBOJS_SSA_CONSTANT_I64,.type=TURBOJS_SSA_TYPE_INT64,.immediate=99};
      g->values[3]=(TurboJSSSAValue){.id=3,.block=0,.opcode=TURBOJS_SSA_ELEMENT_STORE,.type=TURBOJS_SSA_TYPE_REFERENCE,.left=0,.right=1,.metadata=2,.element_kind=TURBOJS_ELEMENT_KIND_PACKED_I64,.element_flags=TURBOJS_ELEMENT_FLAG_WRITABLE,.element_generation=7};
      g->values[4]=(TurboJSSSAValue){.id=4,.block=0,.opcode=TURBOJS_SSA_RETURN,.type=TURBOJS_SSA_TYPE_INT64,.left=2};
    }else{
      g->values[2]=(TurboJSSSAValue){.id=2,.block=0,.opcode=TURBOJS_SSA_ELEMENT_LOAD,.type=TURBOJS_SSA_TYPE_REFERENCE,.left=0,.right=1,.element_kind=TURBOJS_ELEMENT_KIND_PACKED_I64,.element_flags=0,.element_generation=7};
      g->values[3]=(TurboJSSSAValue){.id=3,.block=0,.opcode=TURBOJS_SSA_RETURN,.type=TURBOJS_SSA_TYPE_REFERENCE,.left=2};
    }
}

static void build_dynamic_load_graph(TurboJSSSAGraph*g){
    memset(g,0,sizeof(*g));g->entry_block=0;g->block_count=g->block_capacity=1;g->blocks=calloc(1,sizeof(*g->blocks));g->blocks[0].reachable=1;g->blocks[0].value_count=4;
    g->value_count=g->value_capacity=4;g->values=calloc(g->value_count,sizeof(*g->values));
    g->values[0]=(TurboJSSSAValue){.id=0,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_REFERENCE,.immediate=0};
    g->values[1]=(TurboJSSSAValue){.id=1,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_INT32,.immediate=1};
    g->values[2]=(TurboJSSSAValue){.id=2,.block=0,.opcode=TURBOJS_SSA_ELEMENT_LOAD,.type=TURBOJS_SSA_TYPE_REFERENCE,.left=0,.right=1,.element_kind=TURBOJS_ELEMENT_KIND_PACKED_I64,.element_generation=7};
    g->values[3]=(TurboJSSSAValue){.id=3,.block=0,.opcode=TURBOJS_SSA_RETURN,.type=TURBOJS_SSA_TYPE_REFERENCE,.left=2};
}


static void build_dynamic_store_graph(TurboJSSSAGraph*g){
    memset(g,0,sizeof(*g));g->entry_block=0;g->block_count=g->block_capacity=1;g->blocks=calloc(1,sizeof(*g->blocks));g->blocks[0].reachable=1;g->blocks[0].value_count=5;
    g->value_count=g->value_capacity=5;g->values=calloc(g->value_count,sizeof(*g->values));
    g->values[0]=(TurboJSSSAValue){.id=0,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_REFERENCE,.immediate=0};
    g->values[1]=(TurboJSSSAValue){.id=1,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_INT32,.immediate=1};
    g->values[2]=(TurboJSSSAValue){.id=2,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_INT64,.immediate=2};
    g->values[3]=(TurboJSSSAValue){.id=3,.block=0,.opcode=TURBOJS_SSA_ELEMENT_STORE,.type=TURBOJS_SSA_TYPE_REFERENCE,.left=0,.right=1,.metadata=2,.element_kind=TURBOJS_ELEMENT_KIND_PACKED_I64,.element_flags=TURBOJS_ELEMENT_FLAG_WRITABLE,.element_generation=7};
    g->values[4]=(TurboJSSSAValue){.id=4,.block=0,.opcode=TURBOJS_SSA_RETURN,.type=TURBOJS_SSA_TYPE_INT64,.left=2};
}

int main(void){
    TurboJSSSAGraph g;TurboJSRegionNativeFunction*n=NULL;TurboJSRegionValue result,args[1];TurboJSIRDiagnostic d={0};TurboJSRegionNativeStats st={0};
    TurboJSRegionValueOps ops={.guard_shape=guard_shape,.load_own_slot=load_slot,.store_own_slot=store_slot,.to_i64=to_i64,.from_i64=from_i64,.guard_elements=guard_elements,.load_element=load_element,.store_element=store_element,.array_length=array_length};
    MockArray a={TURBOJS_ELEMENT_KIND_PACKED_I64,TURBOJS_ELEMENT_FLAG_WRITABLE,7,4,{10,20,30,40}};args[0]=(TurboJSRegionValue)(uintptr_t)&a;
    TurboJSRegionElementLayout layout={.object_pointer_mask=UINT64_MAX,.kind_offset=offsetof(MockArray,kind),.generation_offset=offsetof(MockArray,generation),.length_offset=offsetof(MockArray,length),.element_storage_offset=offsetof(MockArray,values),.element_stride=sizeof(TurboJSRegionValue),.element_value_offset=0,.element_storage_indirect=0};
    build_graph(&g,0);CHECK(TurboJS_RegionNativeCompile(&g,&n,&st,&d)==TURBOJS_IR_OK);CHECK(TurboJS_RegionNativeInvokeValues(n,args,1,&ops,NULL,&result)==TURBOJS_IR_OK);CHECK(result==30);TurboJS_RegionNativeFunctionDestroy(n);TurboJS_SSAGraphDestroy(&g);
    build_graph(&g,1);n=NULL;CHECK(TurboJS_RegionNativeCompile(&g,&n,&st,&d)==TURBOJS_IR_OK);CHECK(TurboJS_RegionNativeInvokeValues(n,args,1,&ops,NULL,&result)==TURBOJS_IR_OK);CHECK(a.values[2]==99);TurboJS_RegionNativeFunctionDestroy(n);TurboJS_SSAGraphDestroy(&g);
    a.generation=8;build_graph(&g,0);n=NULL;CHECK(TurboJS_RegionNativeCompile(&g,&n,&st,&d)==TURBOJS_IR_OK);CHECK(TurboJS_RegionNativeInvokeValues(n,args,1,&ops,NULL,&result)==TURBOJS_IR_BAILOUT);TurboJS_RegionNativeFunctionDestroy(n);TurboJS_SSAGraphDestroy(&g);

    /* Direct Gearbox element load/store path. */
    a.generation=7;a.values[2]=30;build_graph(&g,0);n=NULL;memset(&st,0,sizeof(st));CHECK(TurboJS_RegionNativeCompileWithElementLayout(&g,&layout,&n,&st,&d)==TURBOJS_IR_OK);CHECK(st.inline_element_loads==1);CHECK(st.inline_element_kind_guards==1);CHECK(st.inline_element_generation_guards==1);CHECK(st.inline_element_bounds_guards==1);CHECK(TurboJS_RegionNativeInvokeValues(n,args,1,&ops,NULL,&result)==TURBOJS_IR_OK);CHECK(result==30);TurboJS_RegionNativeFunctionDestroy(n);TurboJS_SSAGraphDestroy(&g);
    build_graph(&g,1);n=NULL;memset(&st,0,sizeof(st));CHECK(TurboJS_RegionNativeCompileWithElementLayout(&g,&layout,&n,&st,&d)==TURBOJS_IR_OK);CHECK(st.inline_element_stores==1);CHECK(TurboJS_RegionNativeInvokeValues(n,args,1,&ops,NULL,&result)==TURBOJS_IR_OK);CHECK(a.values[2]==99);TurboJS_RegionNativeFunctionDestroy(n);TurboJS_SSAGraphDestroy(&g);
    a.generation=8;build_graph(&g,0);n=NULL;CHECK(TurboJS_RegionNativeCompileWithElementLayout(&g,&layout,&n,&st,&d)==TURBOJS_IR_OK);CHECK(TurboJS_RegionNativeInvokeValues(n,args,1,&ops,NULL,&result)==TURBOJS_IR_BAILOUT);TurboJS_RegionNativeFunctionDestroy(n);TurboJS_SSAGraphDestroy(&g);a.generation=7;
    {
      TurboJSRegionValue dynamic_args[2];
      dynamic_args[0]=(TurboJSRegionValue)(uintptr_t)&a;dynamic_args[1]=3;
      build_dynamic_load_graph(&g);n=NULL;memset(&st,0,sizeof(st));
      CHECK(TurboJS_RegionNativeCompileWithElementLayout(&g,&layout,&n,&st,&d)==TURBOJS_IR_OK);
      CHECK(st.inline_element_loads==1);CHECK(st.inline_element_dynamic_indexes==1);
      CHECK(TurboJS_RegionNativeInvokeValues(n,dynamic_args,2,&ops,NULL,&result)==TURBOJS_IR_OK);CHECK(result==40);
      dynamic_args[1]=4;CHECK(TurboJS_RegionNativeInvokeValues(n,dynamic_args,2,&ops,NULL,&result)==TURBOJS_IR_BAILOUT);
      dynamic_args[1]=(TurboJSRegionValue)(int64_t)-1;CHECK(TurboJS_RegionNativeInvokeValues(n,dynamic_args,2,&ops,NULL,&result)==TURBOJS_IR_BAILOUT);
      TurboJS_RegionNativeFunctionDestroy(n);TurboJS_SSAGraphDestroy(&g);
    }
    {
      TurboJSRegionValue dynamic_args[3];
      dynamic_args[0]=(TurboJSRegionValue)(uintptr_t)&a;dynamic_args[1]=1;dynamic_args[2]=1234;
      build_dynamic_store_graph(&g);n=NULL;memset(&st,0,sizeof(st));
      CHECK(TurboJS_RegionNativeCompileWithElementLayout(&g,&layout,&n,&st,&d)==TURBOJS_IR_OK);
      CHECK(st.inline_element_stores==1);CHECK(st.inline_element_dynamic_indexes==1);CHECK(st.inline_element_dynamic_stores==1);
      CHECK(TurboJS_RegionNativeInvokeValues(n,dynamic_args,3,&ops,NULL,&result)==TURBOJS_IR_OK);CHECK(result==1234);CHECK(a.values[1]==1234);
      dynamic_args[1]=8;dynamic_args[2]=88;CHECK(TurboJS_RegionNativeInvokeValues(n,dynamic_args,3,&ops,NULL,&result)==TURBOJS_IR_BAILOUT);CHECK(a.values[1]==1234);
      TurboJS_RegionNativeFunctionDestroy(n);TurboJS_SSAGraphDestroy(&g);
    }
    {
      TurboJSSSAOptimizationStats os;
      memset(&g,0,sizeof(g));g.entry_block=0;g.block_count=g.block_capacity=1;g.blocks=calloc(1,sizeof(*g.blocks));g.blocks[0].reachable=1;g.blocks[0].value_count=5;
      g.value_count=g.value_capacity=5;g.values=calloc(5,sizeof(*g.values));
      g.values[0]=(TurboJSSSAValue){.id=0,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_REFERENCE,.immediate=0};
      g.values[1]=(TurboJSSSAValue){.id=1,.block=0,.opcode=TURBOJS_SSA_CONSTANT_I64,.type=TURBOJS_SSA_TYPE_INT64,.immediate=1};
      g.values[2]=(TurboJSSSAValue){.id=2,.block=0,.opcode=TURBOJS_SSA_ELEMENT_LOAD,.type=TURBOJS_SSA_TYPE_REFERENCE,.left=0,.right=1,.element_kind=TURBOJS_ELEMENT_KIND_PACKED_I64,.element_generation=7};
      g.values[3]=(TurboJSSSAValue){.id=3,.block=0,.opcode=TURBOJS_SSA_ELEMENT_LOAD,.type=TURBOJS_SSA_TYPE_REFERENCE,.left=0,.right=1,.element_kind=TURBOJS_ELEMENT_KIND_PACKED_I64,.element_generation=7};
      g.values[4]=(TurboJSSSAValue){.id=4,.block=0,.opcode=TURBOJS_SSA_RETURN,.type=TURBOJS_SSA_TYPE_REFERENCE,.left=3};
      os=TurboJS_SSAOptimize(&g);CHECK(os.element_bounds_checks_eliminated==1);CHECK(g.values[3].removed);CHECK(g.values[4].left==2);TurboJS_SSAGraphDestroy(&g);
    }
    puts("element SSA test passed");return 0;
}
