#ifndef TURBOJS_INTERNAL_EXCEPTION_BRIDGE_H
#define TURBOJS_INTERNAL_EXCEPTION_BRIDGE_H

/* Private opaque bridge for independently compiled exception-state APIs. */
#include <turbojs.h>

JSValue turbojs_internal_throw(JSContext *ctx, JSValue obj);
JSValue turbojs_internal_get_exception(JSContext *ctx);
bool turbojs_internal_has_exception(JSContext *ctx);
void turbojs_internal_reset_uncatchable_error(JSContext *ctx);

#endif
