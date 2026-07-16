/* Engine domain source: builtins/core_builtins.inc -> array_builtin.
 * Ownership: builtins subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static JSValue js_array_iterator_next(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv,
                                      int *pdone, int magic)
{
    JSArrayIteratorData *it;
    uint32_t len, idx;
    JSValue val, obj;
    JSObject *p;

    it = JS_GetOpaque2(ctx, this_val, JS_CLASS_ARRAY_ITERATOR);
    if (!it)
        goto fail1;
    if (JS_IsUndefined(it->obj))
        goto done;
    p = JS_VALUE_GET_OBJ(it->obj);
    if (is_typed_array(p->class_id)) {
        if (typed_array_is_oob(p)) {
            JS_ThrowTypeErrorArrayBufferOOB(ctx);
            goto fail1;
        }
        len = p->u.array.count;
    } else {
        if (js_get_length32(ctx, &len, it->obj)) {
        fail1:
            *pdone = false;
            return JS_EXCEPTION;
        }
    }
    idx = it->idx;
    if (idx >= len) {
        JS_FreeValue(ctx, it->obj);
        it->obj = JS_UNDEFINED;
    done:
        *pdone = true;
        return JS_UNDEFINED;
    }
    it->idx = idx + 1;
    *pdone = false;
    if (it->kind == JS_ITERATOR_KIND_KEY) {
        return js_uint32(idx);
    } else {
        val = JS_GetPropertyUint32(ctx, it->obj, idx);
        if (JS_IsException(val))
            return JS_EXCEPTION;
        if (it->kind == JS_ITERATOR_KIND_VALUE) {
            return val;
        } else {
            JSValueConst args[2];
            JSValue num;
            num = js_uint32(idx);
            args[0] = num;
            args[1] = val;
            obj = js_create_array(ctx, 2, args);
            JS_FreeValue(ctx, val);
            JS_FreeValue(ctx, num);
            return obj;
        }
    }
}

typedef struct JSIteratorWrapData {
    JSValue wrapped_iter;
    JSValue wrapped_next;
} JSIteratorWrapData;

static void js_iterator_wrap_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);
    JSIteratorWrapData *it = p->u.iterator_wrap_data;
    if (it) {
        JS_FreeValueRT(rt, it->wrapped_iter);
        JS_FreeValueRT(rt, it->wrapped_next);
        js_free_rt(rt, it);
    }
}

static void js_iterator_wrap_mark(JSRuntime *rt, JSValueConst val,
                                  JS_MarkFunc *mark_func)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);
    JSIteratorWrapData *it = p->u.iterator_wrap_data;
    if (it) {
        JS_MarkValue(rt, it->wrapped_iter, mark_func);
        JS_MarkValue(rt, it->wrapped_next, mark_func);
    }
}

static JSValue js_iterator_wrap_next(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv,
                                     int *pdone, int magic)
{
    JSIteratorWrapData *it;
    JSValue method, ret;
    it = JS_GetOpaque2(ctx, this_val, JS_CLASS_ITERATOR_WRAP);
    if (!it)
        return JS_EXCEPTION;
    if (magic == GEN_MAGIC_NEXT)
        return JS_IteratorNext(ctx, it->wrapped_iter, it->wrapped_next, argc, argv, pdone);
    method = JS_GetProperty(ctx, it->wrapped_iter, JS_ATOM_return);
    if (JS_IsException(method))
        return JS_EXCEPTION;
    if (JS_IsNull(method) || JS_IsUndefined(method)) {
        *pdone = true;
        return JS_UNDEFINED;
    }
    ret = JS_IteratorNext2(ctx, it->wrapped_iter, method, argc, argv, pdone);
    JS_FreeValue(ctx, method);
    return ret;
}

static const JSCFunctionListEntry js_iterator_wrap_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_iterator_wrap_next, GEN_MAGIC_NEXT ),
    JS_ITERATOR_NEXT_DEF("return", 0, js_iterator_wrap_next, GEN_MAGIC_RETURN ),
};

static int check_iterator(JSContext *ctx, JSValueConst obj);

static JSValue js_iterator_constructor_getset(JSContext *ctx,
                                              JSValueConst this_val,
                                              int argc, JSValueConst *argv,
                                              int magic,
                                              JSValueConst *func_data)
{
    int ret;

    if (magic) { // setter (the getter is registered with magic == 0)
        // SetterThatIgnoresPrototypeProperties(%Iterator.prototype%, "constructor", v)
        // argv[0] is the assigned value; it defaults to undefined because the
        // setter is registered with length 1 (see JS_AddIntrinsicBaseObjects).
        if (check_iterator(ctx, this_val) < 0)
            return JS_EXCEPTION;
        if (js_same_value(ctx, this_val, ctx->class_proto[JS_CLASS_ITERATOR]))
            return JS_ThrowTypeError(ctx, "Cannot assign to read only property");
        ret = JS_GetOwnProperty(ctx, NULL, this_val, JS_ATOM_constructor);
        if (ret < 0)
            return JS_EXCEPTION;
        if (ret) {
            if (JS_SetProperty(ctx, this_val, JS_ATOM_constructor, js_dup(argv[0])) < 0)
                return JS_EXCEPTION;
        } else {
            if (JS_DefinePropertyValue(ctx, this_val, JS_ATOM_constructor,
                                       js_dup(argv[0]),
                                       JS_PROP_C_W_E | JS_PROP_THROW) < 0)
                return JS_EXCEPTION;
        }
        return JS_UNDEFINED;
    }
    return js_dup(func_data[0]);
}

static JSValue js_iterator_constructor(JSContext *ctx, JSValueConst new_target,
                                       int argc, JSValueConst *argv)
{
    JSObject *p;

    if (JS_TAG_OBJECT != JS_VALUE_GET_TAG(new_target))
        return JS_ThrowTypeError(ctx, "constructor requires 'new'");
    p = JS_VALUE_GET_OBJ(new_target);
    if (p->class_id == JS_CLASS_C_FUNCTION)
        if (p->u.cfunc.c_function.generic == js_iterator_constructor)
            return JS_ThrowTypeError(ctx, "abstract class not constructable");
    return js_create_from_ctor(ctx, new_target, JS_CLASS_ITERATOR);
}

typedef struct JSIteratorConcatData {
    uint32_t index : 31; // elements (not pairs!) in values[] array
    uint32_t unused : 1;
    uint32_t count : 31;
    uint32_t running : 1;
    JSValue iter, next, values[]; // array of (object, method) pairs
} JSIteratorConcatData;

static void js_iterator_concat_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);
    JSIteratorConcatData *it = p->u.iterator_concat_data;
    if (it) {
        JS_FreeValueRT(rt, it->iter);
        JS_FreeValueRT(rt, it->next);
        for (uint32_t i = it->index; i < it->count; i++)
            JS_FreeValueRT(rt, it->values[i]);
        js_free_rt(rt, it);
    }
}

static void js_iterator_concat_mark(JSRuntime *rt, JSValueConst val,
                                    JS_MarkFunc *mark_func)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);
    JSIteratorConcatData *it = p->u.iterator_concat_data;
    if (it) {
        JS_MarkValue(rt, it->iter, mark_func);
        JS_MarkValue(rt, it->next, mark_func);
        for (uint32_t i = it->index; i < it->count; i++)
            JS_MarkValue(rt, it->values[i], mark_func);
    }
}

static JSValue js_iterator_concat_next(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv,
                                       int *pdone, int magic)
{
    JSValue iter, item, next, val, *obj, *meth;
    JSIteratorConcatData *it;
    int done;

    it = JS_GetOpaque2(ctx, this_val, JS_CLASS_ITERATOR_CONCAT);
    if (!it)
        return JS_EXCEPTION;
    if (it->running)
        return JS_ThrowTypeError(ctx, "already running");
    it->running = true;
next:
    if (it->index >= it->count) {
        *pdone = true;
        val = JS_UNDEFINED;
        goto done;
    }
    obj = &it->values[it->index + 0];
    meth = &it->values[it->index + 1];
    iter = it->iter;
    if (JS_IsUndefined(iter)) {
        iter = JS_GetIterator2(ctx, *obj, *meth);
        if (JS_IsException(iter))
            goto fail;
        it->iter = iter;
    }
    next = it->next;
    if (JS_IsUndefined(next)) {
        next = JS_GetProperty(ctx, iter, JS_ATOM_next);
        if (JS_IsException(next))
            goto fail;
        it->next = next;
    }
    item = JS_IteratorNext2(ctx, iter, next, 0, NULL, &done);
    if (JS_IsException(item))
        goto fail;
    if (!done) {
        *pdone = false;
        val = item;
        goto done;
    }
    // done==1 means really done, done==2 means "unknown, inspect object"
    if (done == 2) {
        val = JS_GetProperty(ctx, item, JS_ATOM_done);
        if (JS_IsException(val)) {
            JS_FreeValue(ctx, item);
            goto fail;
        }
        done = JS_ToBoolFree(ctx, val);
    }
    if (done) {
        JS_FreeValue(ctx, item);
        JS_FreeValue(ctx, iter);
        JS_FreeValue(ctx, next);
        it->iter = JS_UNDEFINED;
        it->next = JS_UNDEFINED;
        JS_FreeValue(ctx, *meth);
        JS_FreeValue(ctx, *obj);
        it->index += 2;
        goto next;
    }
    val = JS_GetProperty(ctx, item, JS_ATOM_value);
    JS_FreeValue(ctx, item);
    *pdone = false;
done:
    it->running = false;
    return val;
fail:
    val = JS_EXCEPTION;
    goto done;
}

static JSValue js_iterator_concat_return(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    JSIteratorConcatData *it;
    JSValue ret, *pval;

    it = JS_GetOpaque2(ctx, this_val, JS_CLASS_ITERATOR_CONCAT);
    if (!it)
        return JS_EXCEPTION;
    if (it->running)
        return JS_ThrowTypeError(ctx, "already running");
    ret = JS_UNDEFINED;
    if (!JS_IsUndefined(it->iter)) {
        ret = JS_GetProperty(ctx, it->iter, JS_ATOM_return);
        if (JS_IsException(ret))
            return JS_EXCEPTION;
        it->running = true;
        ret = JS_CallFree(ctx, ret, it->iter, 0, NULL);
        it->running = false;
    }
    while (it->index < it->count) {
        pval = &it->values[it->index++];
        JS_FreeValue(ctx, *pval);
        *pval = JS_UNDEFINED;
    }
    JS_FreeValue(ctx, it->iter);
    JS_FreeValue(ctx, it->next);
    it->iter = JS_UNDEFINED;
    it->next = JS_UNDEFINED;
    return ret;
}

static const JSCFunctionListEntry js_iterator_concat_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 1, js_iterator_concat_next, 0 ),
    JS_CFUNC_DEF("return", 1, js_iterator_concat_return ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Iterator Concat", JS_PROP_CONFIGURABLE ),
};

static JSValue js_iterator_concat(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    JSIteratorConcatData *it;
    JSValue obj, method;

    it = js_malloc(ctx, sizeof(*it) + 2*argc * sizeof(it->values[0]));
    if (!it)
        return JS_EXCEPTION;
    it->running = false;
    it->index = 0;
    it->count = 0;
    it->iter = JS_UNDEFINED;
    it->next = JS_UNDEFINED;
    for (int i = 0; i < argc; i++) {
        JSValueConst obj = argv[i];
        if (!JS_IsObject(obj)) {
            JS_ThrowTypeErrorNotAnObject(ctx);
            goto fail;
        }
        method = JS_GetProperty(ctx, obj, JS_ATOM_Symbol_iterator);
        if (JS_IsException(method))
            goto fail;
        if (!JS_IsFunction(ctx, method)) {
            JS_ThrowTypeErrorNotAFunction(ctx);
            JS_FreeValue(ctx, method);
            goto fail;
        }
        it->values[it->count++] = js_dup(obj);
        it->values[it->count++] = method;
    }
    obj = JS_NewObjectClass(ctx, JS_CLASS_ITERATOR_CONCAT);
    if (JS_IsException(obj))
        goto fail;
    JS_SetOpaqueInternal(obj, it);
    return obj;
fail:
    for (uint32_t i = 0; i < it->count; i++)
        JS_FreeValue(ctx, it->values[i]);
    js_free(ctx, it);
    return JS_EXCEPTION;
}

static JSValue js_iterator_from(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    JSValue method, iter;
    JSIteratorWrapData *it;
    int ret;

    JSValueConst obj = argv[0];
    if (JS_IsString(obj)) {
        method = JS_GetProperty(ctx, obj, JS_ATOM_Symbol_iterator);
        if (JS_IsException(method))
            return JS_EXCEPTION;
        return JS_CallFree(ctx, method, obj, 0, NULL);
    }
    if (!JS_IsObject(obj))
        return JS_ThrowTypeError(ctx, "Iterator.from called on non-object");
    ret = JS_OrdinaryIsInstanceOf(ctx, obj, ctx->iterator_ctor);
    if (ret < 0)
        return JS_EXCEPTION;
    if (ret)
        return js_dup(obj);
    method = JS_GetProperty(ctx, obj, JS_ATOM_Symbol_iterator);
    if (JS_IsException(method))
        return JS_EXCEPTION;
    if (JS_IsNull(method) || JS_IsUndefined(method)) {
        method = JS_GetProperty(ctx, obj, JS_ATOM_next);
        if (JS_IsException(method))
            return JS_EXCEPTION;
        iter = JS_NewObjectClass(ctx, JS_CLASS_ITERATOR_WRAP);
        if (JS_IsException(iter))
            goto fail;
        it = js_malloc(ctx, sizeof(*it));
        if (!it)
            goto fail;
        it->wrapped_iter = js_dup(obj);
        it->wrapped_next = method;
        JS_SetOpaqueInternal(iter, it);
    } else {
        iter = JS_GetIterator2(ctx, obj, method);
        JS_FreeValue(ctx, method);
        if (JS_IsException(iter))
            return JS_EXCEPTION;
    }
    return iter;
fail:
    JS_FreeValue(ctx, method);
    JS_FreeValue(ctx, iter);
    return JS_EXCEPTION;
}

static int check_iterator(JSContext *ctx, JSValueConst obj)
{
    if (!JS_IsObject(obj)) {
        JS_ThrowTypeErrorNotAnObject(ctx);
        return -1;
    }
    return 0;
}

typedef struct JSIteratorHelperData {
    JSValue obj;
    JSValue next;
    JSValue func; // predicate (filter) or mapper (flatMap, map)
    JSValue inner; // innerValue (flatMap)
    int64_t count; // limit (drop, take) or counter (filter, map, flatMap)
    JSIteratorHelperKindEnum kind : 8;
    uint8_t executing : 1;
    uint8_t done : 1;
} JSIteratorHelperData;

static JSValue js_create_iterator_helper(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv, int magic)
{
    JSValueConst func;
    JSValue obj, method;
    int64_t count;
    JSIteratorHelperData *it;

    if (check_iterator(ctx, this_val) < 0)
        return JS_EXCEPTION;
    func = JS_UNDEFINED;
    count = 0;

    switch(magic) {
    case JS_ITERATOR_HELPER_KIND_DROP:
    case JS_ITERATOR_HELPER_KIND_TAKE:
        {
            JSValue v;
            double dlimit;
            v = JS_ToNumber(ctx, argv[0]);
            if (JS_IsException(v))
                goto fail;
            // Check for Infinity.
            if (JS_ToFloat64(ctx, &dlimit, v)) {
                JS_FreeValue(ctx, v);
                goto fail;
            }
            if (isnan(dlimit)) {
                JS_FreeValue(ctx, v);
                goto range_error;
            }
            if (!isfinite(dlimit)) {
                JS_FreeValue(ctx, v);
                if (dlimit < 0)
                    goto range_error;
                else
                    count = MAX_SAFE_INTEGER;
            } else {
                v = JS_ToIntegerFree(ctx, v);
                if (JS_IsException(v))
                    goto fail;
                if (JS_ToInt64Free(ctx, &count, v))
                    goto fail;
            }
            if (count < 0)
                goto range_error;
        }
        break;
    case JS_ITERATOR_HELPER_KIND_FILTER:
    case JS_ITERATOR_HELPER_KIND_FLAT_MAP:
    case JS_ITERATOR_HELPER_KIND_MAP:
        {
            func = argv[0];
            if (check_function(ctx, func))
                goto fail;
        }
        break;
    default:
        abort();
        break;
    }

    method = JS_GetProperty(ctx, this_val, JS_ATOM_next);
    if (JS_IsException(method))
        goto fail;
    obj = JS_NewObjectClass(ctx, JS_CLASS_ITERATOR_HELPER);
    if (JS_IsException(obj)) {
        JS_FreeValue(ctx, method);
        goto fail;
    }
    it = js_malloc(ctx, sizeof(*it));
    if (!it) {
        JS_FreeValue(ctx, obj);
        JS_FreeValue(ctx, method);
        goto fail;
    }
    it->kind = magic;
    it->obj = js_dup(this_val);
    it->func = js_dup(func);
    it->next = method;
    it->inner = JS_UNDEFINED;
    it->count = count;
    it->executing = 0;
    it->done = 0;
    JS_SetOpaqueInternal(obj, it);
    return obj;
range_error:
    JS_ThrowRangeError(ctx, "must be positive");
fail:
    JS_IteratorClose(ctx, this_val, true);
    return JS_EXCEPTION;
}

static JSValue js_iterator_proto_func(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv, int magic)
{
    JSValue item, method, ret, func, index_val, r;
    JSValueConst args[2];
    int64_t idx;
    int done;

    if (check_iterator(ctx, this_val) < 0)
        return JS_EXCEPTION;
    func = JS_UNDEFINED;
    method = JS_UNDEFINED;
    if (check_function(ctx, argv[0]))
        goto fail;
    func = js_dup(argv[0]);
    method = JS_GetProperty(ctx, this_val, JS_ATOM_next);
    if (JS_IsException(method))
        goto fail;

    r = JS_UNDEFINED;

    switch(magic) {
    case JS_ITERATOR_HELPER_KIND_EVERY:
        {
            r = JS_TRUE;
            for (idx = 0; /*empty*/; idx++) {
                item = JS_IteratorNext(ctx, this_val, method, 0, NULL, &done);
                if (JS_IsException(item))
                    goto fail;
                if (done)
                    break;
                index_val = js_int64(idx);
                args[0] = item;
                args[1] = index_val;
                ret = JS_Call(ctx, func, JS_UNDEFINED, countof(args), args);
                JS_FreeValue(ctx, item);
                JS_FreeValue(ctx, index_val);
                if (JS_IsException(ret))
                    goto fail;
                if (!JS_ToBoolFree(ctx, ret)) {
                    if (JS_IteratorClose(ctx, this_val, false) < 0)
                        r = JS_EXCEPTION;
                    else
                        r = JS_FALSE;
                    break;
                }
                index_val = JS_UNDEFINED;
                ret = JS_UNDEFINED;
                item = JS_UNDEFINED;
            }
        }
        break;
    case JS_ITERATOR_HELPER_KIND_FIND:
        {
            for (idx = 0; /*empty*/; idx++) {
                item = JS_IteratorNext(ctx, this_val, method, 0, NULL, &done);
                if (JS_IsException(item))
                    goto fail;
                if (done)
                    break;
                index_val = js_int64(idx);
                args[0] = item;
                args[1] = index_val;
                ret = JS_Call(ctx, func, JS_UNDEFINED, countof(args), args);
                JS_FreeValue(ctx, index_val);
                if (JS_IsException(ret)) {
                    JS_FreeValue(ctx, item);
                    goto fail;
                }
                if (JS_ToBoolFree(ctx, ret)) {
                    if (JS_IteratorClose(ctx, this_val, false) < 0) {
                        JS_FreeValue(ctx, item);
                        r = JS_EXCEPTION;
                    } else {
                        r = item;
                    }
                    break;
                }
                index_val = JS_UNDEFINED;
                ret = JS_UNDEFINED;
                item = JS_UNDEFINED;
            }
        }
        break;
    case JS_ITERATOR_HELPER_KIND_FOR_EACH:
        {
            for (idx = 0; /*empty*/; idx++) {
                item = JS_IteratorNext(ctx, this_val, method, 0, NULL, &done);
                if (JS_IsException(item))
                    goto fail;
                if (done)
                    break;
                index_val = js_int64(idx);
                args[0] = item;
                args[1] = index_val;
                ret = JS_Call(ctx, func, JS_UNDEFINED, countof(args), args);
                JS_FreeValue(ctx, item);
                JS_FreeValue(ctx, index_val);
                if (JS_IsException(ret))
                    goto fail;
                JS_FreeValue(ctx, ret);
                index_val = JS_UNDEFINED;
                ret = JS_UNDEFINED;
                item = JS_UNDEFINED;
            }
        }
        break;
    case JS_ITERATOR_HELPER_KIND_SOME:
        {
            r = JS_FALSE;
            for (idx = 0; /*empty*/; idx++) {
                item = JS_IteratorNext(ctx, this_val, method, 0, NULL, &done);
                if (JS_IsException(item))
                    goto fail;
                if (done)
                    break;
                index_val = js_int64(idx);
                args[0] = item;
                args[1] = index_val;
                ret = JS_Call(ctx, func, JS_UNDEFINED, countof(args), args);
                JS_FreeValue(ctx, item);
                JS_FreeValue(ctx, index_val);
                if (JS_IsException(ret))
                    goto fail;
                if (JS_ToBoolFree(ctx, ret)) {
                    if (JS_IteratorClose(ctx, this_val, false) < 0)
                        r = JS_EXCEPTION;
                    else
                        r = JS_TRUE;
                    break;
                }
                index_val = JS_UNDEFINED;
                ret = JS_UNDEFINED;
                item = JS_UNDEFINED;
            }
        }
        break;
    default:
        abort();
        break;
    }

    JS_FreeValue(ctx, func);
    JS_FreeValue(ctx, method);
    return r;
