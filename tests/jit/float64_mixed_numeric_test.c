#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "jit.h"

static int64_t bits(double v) { int64_t x; memcpy(&x, &v, sizeof(x)); return x; }
static double number(int64_t v) { double x; memcpy(&x, &v, sizeof(x)); return x; }

static int emit(TurboJSIRFunction *f, TurboJSIROpcode op, uint16_t d, uint16_t l, uint16_t r, int64_t imm) {
    TurboJSIRInstruction in = {0}; in.opcode=op; in.destination=d; in.left=l; in.right=r; in.immediate=imm;
    return TurboJS_IREmit(f,in)==TURBOJS_IR_OK;
}

int main(void) {
    TurboJSIRFunction f; TurboJSNativeFunction *native=NULL; TurboJSIRDiagnostic d; int64_t out=0; double v;
    TurboJS_IRFunctionInit(&f,0);
    for (int i=0;i<9;i++) if (TurboJS_IRAllocateRegister(&f)==TURBOJS_IR_NO_REGISTER) return 1;
    if (!emit(&f,TURBOJS_IR_CONSTANT_I64,0,0,0,7) ||
        !emit(&f,TURBOJS_IR_I64_TO_F64,1,0,0,0) ||
        !emit(&f,TURBOJS_IR_CONSTANT_F64,2,0,0,bits(7.5)) ||
        !emit(&f,TURBOJS_IR_LESS_THAN_F64,3,1,2,0) ||
        !emit(&f,TURBOJS_IR_LESS_EQUAL_F64,4,2,2,0) ||
        !emit(&f,TURBOJS_IR_EQUAL_F64,5,2,2,0) ||
        !emit(&f,TURBOJS_IR_CONSTANT_F64,6,0,0,bits(NAN)) ||
        !emit(&f,TURBOJS_IR_EQUAL_F64,7,6,6,0) ||
        !emit(&f,TURBOJS_IR_F64_TO_I64_TRUNC,8,2,0,0) ||
        !emit(&f,TURBOJS_IR_RETURN_I64,0,8,0,0)) return 1;
    if (TurboJS_IRExecute(&f,NULL,0,&out)!=TURBOJS_IR_OK || out!=7) return 2;
    if (TurboJS_BaselineCompile(&f,&native,&d)!=TURBOJS_IR_OK) { fprintf(stderr,"compile: %s\n",d.message); return 3; }
    if (TurboJS_NativeInvoke(native,NULL,0,&out)!=TURBOJS_IR_OK || out!=7) return 4;
    TurboJS_NativeFunctionDestroy(native); TurboJS_IRFunctionDestroy(&f);

    TurboJS_IRFunctionInit(&f,0);
    for (int i=0;i<4;i++) TurboJS_IRAllocateRegister(&f);
    emit(&f,TURBOJS_IR_CONSTANT_F64,0,0,0,bits(1.25)); emit(&f,TURBOJS_IR_CONSTANT_F64,1,0,0,bits(2.0));
    emit(&f,TURBOJS_IR_ADD_F64,2,0,1,0); emit(&f,TURBOJS_IR_RETURN_F64,0,2,0,0);
    if (TurboJS_IRExecute(&f,NULL,0,&out)!=TURBOJS_IR_OK) return 5;
    v=number(out);
    if (v!=3.25) return 6;
    if (TurboJS_BaselineCompile(&f,&native,&d)!=TURBOJS_IR_OK || TurboJS_NativeInvoke(native,NULL,0,&out)!=TURBOJS_IR_OK || number(out)!=3.25) return 7;
    TurboJS_NativeFunctionDestroy(native); TurboJS_IRFunctionDestroy(&f);
    puts("Float64 comparisons and mixed numeric conversions passed");
    return 0;
}
