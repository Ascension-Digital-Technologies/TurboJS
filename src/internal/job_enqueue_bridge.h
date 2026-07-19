#ifndef TURBOJS_INTERNAL_JOB_ENQUEUE_BRIDGE_H
#define TURBOJS_INTERNAL_JOB_ENQUEUE_BRIDGE_H
#include <turbojs.h>
int turbojs_internal_enqueue_job(JSContext *ctx, JSJobFunc *job_func, int argc, JSValueConst *argv);
#endif
