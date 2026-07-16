/* Independently compiled runtime job queue facade. */
#include <turbojs.h>
#include "internal/job_bridge.h"

bool JS_IsJobPending(JSRuntime *rt)
{
    return turbojs_internal_is_job_pending(rt);
}

JSContext *JS_GetPendingJobContext(JSRuntime *rt)
{
    return turbojs_internal_get_pending_job_context(rt);
}

int JS_ExecutePendingJob(JSRuntime *rt, JSContext **pctx)
{
    return turbojs_internal_execute_pending_job(rt, pctx);
}