fail:
    JS_IteratorClose(ctx, this_val, true);
    JS_FreeValue(ctx, func);
    JS_FreeValue(ctx, method);
    return JS_EXCEPTION;
}

static JSValue js_iterator_proto_reduce(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv)
{
    JSValue item, method, ret, func, index_val, acc;
    JSValueConst args[3];
    int64_t idx;
    int done;

    if (check_iterator(ctx, this_val) < 0)
        return JS_EXCEPTION;
    acc = JS_UNDEFINED;
    func = JS_UNDEFINED;
    method = JS_UNDEFINED;
    if (check_function(ctx, argv[0]))
        goto exception;
    func = js_dup(argv[0]);
    method = JS_GetProperty(ctx, this_val, JS_ATOM_next);
    if (JS_IsException(method))
        goto exception;
    if (argc > 1) {
        acc = js_dup(argv[1]);
        idx = 0;
    } else {
        acc = JS_IteratorNext(ctx, this_val, method, 0, NULL, &done);
        if (JS_IsException(acc))
            goto exception;
        if (done) {
            JS_ThrowTypeError(ctx, "empty iterator");
            goto exception;
        }
        idx = 1;
    }
    for (/* empty */; /*empty*/; idx++) {
        item = JS_IteratorNext(ctx, this_val, method, 0, NULL, &done);
        if (JS_IsException(item))
            goto exception;
        if (done)
            break;
        index_val = js_int64(idx);
        args[0] = acc;
        args[1] = item;
        args[2] = index_val;
        ret = JS_Call(ctx, func, JS_UNDEFINED, countof(args), args);
        JS_FreeValue(ctx, item);
        JS_FreeValue(ctx, index_val);
        if (JS_IsException(ret))
            goto exception;
        JS_FreeValue(ctx, acc);
        acc = ret;
        index_val = JS_UNDEFINED;
        ret = JS_UNDEFINED;
        item = JS_UNDEFINED;
    }
    JS_FreeValue(ctx, func);
    JS_FreeValue(ctx, method);
    return acc;
exception:
    JS_IteratorClose(ctx, this_val, true);
    JS_FreeValue(ctx, acc);
    JS_FreeValue(ctx, func);
    JS_FreeValue(ctx, method);
    return JS_EXCEPTION;
}

