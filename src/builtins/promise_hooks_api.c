/* Independently compiled promise hook facade. */
#include <turbojs.h>
#include "internal/promise_bridge.h"

void JS_SetPromiseHook(JSRuntime *rt, JSPromiseHook promise_hook, void *opaque)
{
    turbojs_internal_set_promise_hook(rt, promise_hook, opaque);
}

void JS_SetHostPromiseRejectionTracker(JSRuntime *rt,
                                       JSHostPromiseRejectionTracker *cb,
                                       void *opaque)
{
    turbojs_internal_set_host_promise_rejection_tracker(rt, cb, opaque);
}
