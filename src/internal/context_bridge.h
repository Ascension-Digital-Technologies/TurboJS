#ifndef QJS_INTERNAL_CONTEXT_BRIDGE_H
#define QJS_INTERNAL_CONTEXT_BRIDGE_H

/* Private opaque bridge for independently compiled context access APIs. */
#include <turbojs.h>

void *turbojs_internal_get_context_opaque(JSContext *ctx);
void turbojs_internal_set_context_opaque(JSContext *ctx, void *opaque);
JSContext *turbojs_internal_dup_context(JSContext *ctx);
JSRuntime *turbojs_internal_get_context_runtime(JSContext *ctx);

#endif
