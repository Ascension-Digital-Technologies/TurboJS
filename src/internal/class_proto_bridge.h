#ifndef TURBOJS_INTERNAL_CLASS_PROTO_BRIDGE_H
#define TURBOJS_INTERNAL_CLASS_PROTO_BRIDGE_H
#include <turbojs.h>
void turbojs_internal_set_class_proto(JSContext *ctx, JSClassID class_id, JSValue obj);
JSValue turbojs_internal_get_class_proto(JSContext *ctx, JSClassID class_id);
JSValue turbojs_internal_get_function_proto(JSContext *ctx);
#endif