static JSValue js_iterator_proto_toArray(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    JSValue item, method, result;
    int64_t idx;
    int done;

    result = JS_UNDEFINED;
    if (check_iterator(ctx, this_val) < 0)
        return JS_EXCEPTION;
    method = JS_GetProperty(ctx, this_val, JS_ATOM_next);
    if (JS_IsException(method))
        return JS_EXCEPTION;
    result = JS_NewArray(ctx);
    if (JS_IsException(result))
        goto exception;
    for (idx = 0; /*empty*/; idx++) {
        item = JS_IteratorNext(ctx, this_val, method, 0, NULL, &done);
        if (JS_IsException(item))
            goto exception;
        if (done)
            break;
        if (JS_DefinePropertyValueInt64(ctx, result, idx, item,
                                        JS_PROP_C_W_E | JS_PROP_THROW) < 0)
            goto exception;
    }
    if (JS_SetProperty(ctx, result, JS_ATOM_length, js_uint32(idx)) < 0)
        goto exception;
    JS_FreeValue(ctx, method);
    return result;
exception:
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, method);
    return JS_EXCEPTION;
}

static JSValue js_async_dispose_to_undef(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv,
                                         int magic, JSValueConst *func_data);

static JSValue js_iterator_proto_dispose(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    JSValue method;

    method = JS_GetProperty(ctx, this_val, JS_ATOM_return);
    if (JS_IsException(method))
        return JS_EXCEPTION;
    if (JS_IsUndefined(method))
        return JS_UNDEFINED;
    return JS_CallFree(ctx, method, this_val, 0, NULL);
}

