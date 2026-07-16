#ifndef QJS_INTERNAL_JOB_BRIDGE_H
#define QJS_INTERNAL_JOB_BRIDGE_H

/* Private opaque bridge for independently compiled job queue APIs. */
#include <turbojs.h>

bool turbojs_internal_is_job_pending(JSRuntime *rt);
JSContext *turbojs_internal_get_pending_job_context(JSRuntime *rt);
int turbojs_internal_execute_pending_job(JSRuntime *rt, JSContext **pctx);

#endif
