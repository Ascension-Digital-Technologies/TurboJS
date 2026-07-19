#include "jit.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"check failed %s:%d: %s\n",__FILE__,__LINE__,#x);return 1;}}while(0)
static TurboJSMoveLocation reg(TurboJSRegisterClass c,int i){TurboJSMoveLocation x={TURBOJS_MOVE_REGISTER,c,(int16_t)i};return x;}
static TurboJSMoveLocation scratch(TurboJSRegisterClass c){TurboJSMoveLocation x={TURBOJS_MOVE_SCRATCH,c,0};return x;}
int main(void){
 TurboJSOSRFrame f; TurboJSOSRValue l[3]={{11,TURBOJS_OSR_VALUE_INT64,0},{0x3ff8000000000000ULL,TURBOJS_OSR_VALUE_FLOAT64,0},{0x1234,TURBOJS_OSR_VALUE_REFERENCE,0}}, st[2]={{7,TURBOJS_OSR_VALUE_INT64,0},{0,TURBOJS_OSR_VALUE_EMPTY,0}}, lo[3],so[2];
 memset(&f,0,sizeof(f)); CHECK(TurboJS_OSRFrameInit(&f,3,2)==TURBOJS_IR_OK); CHECK(TurboJS_OSRFrameCapture(&f,l,3,st,2,44,9)==TURBOJS_IR_OK); CHECK(f.bytecode_offset==44&&f.loop_header==9); CHECK(TurboJS_OSRFrameRestore(&f,lo,3,so,2)==TURBOJS_IR_OK); CHECK(!memcmp(l,lo,sizeof(l))&&!memcmp(st,so,sizeof(st))); TurboJS_OSRFrameDestroy(&f);
 {
  TurboJSParallelMove m[3]={{reg(TURBOJS_REGISTER_CLASS_INTEGER,0),reg(TURBOJS_REGISTER_CLASS_INTEGER,1)},{reg(TURBOJS_REGISTER_CLASS_INTEGER,1),reg(TURBOJS_REGISTER_CLASS_INTEGER,2)},{reg(TURBOJS_REGISTER_CLASS_INTEGER,2),reg(TURBOJS_REGISTER_CLASS_INTEGER,0)}}; TurboJSMoveSequence seq;
  CHECK(TurboJS_ResolveParallelMoves(m,3,scratch(TURBOJS_REGISTER_CLASS_INTEGER),scratch(TURBOJS_REGISTER_CLASS_FLOAT64),&seq)==TURBOJS_IR_OK); CHECK(seq.count==4); CHECK(seq.moves[0].destination.kind==TURBOJS_MOVE_SCRATCH); TurboJS_MoveSequenceDestroy(&seq);
 }
 puts("OSR frame snapshots and parallel moves passed"); return 0;
}
