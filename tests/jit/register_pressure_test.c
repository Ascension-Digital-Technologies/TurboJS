#include <stdio.h>
#include "jit.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
int main(void) {
    TurboJSIRFunction ir; TurboJSNativeFunction *native = NULL; TurboJSIRDiagnostic d;
    int64_t interpreted, compiled; uint16_t r; int i;
    TurboJS_IRFunctionInit(&ir, 0);
    for (i=0;i<64;i++) { r=TurboJS_IRAllocateRegister(&ir); CHECK(r!=(uint16_t)TURBOJS_IR_NO_REGISTER); CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){TURBOJS_IR_CONSTANT_I64,r,0,0,i+1,0})==TURBOJS_IR_OK); }
    for (i=1;i<64;i++) CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){TURBOJS_IR_ADD_I64,0,0,(uint16_t)i,0,0})==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir,(TurboJSIRInstruction){TURBOJS_IR_RETURN_I64,0,0,0,0,0})==TURBOJS_IR_OK);
    CHECK(TurboJS_IRExecute(&ir,NULL,0,&interpreted)==TURBOJS_IR_OK);
    CHECK(TurboJS_BaselineCompile(&ir,&native,&d)==TURBOJS_IR_OK);
    CHECK(TurboJS_NativeInvoke(native,NULL,0,&compiled)==TURBOJS_IR_OK);
    CHECK(interpreted==2080 && compiled==2080);
    printf("64-register pressure test passed; code=%zu bytes\n",TurboJS_NativeCodeSize(native));
    TurboJS_NativeFunctionDestroy(native); TurboJS_IRFunctionDestroy(&ir); return 0;
}