static JSValue js_async_iterator_proto_dispose(JSContext *ctx,
                                               JSValueConst this_val,
                                               int argc, JSValueConst *argv)
{
    JSValue method, ret, promise, undef_fn, then_args[1], result;
    JSValue undef = JS_UNDEFINED;

    method = JS_GetProperty(ctx, this_val, JS_ATOM_return);
    if (JS_IsException(method)) {
        JSValue exc = JS_GetException(ctx);
        JSValue p = js_promise_resolve(ctx, ctx->promise_ctor, 1, vc(&exc), 1);
        JS_FreeValue(ctx, exc);
        return p;
    }
    if (JS_IsUndefined(method)) {
        return js_promise_resolve(ctx, ctx->promise_ctor, 1, vc(&undef), 0);
    }
    ret = JS_Call(ctx, method, this_val, 0, NULL);
    JS_FreeValue(ctx, method);
    if (JS_IsException(ret)) {
        JSValue exc = JS_GetException(ctx);
        JSValue p = js_promise_resolve(ctx, ctx->promise_ctor, 1, vc(&exc), 1);
        JS_FreeValue(ctx, exc);
        return p;
    }
    /* Wrap in Promise.resolve(ret).then(() => undefined) */
    promise = js_promise_resolve(ctx, ctx->promise_ctor, 1, vc(&ret), 0);
    JS_FreeValue(ctx, ret);
    if (JS_IsException(promise))
        return promise;
    undef_fn = JS_NewCFunctionData(ctx, js_async_dispose_to_undef, 0, 0, 0,
                                   NULL);
    if (JS_IsException(undef_fn)) {
        JS_FreeValue(ctx, promise);
        return JS_EXCEPTION;
    }
    then_args[0] = undef_fn;
    result = JS_Invoke(ctx, promise, JS_ATOM_then, 1, vc(then_args));
    JS_FreeValue(ctx, undef_fn);
    JS_FreeValue(ctx, promise);
    return result;
}

