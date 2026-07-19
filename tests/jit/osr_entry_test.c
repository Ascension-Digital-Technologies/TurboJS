#include "jit.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed: %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)
static TurboJSOSRExitKind complete(TurboJSOSRFrame *f, void *opaque, uint32_t *resume) {
    (void)opaque; f->locals[0].bits += 10; *resume = 88; return TURBOJS_OSR_EXIT_COMPLETED;
}
static TurboJSOSRExitKind bailout(TurboJSOSRFrame *f, void *opaque, uint32_t *resume) {
    (void)opaque; f->locals[0].bits = 999; *resume = 77; return TURBOJS_OSR_EXIT_BAILOUT;
}
int main(void) {
    TurboJSOSRState s; TurboJSOSRFrame f; TurboJSOSRValue l[1]={{5,TURBOJS_OSR_VALUE_INT64,0}};
    TurboJSOSREntry e; TurboJSOSRExecutionResult r;
    memset(&f,0,sizeof(f)); CHECK(TurboJS_OSRFrameInit(&f,1,0)==TURBOJS_IR_OK);
    CHECK(TurboJS_OSRFrameCapture(&f,l,1,NULL,0,44,12)==TURBOJS_IR_OK);
    TurboJS_OSRStateInit(&s,12,1); TurboJS_OSRMarkCodeReady(&s);
    e.callback=complete; e.opaque=NULL; e.loop_header=12; e.bailout_limit=2;
    CHECK(TurboJS_OSRExecuteEntry(&s,&e,&f,&r)==TURBOJS_IR_OK);
    CHECK(r.exit_kind==TURBOJS_OSR_EXIT_COMPLETED && f.locals[0].bits==15 && f.bytecode_offset==88 && s.entry_count==1);
    f.locals[0].bits=5; f.bytecode_offset=44; TurboJS_OSRMarkCodeReady(&s); e.callback=bailout;
    CHECK(TurboJS_OSRExecuteEntry(&s,&e,&f,&r)==TURBOJS_IR_OK);
    CHECK(r.exit_kind==TURBOJS_OSR_EXIT_BAILOUT && r.restored_original_frame && f.locals[0].bits==5 && f.bytecode_offset==44 && s.bailout_count==1);
    TurboJS_OSRFrameDestroy(&f); puts("Executable OSR entry and bailout restoration passed"); return 0;
}
