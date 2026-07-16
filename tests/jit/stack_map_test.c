#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "jit.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed: %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)
static void trace(void *opaque, void *reference) { uintptr_t *seen=(uintptr_t*)opaque; *seen=(uintptr_t)reference; }
int main(void) {
    TurboJSIRFunction f; TurboJSNativeFunction *native=NULL; TurboJSIRDiagnostic d; TurboJSDeoptFrame frame; uintptr_t seen=0;
    uint16_t a,b,c; int64_t args[2]={42,0},result=0; size_t i; int found_bailout=0;
    TurboJS_IRFunctionInit(&f,2); a=TurboJS_IRAllocateRegister(&f); b=TurboJS_IRAllocateRegister(&f); c=TurboJS_IRAllocateRegister(&f);
    CHECK(TurboJS_IREmit(&f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,0,0,10})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,b,0,0,1,0,11})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,(TurboJSIRInstruction){TURBOJS_IR_DIV_I32_CHECKED,c,a,b,0,0,77})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&f,(TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,TURBOJS_IR_NO_REGISTER,c,0,0,0,78})==TURBOJS_IR_OK);
    CHECK(TurboJS_BaselineCompile(&f,&native,&d)==TURBOJS_IR_OK);
    CHECK(TurboJS_NativeStackMapCount(native)>=2);
    for(i=0;i<TurboJS_NativeStackMapCount(native);i++){const TurboJSStackMap*m=TurboJS_NativeStackMapAt(native,i);CHECK(m);if(m->kind==TURBOJS_SAFEPOINT_BAILOUT){CHECK(m->instruction_index==2);CHECK(m->bytecode_offset==77);found_bailout=1;}}
    CHECK(found_bailout); CHECK(TurboJS_NativeInvoke(native,args,2,&result)==TURBOJS_IR_BAILOUT);
    frame=TurboJS_NativeLastDeoptFrame(native); ((TurboJSValueKind*)frame.register_kinds)[a]=TURBOJS_VALUE_HEAP_REFERENCE; ((int64_t*)frame.register_values)[a]=(int64_t)(uintptr_t)0x1234u;
    TurboJS_TraceDeoptFrame(&frame,trace,&seen); CHECK(seen==(uintptr_t)0x1234u);
    TurboJS_NativeFunctionDestroy(native); TurboJS_IRFunctionDestroy(&f); puts("stack maps and GC tracing hooks passed"); return 0;
}
