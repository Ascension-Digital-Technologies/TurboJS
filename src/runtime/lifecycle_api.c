/* Independently compiled runtime and context lifecycle facade. */
#include <turbojs.h>
#include "internal/lifecycle_bridge.h"
JSRuntime *JS_NewRuntime2(const JSMallocFunctions *mf, void *opaque) { return turbojs_internal_new_runtime2(mf, opaque); }
JSRuntime *JS_NewRuntime(void) { return turbojs_internal_new_runtime(); }
void JS_FreeRuntime(JSRuntime *rt) { turbojs_internal_free_runtime(rt); }
int JS_AddRuntimeFinalizer(JSRuntime *rt, JSRuntimeFinalizer *finalizer, void *arg) { return turbojs_internal_add_runtime_finalizer(rt, finalizer, arg); }
void JS_SetRuntimeInfo(JSRuntime *rt, const char *s) { turbojs_internal_set_runtime_info(rt, s); }
JSContext *JS_NewContextRaw(JSRuntime *rt) { return turbojs_internal_new_context_raw(rt); }
JSContext *JS_NewContext(JSRuntime *rt) { return turbojs_internal_new_context(rt); }
void JS_FreeContext(JSContext *ctx) { turbojs_internal_free_context(ctx); }
void JS_UpdateStackTop(JSRuntime *rt) { turbojs_internal_update_stack_top(rt); }
