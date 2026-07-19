/* Independently compiled context access facade. */
#include <turbojs.h>
#include "internal/context_bridge.h"

void *JS_GetContextOpaque(JSContext *ctx)
{
    return turbojs_internal_get_context_opaque(ctx);
}

void JS_SetContextOpaque(JSContext *ctx, void *opaque)
{
    turbojs_internal_set_context_opaque(ctx, opaque);
}

JSContext *JS_DupContext(JSContext *ctx)
{
    return turbojs_internal_dup_context(ctx);
}

JSRuntime *JS_GetRuntime(JSContext *ctx)
{
    return turbojs_internal_get_context_runtime(ctx);
}