static JSValue js_iterator_proto_iterator(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv)
{
    return js_dup(this_val);
}

static JSValue js_iterator_proto_get_toStringTag(JSContext *ctx, JSValueConst this_val)
{
    return JS_AtomToString(ctx, JS_ATOM_Iterator);
}

static JSValue js_iterator_proto_set_toStringTag(JSContext *ctx, JSValueConst this_val, JSValueConst val)
{
    int res;

    if (check_iterator(ctx, this_val) < 0)
        return JS_EXCEPTION;
    if (js_same_value(ctx, this_val, ctx->class_proto[JS_CLASS_ITERATOR]))
        return JS_ThrowTypeError(ctx, "Cannot assign to read only property");
    res = JS_GetOwnProperty(ctx, NULL, this_val, JS_ATOM_Symbol_toStringTag);
    if (res < 0)
        return JS_EXCEPTION;
    if (res) {
        if (JS_SetProperty(ctx, this_val, JS_ATOM_Symbol_toStringTag, js_dup(val)) < 0)
            return JS_EXCEPTION;
    } else {
        if (JS_DefinePropertyValueConst(ctx, this_val, JS_ATOM_Symbol_toStringTag, val, JS_PROP_C_W_E | JS_PROP_THROW) < 0)
            return JS_EXCEPTION;
    }
    return JS_UNDEFINED;
}

static void js_iterator_helper_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);
    JSIteratorHelperData *it = p->u.iterator_helper_data;
    if (it) {
        JS_FreeValueRT(rt, it->obj);
        JS_FreeValueRT(rt, it->func);
        JS_FreeValueRT(rt, it->next);
        JS_FreeValueRT(rt, it->inner);
        js_free_rt(rt, it);
    }
}

static void js_iterator_helper_mark(JSRuntime *rt, JSValueConst val,
                                   JS_MarkFunc *mark_func)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);
    JSIteratorHelperData *it = p->u.iterator_helper_data;
    if (it) {
        JS_MarkValue(rt, it->obj, mark_func);
        JS_MarkValue(rt, it->func, mark_func);
        JS_MarkValue(rt, it->next, mark_func);
        JS_MarkValue(rt, it->inner, mark_func);
    }
}

