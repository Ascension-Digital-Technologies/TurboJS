#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "jit.h"
#include "internal/monotonic_clock.h"
static uint64_t now_ns(void){return turbojs_monotonic_now_ns();}
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
static int64_t reference_loop(volatile int64_t start, volatile int64_t limit){volatile int64_t acc=0; while(start<limit){acc+=start; start++;} return acc;}
int main(void){
 TurboJSOSRCountedLoopSpec spec={0,1,2,1,1,77,50000000,TURBOJS_OSR_LOOP_LT}; TurboJSOSRLoopProgram*p=NULL; TurboJSIRDiagnostic d; TurboJSOSRFrame f; TurboJSOSRValue l[3]; TurboJSOSRState s; TurboJSOSREntry e; TurboJSOSRExecutionResult r; uint64_t t0,t1,t2; int64_t ref; const int64_t n=10000000;
 memset(&d,0,sizeof(d)); memset(&f,0,sizeof(f));
 if(TurboJS_OSRCompileCountedI64Loop(&spec,&p,&d)!=TURBOJS_IR_OK)return 1;
 l[0]=(TurboJSOSRValue){0,TURBOJS_OSR_VALUE_INT64,0};l[1]=(TurboJSOSRValue){(uint64_t)n,TURBOJS_OSR_VALUE_INT64,0};l[2]=(TurboJSOSRValue){0,TURBOJS_OSR_VALUE_INT64,0};
 if(TurboJS_OSRFrameInit(&f,3,0)!=TURBOJS_IR_OK||TurboJS_OSRFrameCapture(&f,l,3,NULL,0,4,1)!=TURBOJS_IR_OK)return 1;
 TurboJS_OSRStateInit(&s,1,1);TurboJS_OSRMarkCodeReady(&s);e=TurboJS_OSRLoopProgramEntry(p);
 t0=now_ns();ref=reference_loop(0,n);t1=now_ns();if(TurboJS_OSRExecuteEntry(&s,&e,&f,&r)!=TURBOJS_IR_OK)return 1;t2=now_ns();
 if(ref!=(int64_t)f.locals[2].bits)return 1;
 printf("Reference C loop: %.3f ns/iteration\n",(double)(t1-t0)/(double)n);
 printf("Native OSR loop: %.3f ns/iteration\n",(double)(t2-t1)/(double)n);
 printf("Native code: %zu bytes result=%" PRId64 "\n",TurboJS_OSRLoopProgramCodeSize(p),(int64_t)f.locals[2].bits);
 TurboJS_OSRFrameDestroy(&f);TurboJS_OSRLoopProgramDestroy(p);return 0;
}
