/* Engine domain source: builtins/core_builtins.inc -> object_builtin.
 * Ownership: builtins subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

/* Function class */

static JSValue js_function_proto(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    return JS_UNDEFINED;
}

/* XXX: add a specific eval mode so that Function("}), ({") is rejected */
static JSValue js_function_constructor(JSContext *ctx, JSValueConst new_target,
                                       int argc, JSValueConst *argv, int magic)
{
    JSFunctionKindEnum func_kind = magic;
    int i, n, ret;
    JSValue s, proto, obj = JS_UNDEFINED;
    StringBuffer b_s, *b = &b_s;

    string_buffer_init(ctx, b, 0);
    string_buffer_putc8(b, '(');

    if (func_kind == JS_FUNC_ASYNC || func_kind == JS_FUNC_ASYNC_GENERATOR) {
        string_buffer_puts8(b, "async ");
    }
    string_buffer_puts8(b, "function");

    if (func_kind == JS_FUNC_GENERATOR || func_kind == JS_FUNC_ASYNC_GENERATOR) {
        string_buffer_putc8(b, '*');
    }
    string_buffer_puts8(b, " anonymous(");

    n = argc - 1;
    for(i = 0; i < n; i++) {
        if (i != 0) {
            string_buffer_putc8(b, ',');
        }
        if (string_buffer_concat_value(b, argv[i]))
            goto fail;
    }
    string_buffer_puts8(b, "\n) {\n");
    if (n >= 0) {
        if (string_buffer_concat_value(b, argv[n]))
            goto fail;
    }
    string_buffer_puts8(b, "\n})");
    s = string_buffer_end(b);
    if (JS_IsException(s))
        goto fail1;

    obj = JS_EvalObject(ctx, ctx->global_obj, s, JS_EVAL_TYPE_INDIRECT, -1);
    JS_FreeValue(ctx, s);
    if (JS_IsException(obj))
        goto fail1;
    if (!JS_IsUndefined(new_target)) {
        /* set the prototype */
        proto = JS_GetProperty(ctx, new_target, JS_ATOM_prototype);
        if (JS_IsException(proto))
            goto fail1;
        if (!JS_IsObject(proto)) {
            JSContext *realm;
            JS_FreeValue(ctx, proto);
            realm = JS_GetFunctionRealm(ctx, new_target);
            if (!realm)
                goto fail1;
            proto = js_dup(realm->class_proto[func_kind_to_class_id[func_kind]]);
        }
        ret = JS_SetPrototypeInternal(ctx, obj, proto, true);
        JS_FreeValue(ctx, proto);
        if (ret < 0)
            goto fail1;
    }
    return obj;

 fail:
    string_buffer_free(b);
 fail1:
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static __exception int js_get_length32(JSContext *ctx, uint32_t *pres,
                                       JSValueConst obj)
{
    JSValue len_val;
    len_val = JS_GetProperty(ctx, obj, JS_ATOM_length);
    if (JS_IsException(len_val)) {
        *pres = 0;
        return -1;
    }
    return JS_ToUint32Free(ctx, pres, len_val);
}

static __exception int js_get_length64(JSContext *ctx, int64_t *pres,
                                       JSValueConst obj)
{
    JSValue len_val;
    len_val = JS_GetProperty(ctx, obj, JS_ATOM_length);
    if (JS_IsException(len_val)) {
        *pres = 0;
        return -1;
    }
    return JS_ToLengthFree(ctx, pres, len_val);
}

static __exception int js_set_length64(JSContext *ctx, JSValueConst obj,
                                       int64_t len)
{
    return JS_SetProperty(ctx, obj, JS_ATOM_length, js_int64(len));
}

static void free_arg_list(JSContext *ctx, JSValue *tab, uint32_t len)
{
    uint32_t i;
    for(i = 0; i < len; i++) {
        JS_FreeValue(ctx, tab[i]);
    }
    js_free(ctx, tab);
}

/* XXX: should use ValueArray */
static JSValue *build_arg_list(JSContext *ctx, uint32_t *plen,
                               JSValueConst array_arg)
{
    uint32_t len, i;
    JSValue *tab, ret;
    JSObject *p;

    if (JS_VALUE_GET_TAG(array_arg) != JS_TAG_OBJECT) {
        JS_ThrowTypeError(ctx, "not a object");
        return NULL;
    }
    if (js_get_length32(ctx, &len, array_arg))
        return NULL;
    if (len > JS_MAX_LOCAL_VARS) {
        // XXX: check for stack overflow?
        JS_ThrowRangeError(ctx, "too many arguments in function call (only %d allowed)",
                           JS_MAX_LOCAL_VARS);
        return NULL;
    }
    /* avoid allocating 0 bytes */
    tab = js_mallocz(ctx, sizeof(tab[0]) * max_uint32(1, len));
    if (!tab)
        return NULL;
    p = JS_VALUE_GET_OBJ(array_arg);
    if ((p->class_id == JS_CLASS_ARRAY || p->class_id == JS_CLASS_ARGUMENTS) &&
        p->fast_array &&
        len == p->u.array.count) {
        for(i = 0; i < len; i++) {
            tab[i] = js_dup(p->u.array.u.values[i]);
        }
    } else {
        for(i = 0; i < len; i++) {
            ret = JS_GetPropertyUint32(ctx, array_arg, i);
            if (JS_IsException(ret)) {
                free_arg_list(ctx, tab, i);
                return NULL;
            }
            tab[i] = ret;
        }
    }
    *plen = len;
    return tab;
}

/* magic value: 0 = normal apply, 1 = apply for constructor, 2 =
   Reflect.apply */
static JSValue js_function_apply(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv, int magic)
{
    JSValueConst this_arg, array_arg;
    uint32_t len;
    JSValue *tab, ret;

    if (check_function(ctx, this_val))
        return JS_EXCEPTION;
    this_arg = argv[0];
    array_arg = argv[1];
    if ((JS_VALUE_GET_TAG(array_arg) == JS_TAG_UNDEFINED ||
         JS_VALUE_GET_TAG(array_arg) == JS_TAG_NULL) && magic != 2) {
        return JS_Call(ctx, this_val, this_arg, 0, NULL);
    }
    tab = build_arg_list(ctx, &len, array_arg);
    if (!tab)
        return JS_EXCEPTION;
    if (magic & 1) {
        ret = JS_CallConstructor2(ctx, this_val, this_arg, len, vc(tab));
    } else {
        ret = JS_Call(ctx, this_val, this_arg, len, vc(tab));
    }
    free_arg_list(ctx, tab, len);
    return ret;
}

static JSValue js_function_call(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    if (argc <= 0) {
        return JS_Call(ctx, this_val, JS_UNDEFINED, 0, NULL);
    } else {
        return JS_Call(ctx, this_val, argv[0], argc - 1, argv + 1);
    }
}

static JSValue js_function_bind(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    JSBoundFunction *bf;
    JSValue func_obj, name1, len_val;
    JSObject *p;
    int arg_count, i, ret;

    if (check_function(ctx, this_val))
        return JS_EXCEPTION;

    func_obj = JS_NewObjectProtoClass(ctx, ctx->function_proto,
                                 JS_CLASS_BOUND_FUNCTION);
    if (JS_IsException(func_obj))
        return JS_EXCEPTION;
    p = JS_VALUE_GET_OBJ(func_obj);
    p->is_constructor = JS_IsConstructor(ctx, this_val);
    arg_count = max_int(0, argc - 1);
    bf = js_malloc(ctx, sizeof(*bf) + arg_count * sizeof(JSValue));
    if (!bf)
        goto exception;
    bf->func_obj = js_dup(this_val);
    bf->this_val = js_dup(argv[0]);
    bf->argc = arg_count;
    for(i = 0; i < arg_count; i++) {
        bf->argv[i] = js_dup(argv[i + 1]);
    }
    p->u.bound_function = bf;

    /* XXX: the spec could be simpler by only using GetOwnProperty */
    ret = JS_GetOwnProperty(ctx, NULL, this_val, JS_ATOM_length);
    if (ret < 0)
        goto exception;
    if (!ret) {
        len_val = js_int32(0);
    } else {
        len_val = JS_GetProperty(ctx, this_val, JS_ATOM_length);
        if (JS_IsException(len_val))
            goto exception;
        if (JS_VALUE_GET_TAG(len_val) == JS_TAG_INT) {
            /* most common case */
            int len1 = JS_VALUE_GET_INT(len_val);
            if (len1 <= arg_count)
                len1 = 0;
            else
                len1 -= arg_count;
            len_val = js_int32(len1);
        } else if (JS_VALUE_GET_NORM_TAG(len_val) == JS_TAG_FLOAT64) {
            double d = JS_VALUE_GET_FLOAT64(len_val);
            if (isnan(d)) {
                d = 0.0;
            } else {
                d = trunc(d);
                if (d <= (double)arg_count)
                    d = 0.0;
                else
                    d -= (double)arg_count; /* also converts -0 to +0 */
            }
            len_val = js_number(d);
        } else {
            JS_FreeValue(ctx, len_val);
            len_val = js_int32(0);
        }
    }
    JS_DefinePropertyValue(ctx, func_obj, JS_ATOM_length,
                           len_val, JS_PROP_CONFIGURABLE);

    name1 = JS_GetProperty(ctx, this_val, JS_ATOM_name);
    if (JS_IsException(name1))
        goto exception;
    if (!JS_IsString(name1)) {
        JS_FreeValue(ctx, name1);
        name1 = js_empty_string(ctx->rt);
    }
    name1 = JS_ConcatString3(ctx, "bound ", name1, "");
    if (JS_IsException(name1))
        goto exception;
    JS_DefinePropertyValue(ctx, func_obj, JS_ATOM_name, name1,
                           JS_PROP_CONFIGURABLE);
    return func_obj;
 exception:
    JS_FreeValue(ctx, func_obj);
    return JS_EXCEPTION;
}

static JSValue js_function_toString(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    JSObject *p;
    JSFunctionKindEnum func_kind = JS_FUNC_NORMAL;

    if (check_function(ctx, this_val))
        return JS_EXCEPTION;

    p = JS_VALUE_GET_OBJ(this_val);
    if (js_class_has_bytecode(p->class_id)) {
        JSFunctionBytecode *b = p->u.func.function_bytecode;
        /* `b->source` must be pure ASCII or UTF-8 encoded */
        if (b->source)
            return JS_NewStringLen(ctx, b->source, b->source_len);
    }
    {
        JSValue name;
        const char *pref, *suff;

        switch(func_kind) {
        default:
        case JS_FUNC_NORMAL:
            pref = "function ";
            break;
        case JS_FUNC_GENERATOR:
            pref = "function *";
            break;
        case JS_FUNC_ASYNC:
            pref = "async function ";
            break;
        case JS_FUNC_ASYNC_GENERATOR:
            pref = "async function *";
            break;
        }
        suff = "() {\n    [native code]\n}";
        name = JS_GetProperty(ctx, this_val, JS_ATOM_name);
        if (JS_IsUndefined(name))
            name = js_empty_string(ctx->rt);
        return JS_ConcatString3(ctx, pref, name, suff);
    }
}

static JSValue js_function_hasInstance(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    int ret;
    ret = JS_OrdinaryIsInstanceOf(ctx, argv[0], this_val);
    if (ret < 0)
        return JS_EXCEPTION;
    else
        return js_bool(ret);
}

static const JSCFunctionListEntry js_function_proto_funcs[] = {
    JS_CFUNC_DEF("call", 1, js_function_call ),
    JS_CFUNC_MAGIC_DEF("apply", 2, js_function_apply, 0 ),
    JS_CFUNC_DEF("bind", 1, js_function_bind ),
    JS_CFUNC_DEF("toString", 0, js_function_toString ),
    JS_CFUNC_DEF("[Symbol.hasInstance]", 1, js_function_hasInstance ),
    JS_CGETSET_DEF("fileName", js_function_proto_fileName, NULL ),
    JS_CGETSET_MAGIC_DEF("lineNumber", js_function_proto_int32, NULL,
                         offsetof(JSFunctionBytecode, line_num)),
    JS_CGETSET_MAGIC_DEF("columnNumber", js_function_proto_int32, NULL,
                         offsetof(JSFunctionBytecode, col_num)),
};

/* Error class */

static JSValue iterator_to_array(JSContext *ctx, JSValueConst items)
{
    JSValue iter, next_method = JS_UNDEFINED;
    JSValue v, r = JS_UNDEFINED;
    int64_t k;
    int done;

    iter = JS_GetIterator(ctx, items, false);
    if (JS_IsException(iter))
        goto exception;
    next_method = JS_GetProperty(ctx, iter, JS_ATOM_next);
    if (JS_IsException(next_method))
        goto exception;
    r = JS_NewArray(ctx);
    if (JS_IsException(r))
        goto exception;
    for (k = 0;; k++) {
        v = JS_IteratorNext(ctx, iter, next_method, 0, NULL, &done);
        if (JS_IsException(v))
            goto exception_close;
        if (done)
            break;
        if (JS_DefinePropertyValueInt64(ctx, r, k, v,
                                        JS_PROP_C_W_E | JS_PROP_THROW) < 0)
            goto exception_close;
    }
 done:
    JS_FreeValue(ctx, next_method);
    JS_FreeValue(ctx, iter);
    return r;
 exception_close:
    JS_IteratorClose(ctx, iter, true);
 exception:
    JS_FreeValue(ctx, r);
    r = JS_EXCEPTION;
    goto done;
}

static JSValue js_error_constructor(JSContext *ctx, JSValueConst new_target,
                                    int argc, JSValueConst *argv, int magic)
{
    JSValue obj, msg, proto, cause;
    JSValueConst message;
    int present, opts;

    if (JS_IsUndefined(new_target))
        new_target = JS_GetActiveFunction(ctx);
    proto = JS_GetProperty(ctx, new_target, JS_ATOM_prototype);
    if (JS_IsException(proto))
        return proto;
    if (!JS_IsObject(proto)) {
        JSContext *realm;
        JSValue proto1;

        JS_FreeValue(ctx, proto);
        realm = JS_GetFunctionRealm(ctx, new_target);
        if (!realm)
            return JS_EXCEPTION;
        if (magic < 0) {
            proto1 = realm->class_proto[JS_CLASS_ERROR];
        } else {
            proto1 = realm->native_error_proto[magic];
        }
        proto = js_dup(proto1);
    }
    obj = JS_NewObjectProtoClass(ctx, proto, JS_CLASS_ERROR);
    JS_FreeValue(ctx, proto);
    if (JS_IsException(obj))
        return obj;
    switch (magic) {
    case JS_AGGREGATE_ERROR:
        message = argv[1];
        opts = 2;
        break;
    case JS_SUPPRESSED_ERROR:
        message = argv[2];
        opts = 3;
        break;
    default:
        message = argv[0];
        opts = 1;
    }

    if (!JS_IsUndefined(message)) {
        msg = JS_ToString(ctx, message);
        if (unlikely(JS_IsException(msg)))
            goto exception;
        JS_DefinePropertyValue(ctx, obj, JS_ATOM_message, msg,
                               JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    }

    if (argc > opts && JS_VALUE_GET_TAG(argv[opts]) == JS_TAG_OBJECT) {
        present = JS_HasProperty(ctx, argv[opts], JS_ATOM_cause);
        if (unlikely(present < 0))
            goto exception;
        if (present) {
            cause = JS_GetProperty(ctx, argv[opts], JS_ATOM_cause);
            if (unlikely(JS_IsException(cause)))
                goto exception;
            JS_DefinePropertyValue(ctx, obj, JS_ATOM_cause, cause,
                                   JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
        }
    }

    if (magic == JS_AGGREGATE_ERROR) {
        JSValue error_list = iterator_to_array(ctx, argv[0]);
        if (JS_IsException(error_list))
            goto exception;
        JS_DefinePropertyValue(ctx, obj, JS_ATOM_errors, error_list,
                               JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    }

    if (magic == JS_SUPPRESSED_ERROR) {
        JS_DefinePropertyValue(ctx, obj, JS_ATOM_error,
                               js_dup(argv[0]),
                               JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
        JS_DefinePropertyValue(ctx, obj, JS_ATOM_suppressed,
                               js_dup(argv[1]),
                               JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    }

    /* skip the Error() function in the backtrace */
    build_backtrace(ctx, obj, JS_UNDEFINED, NULL, 0, 0, JS_BACKTRACE_FLAG_SKIP_FIRST_LEVEL);
    return obj;
 exception:
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue js_error_toString(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    JSValue name, msg;

    if (!JS_IsObject(this_val))
        return JS_ThrowTypeErrorNotAnObject(ctx);
    name = JS_GetProperty(ctx, this_val, JS_ATOM_name);
    if (JS_IsUndefined(name))
        name = JS_AtomToString(ctx, JS_ATOM_Error);
    else
        name = JS_ToStringFree(ctx, name);
    if (JS_IsException(name))
        return JS_EXCEPTION;

    msg = JS_GetProperty(ctx, this_val, JS_ATOM_message);
    if (JS_IsUndefined(msg))
        msg = js_empty_string(ctx->rt);
    else
        msg = JS_ToStringFree(ctx, msg);
    if (JS_IsException(msg)) {
        JS_FreeValue(ctx, name);
        return JS_EXCEPTION;
    }
    if (!JS_IsEmptyString(name) && !JS_IsEmptyString(msg))
        name = JS_ConcatString3(ctx, "", name, ": ");
    return JS_ConcatString(ctx, name, msg);
}

static JSValue js_error_get_stack(JSContext *ctx, JSValueConst this_val)
{
    JSObject *p;

    if (JS_VALUE_GET_TAG(this_val) != JS_TAG_OBJECT)
        return JS_ThrowTypeErrorNotAnObject(ctx);
    p = JS_VALUE_GET_OBJ(this_val);
    if (p->class_id != JS_CLASS_ERROR)
        return JS_UNDEFINED;
    return js_dup(p->u.object_data);
}

static JSValue js_error_set_stack(JSContext *ctx, JSValueConst this_val,
                                  JSValueConst value)
{
    int ret, flags;

    if (JS_VALUE_GET_TAG(this_val) != JS_TAG_OBJECT)
        return JS_ThrowTypeErrorNotAnObject(ctx);
    if (!JS_IsString(value))
        return JS_ThrowTypeError(ctx, "Error.prototype.stack setter expects a string");
    if (js_same_value(ctx, this_val, ctx->class_proto[JS_CLASS_ERROR]))
        return JS_ThrowTypeError(ctx, "Error.prototype.stack setter called on the home object");
    ret = JS_GetOwnPropertyFlagsInternal(ctx, &flags, JS_VALUE_GET_OBJ(this_val),
                                         JS_ATOM_stack);
    if (ret < 0)
        return JS_EXCEPTION;
    if (ret == 0) {
        if (JS_DefinePropertyValue(ctx, this_val, JS_ATOM_stack, js_dup(value),
                                   JS_PROP_C_W_E | JS_PROP_THROW) < 0)
            return JS_EXCEPTION;
    } else {
        if (JS_SetPropertyInternal2(ctx, this_val, JS_ATOM_stack, js_dup(value),
                                    this_val, JS_PROP_THROW) < 0)
            return JS_EXCEPTION;
    }
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_error_proto_funcs[] = {
    JS_CFUNC_DEF("toString", 0, js_error_toString ),
    JS_PROP_STRING_DEF("name", "Error", JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE ),
    JS_PROP_STRING_DEF("message", "", JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE ),
    JS_CGETSET_DEF("stack", js_error_get_stack, js_error_set_stack ),
};

static JSValue js_error_isError(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    return js_bool(JS_IsError(argv[0]));
}

static JSValue js_error_get_stackTraceLimit(JSContext *ctx, JSValueConst this_val)
{
    JSValue val;

    val = JS_ToObject(ctx, this_val);
    if (JS_IsException(val))
        return val;
    JS_FreeValue(ctx, val);
    return js_dup(ctx->error_stack_trace_limit);
}

static JSValue js_error_set_stackTraceLimit(JSContext *ctx, JSValueConst this_val, JSValueConst value)
{
    if (JS_IsUndefined(this_val) || JS_IsNull(this_val))
        return JS_ThrowTypeErrorNotAnObject(ctx);
    ctx->error_stack_trace_limit = js_dup(value);
    return JS_UNDEFINED;
}

static JSValue js_error_get_prepareStackTrace(JSContext *ctx, JSValueConst this_val)
{
    JSValue val;

    val = JS_ToObject(ctx, this_val);
    if (JS_IsException(val))
        return val;
    JS_FreeValue(ctx, val);
    return js_dup(ctx->error_prepare_stack);
}

static JSValue js_error_set_prepareStackTrace(JSContext *ctx, JSValueConst this_val, JSValueConst value)
{
    if (JS_IsUndefined(this_val) || JS_IsNull(this_val))
        return JS_ThrowTypeErrorNotAnObject(ctx);
    JS_FreeValue(ctx, ctx->error_prepare_stack);
    ctx->error_prepare_stack = js_dup(value);
    return JS_UNDEFINED;
}

static JSValue js_error_capture_stack_trace(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv)
{
    JSValueConst v = argv[0];
    if (JS_VALUE_GET_TAG(v) != JS_TAG_OBJECT)
        return JS_ThrowTypeErrorNotAnObject(ctx);
    build_backtrace(ctx, v, argv[1], NULL, 0, 0, JS_BACKTRACE_FLAG_SKIP_FIRST_LEVEL|JS_BACKTRACE_FLAG_FILTER_FUNC);
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_error_funcs[] = {
    JS_CFUNC_DEF("isError", 1, js_error_isError ),
    JS_CFUNC_DEF("captureStackTrace", 2, js_error_capture_stack_trace),
    JS_CGETSET_DEF("stackTraceLimit", js_error_get_stackTraceLimit, js_error_set_stackTraceLimit ),
    JS_CGETSET_DEF("prepareStackTrace", js_error_get_prepareStackTrace, js_error_set_prepareStackTrace ),
};

/* AggregateError */

/* used by C code. */
static JSValue js_aggregate_error_constructor(JSContext *ctx,
                                              JSValueConst errors)
{
    JSValue obj;

    obj = JS_NewObjectProtoClass(ctx,
                                 ctx->native_error_proto[JS_AGGREGATE_ERROR],
                                 JS_CLASS_ERROR);
    if (JS_IsException(obj))
        return obj;
    JS_DefinePropertyValueConst(ctx, obj, JS_ATOM_errors, errors,
                               JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    return obj;
}

/* Array */

static int JS_CopySubArray(JSContext *ctx,
                           JSValue obj, int64_t to_pos,
                           int64_t from_pos, int64_t count, int dir)
{
    JSObject *p;
    int64_t i, from, to, len;
    JSValue val;
    int fromPresent;

    p = NULL;
    if (JS_VALUE_GET_TAG(obj) == JS_TAG_OBJECT) {
        p = JS_VALUE_GET_OBJ(obj);
        if (p->class_id != JS_CLASS_ARRAY || !p->fast_array) {
            p = NULL;
        }
    }

    for (i = 0; i < count; ) {
        if (dir < 0) {
            from = from_pos + count - i - 1;
            to = to_pos + count - i - 1;
        } else {
            from = from_pos + i;
            to = to_pos + i;
        }
        if (p && p->fast_array &&
            from >= 0 && from < (len = p->u.array.count)  &&
            to >= 0 && to < len) {
            int64_t l, j;
            /* Fast path for fast arrays. Since we don't look at the
               prototype chain, we can optimize only the cases where
               all the elements are present in the array. */
            l = count - i;
            if (dir < 0) {
                l = min_int64(l, from + 1);
                l = min_int64(l, to + 1);
                for(j = 0; j < l; j++) {
                    set_value(ctx, &p->u.array.u.values[to - j],
                              js_dup(p->u.array.u.values[from - j]));
                }
            } else {
                l = min_int64(l, len - from);
                l = min_int64(l, len - to);
                for(j = 0; j < l; j++) {
                    set_value(ctx, &p->u.array.u.values[to + j],
                              js_dup(p->u.array.u.values[from + j]));
                }
            }
            i += l;
        } else {
            fromPresent = JS_TryGetPropertyInt64(ctx, obj, from, &val);
            if (fromPresent < 0)
                goto exception;

            if (fromPresent) {
                if (JS_SetPropertyInt64(ctx, obj, to, val) < 0)
                    goto exception;
            } else {
                if (JS_DeletePropertyInt64(ctx, obj, to, JS_PROP_THROW) < 0)
                    goto exception;
            }
            i++;
        }
    }
    return 0;

 exception:
    return -1;
}

static JSValue js_array_constructor(JSContext *ctx, JSValueConst new_target,
                                    int argc, JSValueConst *argv)
{
    JSValue obj;
    int i;

    obj = js_create_from_ctor(ctx, new_target, JS_CLASS_ARRAY);
    if (JS_IsException(obj))
        return obj;
    if (argc == 1 && JS_IsNumber(argv[0])) {
        uint32_t len;
        if (JS_ToArrayLengthFree(ctx, &len, js_dup(argv[0]), true))
            goto fail;
        if (JS_SetProperty(ctx, obj, JS_ATOM_length, js_uint32(len)) < 0)
            goto fail;
    } else {
        for(i = 0; i < argc; i++) {
            if (JS_SetPropertyUint32(ctx, obj, i, js_dup(argv[i])) < 0)
                goto fail;
        }
    }
    return obj;
fail:
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue js_array_from(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    // from(items, mapfn = void 0, this_arg = void 0)
    JSValueConst items = argv[0], mapfn, this_arg;
    JSValueConst args[2];
    JSValue stack[2];
    JSValue iter, r, v, v2, arrayLike;
    int64_t k, len;
    int done, mapping;

    mapping = false;
    mapfn = JS_UNDEFINED;
    this_arg = JS_UNDEFINED;
    r = JS_UNDEFINED;
    arrayLike = JS_UNDEFINED;
    stack[0] = JS_UNDEFINED;
    stack[1] = JS_UNDEFINED;

    if (argc > 1) {
        mapfn = argv[1];
        if (!JS_IsUndefined(mapfn)) {
            if (check_function(ctx, mapfn))
                goto exception;
            mapping = 1;
            if (argc > 2)
                this_arg = argv[2];
        }
    }
    iter = JS_GetProperty(ctx, items, JS_ATOM_Symbol_iterator);
    if (JS_IsException(iter))
        goto exception;
    if (!JS_IsUndefined(iter)) {
        JS_FreeValue(ctx, iter);
        if (JS_IsConstructor(ctx, this_val))
            r = JS_CallConstructor(ctx, this_val, 0, NULL);
        else
            r = JS_NewArray(ctx);
        if (JS_IsException(r))
            goto exception;
        stack[0] = js_dup(items);
        if (js_for_of_start(ctx, &stack[1], false))
            goto exception;
        for (k = 0;; k++) {
            v = JS_IteratorNext(ctx, stack[0], stack[1], 0, NULL, &done);
            if (JS_IsException(v))
                goto exception_close;
            if (done)
                break;
            if (mapping) {
                args[0] = v;
                args[1] = js_int32(k);
                v2 = JS_Call(ctx, mapfn, this_arg, 2, args);
                JS_FreeValue(ctx, v);
                v = v2;
                if (JS_IsException(v))
                    goto exception_close;
            }
            if (JS_DefinePropertyValueInt64(ctx, r, k, v,
                                            JS_PROP_C_W_E | JS_PROP_THROW) < 0)
                goto exception_close;
        }
    } else {
        arrayLike = JS_ToObject(ctx, items);
        if (JS_IsException(arrayLike))
            goto exception;
        if (js_get_length64(ctx, &len, arrayLike) < 0)
            goto exception;
        v = js_int64(len);
        args[0] = v;
        if (JS_IsConstructor(ctx, this_val)) {
            r = JS_CallConstructor(ctx, this_val, 1, args);
        } else {
            r = js_array_constructor(ctx, JS_UNDEFINED, 1, args);
        }
        JS_FreeValue(ctx, v);
        if (JS_IsException(r))
            goto exception;
        for(k = 0; k < len; k++) {
            v = JS_GetPropertyInt64(ctx, arrayLike, k);
            if (JS_IsException(v))
                goto exception;
            if (mapping) {
                args[0] = v;
                args[1] = js_int32(k);
                v2 = JS_Call(ctx, mapfn, this_arg, 2, args);
                JS_FreeValue(ctx, v);
                v = v2;
                if (JS_IsException(v))
                    goto exception;
            }
            if (JS_DefinePropertyValueInt64(ctx, r, k, v,
                                            JS_PROP_C_W_E | JS_PROP_THROW) < 0)
                goto exception;
        }
    }
    if (JS_SetProperty(ctx, r, JS_ATOM_length, js_uint32(k)) < 0)
        goto exception;
    goto done;

 exception_close:
    if (!JS_IsUndefined(stack[0]))
        JS_IteratorClose(ctx, stack[0], true);
 exception:
    JS_FreeValue(ctx, r);
    r = JS_EXCEPTION;
 done:
    JS_FreeValue(ctx, arrayLike);
    JS_FreeValue(ctx, stack[0]);
    JS_FreeValue(ctx, stack[1]);
    return r;
}

static JSValue js_array_of(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    JSValue n, obj;
    int i;

    if (JS_IsConstructor(ctx, this_val)) {
        n = js_int32(argc);
        obj = JS_CallConstructor(ctx, this_val, 1, vc(&n));
    } else {
        obj = JS_NewArray(ctx);
    }
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    for(i = 0; i < argc; i++) {
        if (JS_CreateDataPropertyUint32Const(ctx, obj, i, argv[i],
                                             JS_PROP_THROW) < 0) {
            goto fail;
        }
    }
    if (JS_SetProperty(ctx, obj, JS_ATOM_length, js_uint32(argc)) < 0) {
    fail:
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    return obj;
}

static JSValue js_array_isArray(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    int ret;
    ret = js_is_array(ctx, argv[0]);
    if (ret < 0)
        return JS_EXCEPTION;
    else
        return js_bool(ret);
}

static JSValue js_get_this(JSContext *ctx, JSValueConst this_val)
{
    return js_dup(this_val);
}

static JSValue JS_ArraySpeciesCreate(JSContext *ctx, JSValueConst obj,
                                     JSValueConst len_val)
{
    JSValue ctor, ret, species;
    int res;
    JSContext *realm;

    res = js_is_array(ctx, obj);
    if (res < 0)
        return JS_EXCEPTION;
    if (!res)
        return js_array_constructor(ctx, JS_UNDEFINED, 1, &len_val);
    ctor = JS_GetProperty(ctx, obj, JS_ATOM_constructor);
    if (JS_IsException(ctor))
        return ctor;
    if (JS_IsConstructor(ctx, ctor)) {
        /* legacy web compatibility */
        realm = JS_GetFunctionRealm(ctx, ctor);
        if (!realm) {
            JS_FreeValue(ctx, ctor);
            return JS_EXCEPTION;
        }
        if (realm != ctx &&
            js_same_value(ctx, ctor, realm->array_ctor)) {
            JS_FreeValue(ctx, ctor);
            ctor = JS_UNDEFINED;
        }
    }
    if (JS_IsObject(ctor)) {
        species = JS_GetProperty(ctx, ctor, JS_ATOM_Symbol_species);
        JS_FreeValue(ctx, ctor);
        if (JS_IsException(species))
            return species;
        ctor = species;
        if (JS_IsNull(ctor))
            ctor = JS_UNDEFINED;
    }
    if (JS_IsUndefined(ctor)) {
        return js_array_constructor(ctx, JS_UNDEFINED, 1, &len_val);
    } else {
        ret = JS_CallConstructor(ctx, ctor, 1, &len_val);
        JS_FreeValue(ctx, ctor);
        return ret;
    }
}

static const JSCFunctionListEntry js_array_funcs[] = {
    JS_CFUNC_DEF("isArray", 1, js_array_isArray ),
    JS_CFUNC_DEF("from", 1, js_array_from ),
    JS_CFUNC_DEF("of", 0, js_array_of ),
    JS_CGETSET_DEF("[Symbol.species]", js_get_this, NULL ),
};

static int JS_isConcatSpreadable(JSContext *ctx, JSValueConst obj)
{
    JSValue val;

    if (!JS_IsObject(obj))
        return false;
    val = JS_GetProperty(ctx, obj, JS_ATOM_Symbol_isConcatSpreadable);
    if (JS_IsException(val))
        return -1;
    if (!JS_IsUndefined(val))
        return JS_ToBoolFree(ctx, val);
    return js_is_array(ctx, obj);
}

static JSValue js_array_at(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    JSValue obj, ret;
    int64_t len, idx;

    ret = JS_EXCEPTION;
    obj = JS_ToObject(ctx, this_val);
    if (js_get_length64(ctx, &len, obj))
        goto exception;

    if (JS_ToInt64Sat(ctx, &idx, argv[0]))
        goto exception;

    if (idx < 0)
        idx = len + idx;

    if (idx < 0 || idx >= len) {
        ret = JS_UNDEFINED;
    } else {
        ret = JS_GetPropertyInt64(ctx, obj, idx);
    }

 exception:
    JS_FreeValue(ctx, obj);
    return ret;
}

static JSValue js_array_with(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    JSValue arr, obj, ret, *arrp, *pval;
    JSObject *p;
    int64_t i, len, idx;
    uint32_t count32;

    ret = JS_EXCEPTION;
    arr = JS_UNDEFINED;
    obj = JS_ToObject(ctx, this_val);
    if (js_get_length64(ctx, &len, obj))
        goto exception;

    if (JS_ToInt64Sat(ctx, &idx, argv[0]))
        goto exception;

    if (idx < 0)
        idx = len + idx;

    if (idx < 0 || idx >= len) {
        JS_ThrowRangeError(ctx, "invalid array index: %" PRId64, idx);
        goto exception;
    }

    arr = js_allocate_fast_array(ctx, len);
    if (JS_IsException(arr))
        goto exception;

    p = JS_VALUE_GET_OBJ(arr);
    i = 0;
    pval = p->u.array.u.values;
    if (js_get_fast_array(ctx, obj, &arrp, &count32) && count32 == len) {
        for (; i < idx; i++, pval++)
            *pval = js_dup(arrp[i]);
        *pval = js_dup(argv[1]);
        for (i++, pval++; i < len; i++, pval++)
            *pval = js_dup(arrp[i]);
    } else {
        for (; i < idx; i++, pval++)
            if (-1 == JS_TryGetPropertyInt64(ctx, obj, i, pval))
                goto exception;
        *pval = js_dup(argv[1]);
        for (i++, pval++; i < len; i++, pval++) {
            if (-1 == JS_TryGetPropertyInt64(ctx, obj, i, pval))
                goto exception;
        }
    }

    ret = arr;
    arr = JS_UNDEFINED;

exception:
    JS_FreeValue(ctx, arr);
    JS_FreeValue(ctx, obj);
    return ret;
}

static JSValue js_array_concat(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    JSValue obj, arr, val;
    JSValueConst e;
    int64_t len, k, n;
    int i, res;

    arr = JS_UNDEFINED;
    obj = JS_ToObject(ctx, this_val);
    if (JS_IsException(obj))
        goto exception;

    arr = JS_ArraySpeciesCreate(ctx, obj, js_int32(0));
    if (JS_IsException(arr))
        goto exception;
    n = 0;
    for (i = -1; i < argc; i++) {
        if (i < 0)
            e = obj;
        else
            e = argv[i];

        res = JS_isConcatSpreadable(ctx, e);
        if (res < 0)
            goto exception;
        if (res) {
            if (js_get_length64(ctx, &len, e))
                goto exception;
            if (n + len > MAX_SAFE_INTEGER) {
                JS_ThrowTypeError(ctx, "Array loo long");
                goto exception;
            }
            for (k = 0; k < len; k++, n++) {
                res = JS_TryGetPropertyInt64(ctx, e, k, &val);
                if (res < 0)
                    goto exception;
                if (res) {
                    if (JS_DefinePropertyValueInt64(ctx, arr, n, val,
                                                    JS_PROP_C_W_E | JS_PROP_THROW) < 0)
                        goto exception;
                }
            }
        } else {
            if (n >= MAX_SAFE_INTEGER) {
                JS_ThrowTypeError(ctx, "Array loo long");
                goto exception;
            }
            if (JS_DefinePropertyValueInt64Const(ctx, arr, n, e,
                                                 JS_PROP_C_W_E | JS_PROP_THROW) < 0)
                goto exception;
            n++;
        }
    }
    if (JS_SetProperty(ctx, arr, JS_ATOM_length, js_int64(n)) < 0)
        goto exception;

    JS_FreeValue(ctx, obj);
    return arr;

exception:
    JS_FreeValue(ctx, arr);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

#define special_every    0
#define special_some     1
#define special_forEach  2
#define special_map      3
#define special_filter   4
#define special_TA       8

static JSObject *get_typed_array(JSContext *ctx, JSValueConst this_val)
{
    JSObject *p;
    if (JS_VALUE_GET_TAG(this_val) != JS_TAG_OBJECT)
        goto fail;
    p = JS_VALUE_GET_OBJ(this_val);
    if (!is_typed_array(p->class_id)) {
    fail:
        JS_ThrowTypeError(ctx, "not a TypedArray");
        return NULL;
    }
    return p;
}

// Be *very* careful if you touch the typed array's memory directly:
// the length is only valid until the next call into JS land because
// JS code can detach or resize the backing array buffer. Functions
// like JS_GetProperty and JS_ToIndex call JS code.
//
// Exclusively reading or writing elements with JS_GetProperty,
// JS_GetPropertyInt64, JS_SetProperty, etc. is safe because they
// perform bounds checks, as does js_get_fast_array_element.
static int js_typed_array_get_length_unsafe(JSContext *ctx, JSValueConst obj)
{
    JSObject *p;
    p = get_typed_array(ctx, obj);
    if (!p)
        return -1;
    if (typed_array_is_oob(p)) {
        JS_ThrowTypeErrorArrayBufferOOB(ctx);
        return -1;
    }
    return p->u.array.count;
}

static JSValue js_typed_array___speciesCreate(JSContext *ctx,
                                              JSValueConst this_val,
                                              int argc, JSValueConst *argv,
                                              bool require_mutable);

static JSValue js_array_every(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv, int special)
{
    JSValue obj, val, index_val, res, ret;
    JSValueConst args[3];
    JSValueConst func, this_arg;
    int64_t len, k, n;
    int present;

    ret = JS_UNDEFINED;
    val = JS_UNDEFINED;
    if (special & special_TA) {
        obj = js_dup(this_val);
        len = js_typed_array_get_length_unsafe(ctx, obj);
        if (len < 0)
            goto exception;
    } else {
        obj = JS_ToObject(ctx, this_val);
        if (js_get_length64(ctx, &len, obj))
            goto exception;
    }
    func = argv[0];
    this_arg = JS_UNDEFINED;
    if (argc > 1)
        this_arg = argv[1];

    if (check_function(ctx, func))
        goto exception;

    switch (special) {
    case special_every:
    case special_every | special_TA:
        ret = JS_TRUE;
        break;
    case special_some:
    case special_some | special_TA:
        ret = JS_FALSE;
        break;
    case special_map:
        /* XXX: JS_ArraySpeciesCreate should take int64_t */
        ret = JS_ArraySpeciesCreate(ctx, obj, js_int64(len));
        if (JS_IsException(ret))
            goto exception;
        break;
    case special_filter:
        ret = JS_ArraySpeciesCreate(ctx, obj, js_int32(0));
        if (JS_IsException(ret))
            goto exception;
        break;
    case special_map | special_TA:
        args[0] = obj;
        args[1] = js_int32(len);
        ret = js_typed_array___speciesCreate(ctx, JS_UNDEFINED, 2, args, true);
        if (JS_IsException(ret))
            goto exception;
        break;
    case special_filter | special_TA:
        ret = JS_NewArray(ctx);
        if (JS_IsException(ret))
            goto exception;
        break;
    }
    n = 0;

    for(k = 0; k < len; k++) {
        if (special & special_TA) {
            val = JS_GetPropertyInt64(ctx, obj, k);
            if (JS_IsException(val))
                goto exception;
            present = true;
        } else {
            present = JS_TryGetPropertyInt64(ctx, obj, k, &val);
            if (present < 0)
                goto exception;
        }
        if (present) {
            index_val = js_int64(k);
            args[0] = val;
            args[1] = index_val;
            args[2] = obj;
            res = JS_Call(ctx, func, this_arg, 3, args);
            JS_FreeValue(ctx, index_val);
            if (JS_IsException(res))
                goto exception;
            switch (special) {
            case special_every:
            case special_every | special_TA:
                if (!JS_ToBoolFree(ctx, res)) {
                    ret = JS_FALSE;
                    goto done;
                }
                break;
            case special_some:
            case special_some | special_TA:
                if (JS_ToBoolFree(ctx, res)) {
                    ret = JS_TRUE;
                    goto done;
                }
                break;
            case special_map:
                if (JS_DefinePropertyValueInt64(ctx, ret, k, res,
                                                JS_PROP_C_W_E | JS_PROP_THROW) < 0)
                    goto exception;
                break;
            case special_map | special_TA:
                if (JS_SetPropertyValue(ctx, ret, js_int32(k), res, JS_PROP_THROW) < 0)
                    goto exception;
                break;
            case special_filter:
            case special_filter | special_TA:
                if (JS_ToBoolFree(ctx, res)) {
                    if (JS_DefinePropertyValueInt64Const(ctx, ret, n++, val,
                                                         JS_PROP_C_W_E | JS_PROP_THROW) < 0)
                        goto exception;
                }
                break;
            default:
                JS_FreeValue(ctx, res);
                break;
            }
            JS_FreeValue(ctx, val);
            val = JS_UNDEFINED;
        }
    }
done:
    if (special == (special_filter | special_TA)) {
        JSValue arr;
        args[0] = obj;
        args[1] = js_int32(n);
        arr = js_typed_array___speciesCreate(ctx, JS_UNDEFINED, 2, args, true);
        if (JS_IsException(arr))
            goto exception;
        args[0] = ret;
        res = JS_Invoke(ctx, arr, JS_ATOM_set, 1, args);
        if (check_exception_free(ctx, res)) {
            JS_FreeValue(ctx, arr);
            goto exception;
        }
        JS_FreeValue(ctx, ret);
        ret = arr;
    }
    JS_FreeValue(ctx, val);
    JS_FreeValue(ctx, obj);
    return ret;

exception:
    JS_FreeValue(ctx, ret);
    JS_FreeValue(ctx, val);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

#define special_reduce       0
#define special_reduceRight  1