static JSValue js_iterator_helper_next(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv,
                                      int *pdone, int magic)
{
    JSIteratorHelperData *it;
    JSValue ret;

    *pdone = false;

    it = JS_GetOpaque2(ctx, this_val, JS_CLASS_ITERATOR_HELPER);
    if (!it)
        return JS_EXCEPTION;
    if (it->executing)
        return JS_ThrowTypeError(ctx, "cannot invoke a running iterator");
    if (it->done) {
        *pdone = true;
        return JS_UNDEFINED;
    }

    it->executing = 1;

    switch (it->kind) {
    case JS_ITERATOR_HELPER_KIND_DROP:
        {
            JSValue item, method;
            if (magic == GEN_MAGIC_NEXT) {
                method = js_dup(it->next);
            } else {
                method = JS_GetProperty(ctx, it->obj, JS_ATOM_return);
                if (JS_IsException(method))
                    goto fail;
            }
            while (it->count > 0) {
                it->count--;
                item = JS_IteratorNext(ctx, it->obj, method, 0, NULL, pdone);
                if (JS_IsException(item)) {
                    JS_FreeValue(ctx, method);
                    goto fail;
                }
                JS_FreeValue(ctx, item);
                if (magic == GEN_MAGIC_RETURN)
                    *pdone = true;
                if (*pdone) {
                    JS_FreeValue(ctx, method);
                    ret = JS_UNDEFINED;
                    goto done;
                }
            }

            item = JS_IteratorNext(ctx, it->obj, method, 0, NULL, pdone);
            JS_FreeValue(ctx, method);
            if (JS_IsException(item))
                goto fail;
            ret = item;
            goto done;
        }
        break;
    case JS_ITERATOR_HELPER_KIND_FILTER:
        {
            JSValue item, method, selected, index_val;
            JSValueConst args[2];
            if (magic == GEN_MAGIC_NEXT) {
                method = js_dup(it->next);
            } else {
                method = JS_GetProperty(ctx, it->obj, JS_ATOM_return);
                if (JS_IsException(method))
                    goto fail;
            }
        filter_again:
            item = JS_IteratorNext(ctx, it->obj, method, 0, NULL, pdone);
            if (JS_IsException(item)) {
                JS_FreeValue(ctx, method);
                goto fail;
            }
            if (*pdone || magic == GEN_MAGIC_RETURN) {
                JS_FreeValue(ctx, method);
                ret = item;
                goto done;
            }
            index_val = js_int64(it->count++);
            args[0] = item;
            args[1] = index_val;
            selected = JS_Call(ctx, it->func, JS_UNDEFINED, countof(args), args);
            JS_FreeValue(ctx, index_val);
            if (JS_IsException(selected)) {
                JS_FreeValue(ctx, method);
                goto fail;
            }
            if (JS_ToBoolFree(ctx, selected)) {
                JS_FreeValue(ctx, method);
                ret = item;
                goto done;
            }
            goto filter_again;
        }
        break;
    case JS_ITERATOR_HELPER_KIND_FLAT_MAP:
        {
            JSValue item, method, index_val, iter;
            JSValueConst args[2];
        flat_map_again:
            if (JS_IsUndefined(it->inner)) {
                if (magic == GEN_MAGIC_NEXT) {
                    method = js_dup(it->next);
                } else {
                    method = JS_GetProperty(ctx, it->obj, JS_ATOM_return);
                    if (JS_IsException(method))
                        goto fail;
                }
                item = JS_IteratorNext(ctx, it->obj, method, 0, NULL, pdone);
                JS_FreeValue(ctx, method);
                if (JS_IsException(item))
                    goto fail;
                if (*pdone || magic == GEN_MAGIC_RETURN) {
                    ret = item;
                    goto done;
                }
                index_val = js_int64(it->count++);
                args[0] = item;
                args[1] = index_val;
                ret = JS_Call(ctx, it->func, JS_UNDEFINED, countof(args), args);
                JS_FreeValue(ctx, item);
                JS_FreeValue(ctx, index_val);
                if (JS_IsException(ret))
                    goto fail;
                if (!JS_IsObject(ret)) {
                    JS_FreeValue(ctx, ret);
                    JS_ThrowTypeError(ctx, "not an object");
                    goto fail;
                }
                method = JS_GetProperty(ctx, ret, JS_ATOM_Symbol_iterator);
                if (JS_IsException(method)) {
                    JS_FreeValue(ctx, ret);
                    goto fail;
                }
                if (JS_IsNull(method) || JS_IsUndefined(method)) {
                    JS_FreeValue(ctx, method);
                    iter = ret;
                } else {
                    iter = JS_GetIterator2(ctx, ret, method);
                    JS_FreeValue(ctx, method);
                    JS_FreeValue(ctx, ret);
                    if (JS_IsException(iter))
                        goto fail;
                }

                it->inner = iter;
            }

            if (magic == GEN_MAGIC_NEXT)
                method = JS_GetProperty(ctx, it->inner, JS_ATOM_next);
            else
                method = JS_GetProperty(ctx, it->inner, JS_ATOM_return);
            if (JS_IsException(method)) {
            inner_fail:
                JS_IteratorClose(ctx, it->inner, false);
                JS_FreeValue(ctx, it->inner);
                it->inner = JS_UNDEFINED;
                goto fail;
            }
            if (magic == GEN_MAGIC_RETURN && (JS_IsUndefined(method) || JS_IsNull(method))) {
                goto inner_end;
            } else {
                item = JS_IteratorNext(ctx, it->inner, method, 0, NULL, pdone);
                JS_FreeValue(ctx, method);
                if (JS_IsException(item))
                    goto inner_fail;
            }
            if (*pdone) {
            inner_end:
                *pdone = false; // The outer iterator must continue.
                JS_IteratorClose(ctx, it->inner, false);
                JS_FreeValue(ctx, it->inner);
                it->inner = JS_UNDEFINED;
                goto flat_map_again;
            }
            ret = item;
            goto done;
        }
        break;
    case JS_ITERATOR_HELPER_KIND_MAP:
        {
            JSValue item, method, index_val;
            JSValueConst args[2];
            if (magic == GEN_MAGIC_NEXT) {
                method = js_dup(it->next);
            } else {
                method = JS_GetProperty(ctx, it->obj, JS_ATOM_return);
                if (JS_IsException(method))
                    goto fail;
            }
            item = JS_IteratorNext(ctx, it->obj, method, 0, NULL, pdone);
            JS_FreeValue(ctx, method);
            if (JS_IsException(item))
                goto fail;
            if (*pdone || magic == GEN_MAGIC_RETURN) {
                ret = item;
                goto done;
            }
            index_val = js_int64(it->count++);
            args[0] = item;
            args[1] = index_val;
            ret = JS_Call(ctx, it->func, JS_UNDEFINED, countof(args), args);
            JS_FreeValue(ctx, index_val);
            JS_FreeValue(ctx, item);
            if (JS_IsException(ret))
                goto fail;
            goto done;
        }
        break;
    case JS_ITERATOR_HELPER_KIND_TAKE:
        {
            JSValue item, method;
            if (it->count > 0) {
                if (magic == GEN_MAGIC_NEXT) {
                    method = js_dup(it->next);
                } else {
                    method = JS_GetProperty(ctx, it->obj, JS_ATOM_return);
                    if (JS_IsException(method))
                        goto fail;
                }
                it->count--;
                item = JS_IteratorNext(ctx, it->obj, method, 0, NULL, pdone);
                JS_FreeValue(ctx, method);
                if (JS_IsException(item))
                    goto fail;
                ret = item;
                goto done;
            }

            *pdone = true;
            if (JS_IteratorClose(ctx, it->obj, false))
                ret = JS_EXCEPTION;
            else
                ret = JS_UNDEFINED;
            goto done;
        }
        break;
    default:
        abort();
    }

done:
    it->done = magic == GEN_MAGIC_NEXT ? *pdone : 1;
    it->executing = 0;
    return ret;
fail:
    /* close the iterator object, preserving pending exception */
    JS_IteratorClose(ctx, it->obj, true);
    ret = JS_EXCEPTION;
    goto done;
}

