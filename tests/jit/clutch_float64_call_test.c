#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "jit.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed: %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)
int main(void){
 TurboJSIRFunction callee,caller; TurboJSCodeCache *cache; const TurboJSNativeFunction *nc=NULL; TurboJSNativeFunction *nr=NULL;
 TurboJSNativeEntryHandle h; TurboJSClutchCallSite site; TurboJSIRDiagnostic d; uint16_t a,b,r; int key = 0; double args[2]={1.25,2.5}, out=0.0;
 TurboJS_IRFunctionInit(&callee,2); a=TurboJS_IRAllocateRegister(&callee); b=TurboJS_IRAllocateRegister(&callee); r=TurboJS_IRAllocateRegister(&callee);
 CHECK(TurboJS_IREmit(&callee,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,0,0,0})==TURBOJS_IR_OK);
 CHECK(TurboJS_IREmit(&callee,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,b,0,0,1,0,1})==TURBOJS_IR_OK);
 CHECK(TurboJS_IREmit(&callee,(TurboJSIRInstruction){TURBOJS_IR_ADD_F64,r,a,b,0,0,2})==TURBOJS_IR_OK);
 CHECK(TurboJS_IREmit(&callee,(TurboJSIRInstruction){TURBOJS_IR_RETURN_F64,0,r,0,0,0,3})==TURBOJS_IR_OK);
 cache=TurboJS_CodeCacheCreate(8,1u<<20); CHECK(cache); CHECK(TurboJS_CodeCacheCompile(cache,&key,&callee,&nc,&d)==TURBOJS_IR_OK);
 TurboJS_NativeEntryHandleInit(&h); CHECK(TurboJS_CodeCacheAttachEntryHandle(cache,&key,&h,TURBOJS_NATIVE_ENTRY_FLOAT64,2)==TURBOJS_IR_OK);
 TurboJS_ClutchCallSiteInit(&site,&h,h.generation,TURBOJS_NATIVE_ENTRY_FLOAT64,2);
 TurboJS_IRFunctionInit(&caller,2); a=TurboJS_IRAllocateRegister(&caller); b=TurboJS_IRAllocateRegister(&caller); r=TurboJS_IRAllocateRegister(&caller);
 CHECK(TurboJS_ClutchCallSiteSetArgument(&site,0,a)==TURBOJS_IR_OK); CHECK(TurboJS_ClutchCallSiteSetArgument(&site,1,b)==TURBOJS_IR_OK);
 CHECK(TurboJS_IREmit(&caller,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,a,0,0,0,0,0})==TURBOJS_IR_OK);
 CHECK(TurboJS_IREmit(&caller,(TurboJSIRInstruction){TURBOJS_IR_ARGUMENT,b,0,0,1,0,1})==TURBOJS_IR_OK);
 CHECK(TurboJS_IREmit(&caller,(TurboJSIRInstruction){TURBOJS_IR_CALL_NATIVE_F64,r,0,0,(int64_t)(uintptr_t)&site,0,2})==TURBOJS_IR_OK);
 CHECK(TurboJS_IREmit(&caller,(TurboJSIRInstruction){TURBOJS_IR_RETURN_F64,0,r,0,0,0,3})==TURBOJS_IR_OK);
 CHECK(TurboJS_BaselineCompile(&caller,&nr,&d)==TURBOJS_IR_OK); CHECK(TurboJS_NativeInvokeF64(nr,args,2,&out)==TURBOJS_IR_OK); CHECK(fabs(out-3.75)<1e-12);
 CHECK(TurboJS_NativeStackMapAt(nr,0)->kind==TURBOJS_SAFEPOINT_CLUTCH_CALL); TurboJS_CodeCacheClear(cache); CHECK(TurboJS_NativeInvokeF64(nr,args,2,&out)==TURBOJS_IR_BAILOUT);
 TurboJS_NativeFunctionDestroy(nr); TurboJS_IRFunctionDestroy(&caller); TurboJS_CodeCacheDestroy(cache); TurboJS_IRFunctionDestroy(&callee); puts("TurboJS Clutch Float64 call passed"); return 0;
}
