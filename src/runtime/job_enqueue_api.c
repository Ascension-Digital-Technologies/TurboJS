/* Independently compiled job enqueue facade. */
#include <turbojs.h>
#include "internal/job_enqueue_bridge.h"
int JS_EnqueueJob(JSContext *ctx, JSJobFunc *job_func, int argc, JSValueConst *argv) { return turbojs_internal_enqueue_job(ctx, job_func, argc, argv); }