static const JSCFunctionListEntry js_iterator_funcs[] = {
    JS_CFUNC_DEF("concat", 0, js_iterator_concat ),
    JS_CFUNC_DEF("from", 1, js_iterator_from ),
};

static const JSCFunctionListEntry js_iterator_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("drop", 1, js_create_iterator_helper, JS_ITERATOR_HELPER_KIND_DROP ),
    JS_CFUNC_MAGIC_DEF("filter", 1, js_create_iterator_helper, JS_ITERATOR_HELPER_KIND_FILTER ),
    JS_CFUNC_MAGIC_DEF("flatMap", 1, js_create_iterator_helper, JS_ITERATOR_HELPER_KIND_FLAT_MAP ),
    JS_CFUNC_MAGIC_DEF("map", 1, js_create_iterator_helper, JS_ITERATOR_HELPER_KIND_MAP ),
    JS_CFUNC_MAGIC_DEF("take", 1, js_create_iterator_helper, JS_ITERATOR_HELPER_KIND_TAKE ),
    JS_CFUNC_MAGIC_DEF("every", 1, js_iterator_proto_func, JS_ITERATOR_HELPER_KIND_EVERY ),
    JS_CFUNC_MAGIC_DEF("find", 1, js_iterator_proto_func, JS_ITERATOR_HELPER_KIND_FIND),
    JS_CFUNC_MAGIC_DEF("forEach", 1, js_iterator_proto_func, JS_ITERATOR_HELPER_KIND_FOR_EACH ),
    JS_CFUNC_MAGIC_DEF("some", 1, js_iterator_proto_func, JS_ITERATOR_HELPER_KIND_SOME ),
    JS_CFUNC_DEF("reduce", 1, js_iterator_proto_reduce ),
    JS_CFUNC_DEF("toArray", 0, js_iterator_proto_toArray ),
    JS_CFUNC_DEF("[Symbol.dispose]", 0, js_iterator_proto_dispose ),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_iterator_proto_iterator ),
    JS_CGETSET_DEF("[Symbol.toStringTag]", js_iterator_proto_get_toStringTag, js_iterator_proto_set_toStringTag),
};

static const JSCFunctionListEntry js_iterator_helper_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_iterator_helper_next, GEN_MAGIC_NEXT ),
    JS_ITERATOR_NEXT_DEF("return", 0, js_iterator_helper_next, GEN_MAGIC_RETURN ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Iterator Helper", JS_PROP_CONFIGURABLE ),
};

static const JSCFunctionListEntry js_array_unscopables_funcs[] = {
    JS_PROP_BOOL_DEF("at", 1, JS_PROP_C_W_E),
    JS_PROP_BOOL_DEF("copyWithin", 1, JS_PROP_C_W_E),
    JS_PROP_BOOL_DEF("entries", 1, JS_PROP_C_W_E),
    JS_PROP_BOOL_DEF("fill", 1, JS_PROP_C_W_E),
    JS_PROP_BOOL_DEF("find", 1, JS_PROP_C_W_E),
    JS_PROP_BOOL_DEF("findIndex", 1, JS_PROP_C_W_E),
    JS_PROP_BOOL_DEF("findLast", 1, JS_PROP_C_W_E),
    JS_PROP_BOOL_DEF("findLastIndex", 1, JS_PROP_C_W_E),
    JS_PROP_BOOL_DEF("flat", 1, JS_PROP_C_W_E),
    JS_PROP_BOOL_DEF("flatMap", 1, JS_PROP_C_W_E),
    JS_PROP_BOOL_DEF("includes", 1, JS_PROP_C_W_E),
    JS_PROP_BOOL_DEF("keys", 1, JS_PROP_C_W_E),
    JS_PROP_BOOL_DEF("toReversed", 1, JS_PROP_C_W_E),
    JS_PROP_BOOL_DEF("toSorted", 1, JS_PROP_C_W_E),
    JS_PROP_BOOL_DEF("toSpliced", 1, JS_PROP_C_W_E),
    JS_PROP_BOOL_DEF("values", 1, JS_PROP_C_W_E),
};

