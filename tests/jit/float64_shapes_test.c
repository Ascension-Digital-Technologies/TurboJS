#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "jit.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed: %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)

static int emit(TurboJSIRFunction *f, TurboJSIROpcode op, uint16_t d, uint16_t l, uint16_t r, int64_t imm){
    TurboJSIRInstruction in; memset(&in,0,sizeof(in)); in.opcode=op;in.destination=d;in.left=l;in.right=r;in.immediate=imm; return TurboJS_IREmit(f,in)==TURBOJS_IR_OK;
}
int main(void){
    TurboJSIRFunction f; TurboJSNativeFunction *native=NULL; TurboJSIRDiagnostic diag;
    double args[2]={1.5,2.25}, interpreted=0,native_result=0; uint16_t a,b,sum,c,product;
    TurboJSShapeTable *table; const TurboJSShape *root,*xy,*xy2; TurboJSPropertyInlineCache cache; uint16_t offset=99;
    TurboJS_IRFunctionInit(&f,2);a=TurboJS_IRAllocateRegister(&f);b=TurboJS_IRAllocateRegister(&f);sum=TurboJS_IRAllocateRegister(&f);c=TurboJS_IRAllocateRegister(&f);product=TurboJS_IRAllocateRegister(&f);
    CHECK(emit(&f,TURBOJS_IR_ARGUMENT,a,0,0,0));CHECK(emit(&f,TURBOJS_IR_ARGUMENT,b,0,0,1));
    CHECK(emit(&f,TURBOJS_IR_ADD_F64,sum,a,b,0));
    { double v=2.0; int64_t bits; memcpy(&bits,&v,sizeof(bits)); CHECK(emit(&f,TURBOJS_IR_CONSTANT_F64,c,0,0,bits)); }
    CHECK(emit(&f,TURBOJS_IR_MUL_F64,product,sum,c,0));CHECK(emit(&f,TURBOJS_IR_RETURN_F64,0,product,0,0));
    CHECK(TurboJS_IRExecuteF64(&f,args,2,&interpreted)==TURBOJS_IR_OK);CHECK(fabs(interpreted-7.5)<1e-12);
    CHECK(TurboJS_BaselineCompile(&f,&native,&diag)==TURBOJS_IR_OK);CHECK(TurboJS_NativeInvokeF64(native,args,2,&native_result)==TURBOJS_IR_OK);CHECK(fabs(native_result-7.5)<1e-12);
    table=TurboJS_ShapeTableCreate();CHECK(table);root=TurboJS_ShapeRoot(table);CHECK(root);xy=TurboJS_ShapeTransition(table,root,"x");CHECK(xy);xy=TurboJS_ShapeTransition(table,xy,"y");CHECK(xy);xy2=TurboJS_ShapeTransition(table,TurboJS_ShapeTransition(table,root,"x"),"y");CHECK(xy2==xy);CHECK(TurboJS_ShapePropertyCount(xy)==2);CHECK(TurboJS_ShapeLookup(xy,"y",&offset)&&offset==1);
    TurboJS_PropertyInlineCacheInit(&cache);CHECK(TurboJS_PropertyInlineCacheLookup(&cache,xy,"x",&offset)&&offset==0);CHECK(cache.misses==1);CHECK(TurboJS_PropertyInlineCacheLookup(&cache,xy,"x",&offset)&&offset==0);CHECK(cache.hits==1);
    TurboJS_ShapeTableDestroy(table);TurboJS_NativeFunctionDestroy(native);TurboJS_IRFunctionDestroy(&f);puts("Float64 native execution and shape inline caches passed");return 0;
}
