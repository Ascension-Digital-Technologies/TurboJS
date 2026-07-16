#include <stdint.h>
#include <stdio.h>
#include "jit.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed: %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)
static void *relocate(void *opaque, void *reference) {
    uintptr_t delta=*(const uintptr_t*)opaque;
    return (void *)((uintptr_t)reference+delta);
}
int main(void) {
    TurboJSIRFunction f; TurboJSNativeFunction *native=NULL; TurboJSIRDiagnostic d;
    TurboJSSafepointController controller; TurboJSDeoptFrame frame;
    uint16_t n,zero,one,total,cond; int64_t args[1]={5},result=0; uintptr_t delta=0x1000u;
    TurboJS_IRFunctionInit(&f,1); TurboJS_IRFunctionSetLocalCount(&f,2);
    n=TurboJS_IRAllocateRegister(&f); zero=TurboJS_IRAllocateRegister(&f); one=TurboJS_IRAllocateRegister(&f); total=TurboJS_IRAllocateRegister(&f); cond=TurboJS_IRAllocateRegister(&f);
    CHECK(TurboJS_IREmit(&f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,n,0,0,0,0,1})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,(TurboJSIRInstruction){TURBOJS_IR_CONSTANT_I64,zero,0,0,0,0,2})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,(TurboJSIRInstruction){TURBOJS_IR_CONSTANT_I64,one,0,0,1,0,3})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,(TurboJSIRInstruction){TURBOJS_IR_LOCAL_SET,TURBOJS_IR_NO_REGISTER,n,0,0,0,4})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,(TurboJSIRInstruction){TURBOJS_IR_LOCAL_SET,TURBOJS_IR_NO_REGISTER,zero,0,1,0,5})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,(TurboJSIRInstruction){TURBOJS_IR_LOCAL_GET,n,0,0,0,0,6})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,(TurboJSIRInstruction){TURBOJS_IR_LESS_THAN_I64,cond,zero,n,0,0,7})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,(TurboJSIRInstruction){TURBOJS_IR_BRANCH_FALSE,TURBOJS_IR_NO_REGISTER,cond,0,0,14,8})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,(TurboJSIRInstruction){TURBOJS_IR_LOCAL_GET,total,0,0,1,0,9})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,(TurboJSIRInstruction){TURBOJS_IR_ADD_I64,total,total,n,0,0,10})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,(TurboJSIRInstruction){TURBOJS_IR_LOCAL_SET,TURBOJS_IR_NO_REGISTER,total,0,1,0,11})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,(TurboJSIRInstruction){TURBOJS_IR_SUB_I64,n,n,one,0,0,12})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,(TurboJSIRInstruction){TURBOJS_IR_LOCAL_SET,TURBOJS_IR_NO_REGISTER,n,0,0,0,13})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,(TurboJSIRInstruction){TURBOJS_IR_JUMP,TURBOJS_IR_NO_REGISTER,0,0,0,5,14})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,(TurboJSIRInstruction){TURBOJS_IR_LOCAL_GET,total,0,0,1,0,15})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,(TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,TURBOJS_IR_NO_REGISTER,total,0,0,0,16})==TURBOJS_IR_OK);
    CHECK(TurboJS_BaselineCompile(&f,&native,&d)==TURBOJS_IR_OK);
    TurboJS_SafepointControllerInit(&controller); TurboJS_NativeSetSafepointController(native,&controller); TurboJS_SafepointRequest(&controller);
    CHECK(TurboJS_NativeInvoke(native,args,1,&result)==TURBOJS_IR_BAILOUT);
    frame=TurboJS_NativeLastDeoptFrame(native); CHECK(frame.bailout.reason==TURBOJS_BAILOUT_SAFEPOINT_REQUESTED); CHECK(frame.bailout.bytecode_offset==14);
    ((TurboJSValueKind*)frame.local_kinds)[1]=TURBOJS_VALUE_HEAP_REFERENCE; ((int64_t*)frame.local_values)[1]=(int64_t)(uintptr_t)0x2000u;
    ((TurboJSDeoptFrame*)&frame)->live_local_mask|=((uint64_t)1u<<1); ((TurboJSDeoptFrame*)&frame)->materialized_local_mask|=((uint64_t)1u<<1);
    TurboJS_RelocateDeoptFrame(&frame,relocate,&delta); CHECK((uintptr_t)frame.local_values[1]==0x3000u);
    TurboJS_SafepointClear(&controller); CHECK(TurboJS_NativeInvoke(native,args,1,&result)==TURBOJS_IR_OK); CHECK(result==15);
    TurboJS_NativeFunctionDestroy(native); TurboJS_IRFunctionDestroy(&f); puts("runtime safepoint polling and relocation passed"); return 0;
}