static const JSCFunctionListEntry js_array_proto_funcs[] = {
    JS_CFUNC_DEF("at", 1, js_array_at ),
    JS_CFUNC_DEF("with", 2, js_array_with ),
    JS_CFUNC_DEF("concat", 1, js_array_concat ),
    JS_CFUNC_MAGIC_DEF("every", 1, js_array_every, special_every ),
    JS_CFUNC_MAGIC_DEF("some", 1, js_array_every, special_some ),
    JS_CFUNC_MAGIC_DEF("forEach", 1, js_array_every, special_forEach ),
    JS_CFUNC_MAGIC_DEF("map", 1, js_array_every, special_map ),
    JS_CFUNC_MAGIC_DEF("filter", 1, js_array_every, special_filter ),
    JS_CFUNC_MAGIC_DEF("reduce", 1, js_array_reduce, special_reduce ),
    JS_CFUNC_MAGIC_DEF("reduceRight", 1, js_array_reduce, special_reduceRight ),
    JS_CFUNC_DEF("fill", 1, js_array_fill ),
    JS_CFUNC_MAGIC_DEF("find", 1, js_array_find, ArrayFind ),
    JS_CFUNC_MAGIC_DEF("findIndex", 1, js_array_find, ArrayFindIndex ),
    JS_CFUNC_MAGIC_DEF("findLast", 1, js_array_find, ArrayFindLast ),
    JS_CFUNC_MAGIC_DEF("findLastIndex", 1, js_array_find, ArrayFindLastIndex ),
    JS_CFUNC_DEF("indexOf", 1, js_array_indexOf ),
    JS_CFUNC_DEF("lastIndexOf", 1, js_array_lastIndexOf ),
    JS_CFUNC_DEF("includes", 1, js_array_includes ),
    JS_CFUNC_MAGIC_DEF("join", 1, js_array_join, 0 ),
    JS_CFUNC_DEF("toString", 0, js_array_toString ),
    JS_CFUNC_MAGIC_DEF("toLocaleString", 0, js_array_join, 1 ),
    JS_CFUNC_MAGIC_DEF("pop", 0, js_array_pop, 0 ),
    JS_CFUNC_MAGIC_DEF("push", 1, js_array_push, 0 ),
    JS_CFUNC_MAGIC_DEF("shift", 0, js_array_pop, 1 ),
    JS_CFUNC_MAGIC_DEF("unshift", 1, js_array_push, 1 ),
    JS_CFUNC_DEF("reverse", 0, js_array_reverse ),
    JS_CFUNC_DEF("toReversed", 0, js_array_toReversed ),
    JS_CFUNC_DEF("sort", 1, js_array_sort ),
    JS_CFUNC_DEF("toSorted", 1, js_array_toSorted ),
    JS_CFUNC_MAGIC_DEF("slice", 2, js_array_slice, 0 ),
    JS_CFUNC_MAGIC_DEF("splice", 2, js_array_slice, 1 ),
    JS_CFUNC_DEF("toSpliced", 2, js_array_toSpliced ),
    JS_CFUNC_DEF("copyWithin", 2, js_array_copyWithin ),
    JS_CFUNC_MAGIC_DEF("flatMap", 1, js_array_flatten, 1 ),
    JS_CFUNC_MAGIC_DEF("flat", 0, js_array_flatten, 0 ),
    JS_CFUNC_MAGIC_DEF("values", 0, js_create_array_iterator, JS_ITERATOR_KIND_VALUE ),
    JS_ALIAS_DEF("[Symbol.iterator]", "values" ),
    JS_CFUNC_MAGIC_DEF("keys", 0, js_create_array_iterator, JS_ITERATOR_KIND_KEY ),
    JS_CFUNC_MAGIC_DEF("entries", 0, js_create_array_iterator, JS_ITERATOR_KIND_KEY_AND_VALUE ),
    JS_OBJECT_DEF("[Symbol.unscopables]", js_array_unscopables_funcs, countof(js_array_unscopables_funcs), JS_PROP_CONFIGURABLE ),
};

static const JSCFunctionListEntry js_array_iterator_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_array_iterator_next, 0 ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Array Iterator", JS_PROP_CONFIGURABLE ),
};

/* Number */

static JSValue js_number_constructor(JSContext *ctx, JSValueConst new_target,
                                     int argc, JSValueConst *argv)
{
    JSValue val, obj;
    if (argc == 0) {
        val = js_int32(0);
    } else {
        val = JS_ToNumeric(ctx, argv[0]);
        if (JS_IsException(val))
            return val;
        switch(JS_VALUE_GET_TAG(val)) {
        case JS_TAG_SHORT_BIG_INT:
            val = js_int64(JS_VALUE_GET_SHORT_BIG_INT(val));
            if (JS_IsException(val))
                return val;
            break;
        case JS_TAG_BIG_INT:
            {
                JSBigInt *p = JS_VALUE_GET_PTR(val);
                double d;
                d = js_bigint_to_float64(ctx, p);
                JS_FreeValue(ctx, val);
                val = js_float64(d);
            }
            break;
        default:
            break;
        }
    }
    if (!JS_IsUndefined(new_target)) {
        obj = js_create_from_ctor(ctx, new_target, JS_CLASS_NUMBER);
        if (!JS_IsException(obj))
            JS_SetObjectData(ctx, obj, val);
        return obj;
    } else {
        return val;
    }
}

static JSValue js_number_isNaN(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    if (!JS_IsNumber(argv[0]))
        return JS_FALSE;
    return js_global_isNaN(ctx, this_val, argc, argv);
}

static JSValue js_number_isFinite(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    if (!JS_IsNumber(argv[0]))
        return JS_FALSE;
    return js_global_isFinite(ctx, this_val, argc, argv);
}

static JSValue js_number_isInteger(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    int ret;
    ret = JS_NumberIsInteger(ctx, argv[0]);
    if (ret < 0)
        return JS_EXCEPTION;
    else
        return js_bool(ret);
}

static JSValue js_number_isSafeInteger(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    double d;
    if (!JS_IsNumber(argv[0]))
        return JS_FALSE;
    if (unlikely(JS_ToFloat64(ctx, &d, argv[0])))
        return JS_EXCEPTION;
    return js_bool(is_safe_integer(d));
}

static const JSCFunctionListEntry js_number_funcs[] = {
    /* global ParseInt and parseFloat should be defined already or delayed */
    JS_ALIAS_BASE_DEF("parseInt", "parseInt", 0 ),
    JS_ALIAS_BASE_DEF("parseFloat", "parseFloat", 0 ),
    JS_CFUNC_DEF("isNaN", 1, js_number_isNaN ),
    JS_CFUNC_DEF("isFinite", 1, js_number_isFinite ),
    JS_CFUNC_DEF("isInteger", 1, js_number_isInteger ),
    JS_CFUNC_DEF("isSafeInteger", 1, js_number_isSafeInteger ),
    JS_PROP_DOUBLE_DEF("MAX_VALUE", 1.7976931348623157e+308, 0 ),
    JS_PROP_DOUBLE_DEF("MIN_VALUE", 5e-324, 0 ),
    JS_PROP_DOUBLE_DEF("EPSILON", 2.220446049250313e-16, 0 ), /* ES6 */
    JS_PROP_DOUBLE_DEF("MAX_SAFE_INTEGER", 9007199254740991.0, 0 ), /* ES6 */
    JS_PROP_DOUBLE_DEF("MIN_SAFE_INTEGER", -9007199254740991.0, 0 ), /* ES6 */
    // workarounds for msvc & djgpp where NAN and INFINITY
    // are not compile-time expressions
    JS_PROP_U2D_DEF("NaN",               0x7FF8ull<<48, 0 ),
    JS_PROP_U2D_DEF("NEGATIVE_INFINITY", 0xFFF0ull<<48, 0 ),
    JS_PROP_U2D_DEF("POSITIVE_INFINITY", 0x7FF0ull<<48, 0 ),
};

