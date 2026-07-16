#include <stdio.h>
#include <string.h>
#include "jit.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"check failed: %s:%d: %s\n",__FILE__,__LINE__,#x);return 1;}}while(0)
static int emit(TurboJSIRFunction*f,TurboJSIROpcode op,uint16_t d,uint16_t l,uint16_t r,int64_t imm,uint32_t t){TurboJSIRInstruction in={op,d,l,r,imm,t};return TurboJS_IREmit(f,in)==TURBOJS_IR_OK;}
int main(void){TurboJSIRFunction a,b;TurboJSAOTBuffer image={0};TurboJSIRDiagnostic d;TurboJSNativeFunction*n=NULL;int64_t args[2]={40,2},x=0,y=0;TurboJSBailoutInfo bi;
 TurboJS_IRFunctionInit(&a,2);CHECK(TurboJS_IRAllocateRegister(&a)==0);CHECK(TurboJS_IRAllocateRegister(&a)==1);CHECK(TurboJS_IRAllocateRegister(&a)==2);
 CHECK(emit(&a,TURBOJS_IR_ARGUMENT,0,0,0,0,0));CHECK(emit(&a,TURBOJS_IR_ARGUMENT,1,0,0,1,0));CHECK(emit(&a,TURBOJS_IR_ADD_I32_CHECKED,2,0,1,0,0));CHECK(emit(&a,TURBOJS_IR_RETURN_I64,0,2,0,0,0));
 CHECK(TurboJS_AOTSerializeIR(&a,&image,&d)==TURBOJS_IR_OK);CHECK(image.size>32);CHECK(TurboJS_AOTDeserializeIR(image.data,image.size,&b,&d)==TURBOJS_IR_OK);
 CHECK(TurboJS_IRExecute(&b,args,2,&x)==TURBOJS_IR_OK);CHECK(TurboJS_BaselineCompile(&b,&n,&d)==TURBOJS_IR_OK);CHECK(TurboJS_NativeInvoke(n,args,2,&y)==TURBOJS_IR_OK);CHECK(x==42&&y==42);
 args[0]=INT32_MAX;args[1]=1;CHECK(TurboJS_NativeInvoke(n,args,2,&y)==TURBOJS_IR_BAILOUT);bi=TurboJS_NativeLastBailout(n);CHECK(bi.reason==TURBOJS_BAILOUT_INTEGER_OVERFLOW);CHECK(bi.instruction_index==2);
 image.data[image.size-1]^=1;TurboJS_IRFunctionInit(&a,0);CHECK(TurboJS_AOTDeserializeIR(image.data,image.size,&a,&d)!=TURBOJS_IR_OK);
 TurboJS_NativeFunctionDestroy(n);TurboJS_IRFunctionDestroy(&b);TurboJS_IRFunctionDestroy(&a);TurboJS_AOTBufferDestroy(&image);puts("portable AOT IR and precise bailout tests passed");return 0;}
