/* Independently compiled class/prototype facade. */
#include <turbojs.h>
#include "internal/class_proto_bridge.h"
void JS_SetClassProto(JSContext *ctx, JSClassID class_id, JSValue obj) { turbojs_internal_set_class_proto(ctx, class_id, obj); }
JSValue JS_GetClassProto(JSContext *ctx, JSClassID class_id) { return turbojs_internal_get_class_proto(ctx, class_id); }
JSValue JS_GetFunctionProto(JSContext *ctx) { return turbojs_internal_get_function_proto(ctx); }
