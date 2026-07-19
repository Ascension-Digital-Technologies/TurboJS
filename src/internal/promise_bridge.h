#ifndef TURBOJS_INTERNAL_PROMISE_BRIDGE_H
#define TURBOJS_INTERNAL_PROMISE_BRIDGE_H

/* Private opaque bridge for independently compiled promise hook APIs. */
#include <turbojs.h>

void turbojs_internal_set_promise_hook(JSRuntime *rt, JSPromiseHook promise_hook, void *opaque);
void turbojs_internal_set_host_promise_rejection_tracker(
    JSRuntime *rt, JSHostPromiseRejectionTracker *cb, void *opaque);

#endif
