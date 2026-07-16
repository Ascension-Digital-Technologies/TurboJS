#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "jit.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed: %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)

static TurboJSIRInstruction make(TurboJSIROpcode op,uint16_t d,uint16_t l,uint16_t r,int64_t imm){TurboJSIRInstruction i;memset(&i,0,sizeof(i));i.opcode=op;i.destination=d;i.left=l;i.right=r;i.immediate=imm;return i;}

static int run_case(TurboJSIROpcode op,int64_t a,int64_t b,int expect_bailout,int64_t expected){
    TurboJSIRFunction ir; TurboJSNativeFunction *native=NULL; TurboJSIRDiagnostic d; int64_t args[2]={a,b}, iv=0,nv=0; TurboJSIRStatus is,ns;
    uint16_t ra,rb,rr;
    TurboJS_IRFunctionInit(&ir,2); ra=TurboJS_IRAllocateRegister(&ir); rb=TurboJS_IRAllocateRegister(&ir); rr=TurboJS_IRAllocateRegister(&ir);
    CHECK(TurboJS_IREmit(&ir,make(TURBOJS_IR_ARGUMENT,ra,0,0,0))==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir,make(TURBOJS_IR_ARGUMENT,rb,0,0,1))==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir,make(op,rr,ra,rb,0))==TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir,make(TURBOJS_IR_RETURN_I64,0,rr,0,0))==TURBOJS_IR_OK);
    CHECK(TurboJS_BaselineCompile(&ir,&native,&d)==TURBOJS_IR_OK);
    is=TurboJS_IRExecute(&ir,args,2,&iv); ns=TurboJS_NativeInvoke(native,args,2,&nv);
    if(expect_bailout){CHECK(is==TURBOJS_IR_BAILOUT);CHECK(ns==TURBOJS_IR_BAILOUT);CHECK(TurboJS_NativeLastBailout(native).reason==TURBOJS_BAILOUT_INTEGER_OVERFLOW);}else{CHECK(is==TURBOJS_IR_OK);CHECK(ns==TURBOJS_IR_OK);CHECK(iv==expected);CHECK(nv==expected);}
    TurboJS_NativeFunctionDestroy(native); TurboJS_IRFunctionDestroy(&ir); return 0;
}
int main(void){
    CHECK(run_case(TURBOJS_IR_ADD_I32_CHECKED,10,20,0,30)==0);
    CHECK(run_case(TURBOJS_IR_ADD_I32_CHECKED,INT32_MAX,1,1,0)==0);
    CHECK(run_case(TURBOJS_IR_SUB_I32_CHECKED,INT32_MIN,1,1,0)==0);
    CHECK(run_case(TURBOJS_IR_MUL_I32_CHECKED,46340,46340,0,2147395600)==0);
    CHECK(run_case(TURBOJS_IR_MUL_I32_CHECKED,46341,46341,1,0)==0);
    puts("checked arithmetic and native bailout tests passed"); return 0;
}
