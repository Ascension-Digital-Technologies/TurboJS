#include "jit.h"
#include <limits.h>
#include <string.h>
void TurboJS_OSRStateInit(TurboJSOSRState *s,uint32_t header,uint32_t threshold){if(!s)return;memset(s,0,sizeof(*s));s->loop_header=header;s->compile_threshold=threshold?threshold:1;}
TurboJSOSRDecision TurboJS_OSRObserveBackedge(TurboJSOSRState *s){if(!s||s->disabled)return TURBOJS_OSR_DISABLED;if(s->backedge_count!=UINT32_MAX)s->backedge_count++;if(s->code_ready)return TURBOJS_OSR_ENTER_READY_CODE;if(!s->compilation_requested&&s->backedge_count>=s->compile_threshold){s->compilation_requested=1;return TURBOJS_OSR_REQUEST_COMPILE;}return TURBOJS_OSR_CONTINUE_INTERPRETING;}
void TurboJS_OSRMarkCodeReady(TurboJSOSRState *s){if(s&&!s->disabled)s->code_ready=1;}
void TurboJS_OSRRecordEntry(TurboJSOSRState *s){if(s&&s->entry_count!=UINT32_MAX)s->entry_count++;}
void TurboJS_OSRRecordBailout(TurboJSOSRState *s,uint32_t limit){if(!s)return;if(s->bailout_count!=UINT32_MAX)s->bailout_count++;s->code_ready=0;if(limit&&s->bailout_count>=limit)s->disabled=1;}
