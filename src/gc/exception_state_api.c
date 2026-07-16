/* Independently compiled exception-state facade. */
#include <turbojs.h>
#include "internal/exception_bridge.h"

JSValue JS_Throw(JSContext *ctx, JSValue obj)
{
    return turbojs_internal_throw(ctx, obj);
}

JSValue JS_GetException(JSContext *ctx)
{
    return turbojs_internal_get_exception(ctx);
}

bool JS_HasException(JSContext *ctx)
{
    return turbojs_internal_has_exception(ctx);
}

void JS_ResetUncatchableError(JSContext *ctx)
{
    turbojs_internal_reset_uncatchable_error(ctx);
}
