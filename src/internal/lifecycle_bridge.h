#ifndef QJS_INTERNAL_LIFECYCLE_BRIDGE_H
#define QJS_INTERNAL_LIFECYCLE_BRIDGE_H
#include <turbojs.h>
JSRuntime *turbojs_internal_new_runtime2(const JSMallocFunctions *mf, void *opaque);
JSRuntime *turbojs_internal_new_runtime(void);
void turbojs_internal_free_runtime(JSRuntime *rt);
int turbojs_internal_add_runtime_finalizer(JSRuntime *rt, JSRuntimeFinalizer *finalizer, void *arg);
void turbojs_internal_set_runtime_info(JSRuntime *rt, const char *s);
JSContext *turbojs_internal_new_context_raw(JSRuntime *rt);
JSContext *turbojs_internal_new_context(JSRuntime *rt);
void turbojs_internal_free_context(JSContext *ctx);
void turbojs_internal_update_stack_top(JSRuntime *rt);
#endif
