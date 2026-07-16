/* Engine domain source: gc/gc_exceptions_properties.inc -> exception_core.
 * Ownership: gc subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static bool can_store_error_stack(JSValueConst obj)
{
    JSObject *p;
    if (JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT)
        return false;
    p = JS_VALUE_GET_OBJ(obj);
    if (p->class_id != JS_CLASS_ERROR)
        return false;
    if (!JS_IsUndefined(p->u.object_data))
        return false;
    if (find_own_property1(p, JS_ATOM_stack))
        return false;
    return true;
}

#define JS_BACKTRACE_FLAG_SKIP_FIRST_LEVEL (1 << 0)
/* only taken into account if filename is provided */
#define JS_BACKTRACE_FLAG_SINGLE_LEVEL     (1 << 1)
#define JS_BACKTRACE_FLAG_FILTER_FUNC      (1 << 2)

/* if filename != NULL, an additional level is added with the filename
   and line number information (used for parse error). */
static void build_backtrace(JSContext *ctx, JSValueConst error_val,
                            JSValueConst filter_func, const char *filename,
                            int line_num, int col_num, int backtrace_flags)
{
    JSStackFrame *sf, *sf_start;
    JSValue stack, prepare, saved_exception, error_obj;
    DynBuf dbuf;
    const char *func_name_str;
    const char *str1;
    JSObject *p;
    JSFunctionBytecode *b;
    bool backtrace_barrier, has_prepare, has_filter_func;
    JSRuntime *rt;
    JSCallSiteData csd[64];
    uint32_t i;
    double d;
    int stack_trace_limit;

    rt = ctx->rt;
    if (rt->in_build_stack_trace)
        return;
    rt->in_build_stack_trace = true;
    error_obj = js_dup(error_val);

    // Save exception because conversion to double may fail.
    saved_exception = JS_GetException(ctx);

    // Extract stack trace limit.
    // Ignore error since it sets d to NAN anyway.
    // coverity[check_return]
    JS_ToFloat64(ctx, &d, ctx->error_stack_trace_limit);
    if (isnan(d) || d < 0.0)
        stack_trace_limit = 0;
    else if (d > INT32_MAX)
        stack_trace_limit = INT32_MAX;
    else
        stack_trace_limit = fabs(d);

    // Restore current exception.
    JS_Throw(ctx, saved_exception);
    saved_exception = JS_UNINITIALIZED;

    stack_trace_limit = min_int(stack_trace_limit, countof(csd));
    stack_trace_limit = max_int(stack_trace_limit, 0);
    has_prepare = false;
    has_filter_func = backtrace_flags & JS_BACKTRACE_FLAG_FILTER_FUNC;
    i = 0;

    if (!JS_IsNull(ctx->error_ctor)) {
        prepare = js_dup(ctx->error_prepare_stack);
        has_prepare = JS_IsFunction(ctx, prepare);
    }

    if (has_prepare) {
        saved_exception = JS_GetException(ctx);
        if (stack_trace_limit == 0)
            goto done;
        if (filename)
            js_new_callsite_data2(ctx, &csd[i++], filename, line_num, col_num);
    } else {
        js_dbuf_init(ctx, &dbuf);
        if (stack_trace_limit == 0)
            goto done;
        if (filename) {
            i++;
            dbuf_printf(&dbuf, "    at %s", filename);
            if (line_num != -1)
                dbuf_printf(&dbuf, ":%d:%d", line_num, col_num);
            dbuf_putc(&dbuf, '\n');
        }
    }

    if (filename && (backtrace_flags & JS_BACKTRACE_FLAG_SINGLE_LEVEL))
        goto done;

    sf_start = rt->current_stack_frame;

    /* Find the frame we want to start from. Note that when a filter is used the filter
       function will be the first, but we also specify we want to skip the first one. */
    if (has_filter_func) {
        for (sf = sf_start; sf != NULL && i < stack_trace_limit; sf = sf->prev_frame) {
            if (js_same_value(ctx, sf->cur_func, filter_func)) {
                sf_start = sf;
                break;
            }
        }
    }

    for (sf = sf_start; sf != NULL && i < stack_trace_limit; sf = sf->prev_frame) {
        if (backtrace_flags & JS_BACKTRACE_FLAG_SKIP_FIRST_LEVEL) {
            backtrace_flags &= ~JS_BACKTRACE_FLAG_SKIP_FIRST_LEVEL;
            continue;
        }

        p = JS_VALUE_GET_OBJ(sf->cur_func);
        b = NULL;
        backtrace_barrier = false;

        if (js_class_has_bytecode(p->class_id)) {
            b = p->u.func.function_bytecode;
            backtrace_barrier = b->backtrace_barrier;
        }

        if (has_prepare) {
            js_new_callsite_data(ctx, &csd[i], sf);
        } else {
            /* func_name_str is UTF-8 encoded if needed */
            func_name_str = get_func_name(ctx, sf->cur_func);
            if (!func_name_str || func_name_str[0] == '\0')
                str1 = "<anonymous>";
            else
                str1 = func_name_str;
            dbuf_printf(&dbuf, "    at %s", str1);
            JS_FreeCString(ctx, func_name_str);

            if (b && sf->cur_pc) {
                const char *atom_str;
                int line_num1, col_num1;
                uint32_t pc;

                pc = sf->cur_pc - b->byte_code_buf - 1;
                line_num1 = find_line_num(ctx, b, pc, &col_num1);
                atom_str = b->filename ? JS_AtomToCString(ctx, b->filename) : NULL;
                dbuf_printf(&dbuf, " (%s", atom_str ? atom_str : "<null>");
                JS_FreeCString(ctx, atom_str);
                if (line_num1 != -1)
                    dbuf_printf(&dbuf, ":%d:%d", line_num1, col_num1);
                dbuf_putc(&dbuf, ')');
            } else if (b) {
                // FIXME(bnoordhuis) Missing `sf->cur_pc = pc` in bytecode
                // handler in JS_CallInternal. Almost never user observable
                // except with intercepting JS proxies that throw exceptions.
                dbuf_printf(&dbuf, " (missing)");
            } else {
                dbuf_printf(&dbuf, " (native)");
            }
            dbuf_putc(&dbuf, '\n');
        }
        i++;

        /* stop backtrace if JS_EVAL_FLAG_BACKTRACE_BARRIER was used */
        if (backtrace_barrier)
            break;
    }
 done:
    if (has_prepare) {
        int j = 0, k;
        stack = JS_NewArray(ctx);
        if (JS_IsException(stack)) {
            stack = JS_NULL;
        } else {
            for (; j < i; j++) {
                JSValue v = js_new_callsite(ctx, &csd[j]);
                if (JS_IsException(v))
                    break;
                if (JS_DefinePropertyValueUint32(ctx, stack, j, v, JS_PROP_C_W_E) < 0) {
                    JS_FreeValue(ctx, v);
                    break;
                }
            }
        }
        // Clear the csd's we didn't use in case of error.
        for (k = j; k < i; k++) {
            JS_FreeValue(ctx, csd[k].filename);
            JS_FreeValue(ctx, csd[k].func);
            JS_FreeValue(ctx, csd[k].func_name);
        }
        JSValueConst args[] = {
            error_obj,
            stack,
        };
        JSValue stack2 = JS_Call(ctx, prepare, ctx->error_ctor, countof(args), args);
        JS_FreeValue(ctx, stack);
        if (JS_IsException(stack2))
            stack = JS_NULL;
        else
            stack = stack2;
        JS_FreeValue(ctx, prepare);
        JS_Throw(ctx, saved_exception);
    } else {
        if (dbuf_error(&dbuf))
            stack = JS_NULL;
        else
            stack = JS_NewStringLen(ctx, (char *)dbuf.buf, dbuf.size);
        dbuf_free(&dbuf);
    }

    if (JS_IsUndefined(ctx->error_back_trace))
        ctx->error_back_trace = js_dup(stack);
    if (has_filter_func) {
        /* Error.captureStackTrace(target, ...): install an own data property
           on the (possibly non-Error) target, shadowing the accessor */
        JS_DefinePropertyValue(ctx, error_obj, JS_ATOM_stack, stack,
                               JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    } else if (can_store_error_stack(error_obj)) {
        /* genuine Error instance: store as the [[ErrorData]] stack value */
        p = JS_VALUE_GET_OBJ(error_obj);
        JS_FreeValue(ctx, p->u.object_data);
        p->u.object_data = stack;
    } else if (can_add_backtrace(error_obj)) {
        /* DOMException and the like keep an own "stack" data property */
        JS_DefinePropertyValue(ctx, error_obj, JS_ATOM_stack, stack,
                               JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    } else {
        JS_FreeValue(ctx, stack);
    }

    JS_FreeValue(ctx, error_obj);
    rt->in_build_stack_trace = false;
}

JSValue JS_NewError(JSContext *ctx)
{
    JSValue obj = JS_NewObjectClass(ctx, JS_CLASS_ERROR);
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    build_backtrace(ctx, obj, JS_UNDEFINED, NULL, 0, 0, 0);
    return obj;
}

static JSValue JS_MakeError2(JSContext *ctx, JSErrorEnum error_num,
                             bool add_backtrace, const char *message)
{
    JSValue obj, msg;

    if (error_num == JS_PLAIN_ERROR) {
        obj = JS_NewObjectClass(ctx, JS_CLASS_ERROR);
    } else {
        obj = JS_NewObjectProtoClass(ctx, ctx->native_error_proto[error_num],
                                     JS_CLASS_ERROR);
    }
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    msg = JS_NewString(ctx, message);
    if (JS_IsException(msg))
        msg = JS_NewString(ctx, "Invalid error message");
    if (!JS_IsException(msg)) {
        JS_DefinePropertyValue(ctx, obj, JS_ATOM_message, msg,
                               JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    }
    if (add_backtrace)
        build_backtrace(ctx, obj, JS_UNDEFINED, NULL, 0, 0, 0);
    return obj;
}

static JSValue JS_PRINTF_FORMAT_ATTR(4, 0)
JS_MakeError(JSContext *ctx, JSErrorEnum error_num, bool add_backtrace,
             JS_PRINTF_FORMAT const char *fmt, va_list ap)
{
    char buf[256];

    vsnprintf(buf, sizeof(buf), fmt, ap);
    return JS_MakeError2(ctx, error_num, add_backtrace, buf);
}

/* fmt and arguments may be pure ASCII or UTF-8 encoded contents */
static JSValue JS_PRINTF_FORMAT_ATTR(4, 0)
JS_ThrowError2(JSContext *ctx, JSErrorEnum error_num, bool add_backtrace,
               JS_PRINTF_FORMAT const char *fmt, va_list ap)
{
    JSValue obj;

    obj = JS_MakeError(ctx, error_num, add_backtrace, fmt, ap);
    if (unlikely(JS_IsException(obj))) {
        /* out of memory: throw JS_NULL to avoid recursing */
        obj = JS_NULL;
    }
    return JS_Throw(ctx, obj);
}

static JSValue JS_PRINTF_FORMAT_ATTR(3, 0)
JS_ThrowError(JSContext *ctx, JSErrorEnum error_num,
              JS_PRINTF_FORMAT const char *fmt, va_list ap)
{
    JSRuntime *rt = ctx->rt;
    JSStackFrame *sf;
    bool add_backtrace;

    /* the backtrace is added later if called from a bytecode function */
    sf = rt->current_stack_frame;
    add_backtrace = !rt->in_out_of_memory &&
        (!sf || (JS_GetFunctionBytecode(sf->cur_func) == NULL));
    return JS_ThrowError2(ctx, error_num, add_backtrace, fmt, ap);
}

#define JS_ERROR_MAP(X)     \
    X(Internal, INTERNAL)   \
    X(Plain, PLAIN)         \
    X(Range, RANGE)         \
    X(Reference, REFERENCE) \
    X(Syntax, SYNTAX)       \
    X(Type, TYPE)           \

#define X(lc, uc)   \
    JSValue JS_PRINTF_FORMAT_ATTR(2, 3)                         \
    JS_New##lc##Error(JSContext *ctx,                           \
                      JS_PRINTF_FORMAT const char *fmt, ...)    \
    {                                                           \
        JSValue val;                                            \
        va_list ap;                                             \
                                                                \
        va_start(ap, fmt);                                      \
        val = JS_MakeError(ctx, JS_##uc##_ERROR,                \
                           /*add_backtrace*/true, fmt, ap);     \
        va_end(ap);                                             \
        return val;                                             \
    }                                                           \
    JSValue JS_PRINTF_FORMAT_ATTR(2, 3)                         \
    JS_Throw##lc##Error(JSContext *ctx,                         \
                        JS_PRINTF_FORMAT const char *fmt, ...)  \
    {                                                           \
        JSValue val;                                            \
        va_list ap;                                             \
                                                                \
        va_start(ap, fmt);                                      \
        val = JS_ThrowError(ctx, JS_##uc##_ERROR, fmt, ap);     \
        va_end(ap);                                             \
        return val;                                             \
    }                                                           \

JS_ERROR_MAP(X)

#undef X
#undef JS_ERROR_MAP

static int JS_PRINTF_FORMAT_ATTR(3, 4) JS_ThrowTypeErrorOrFalse(JSContext *ctx, int flags, JS_PRINTF_FORMAT const char *fmt, ...)
{
    va_list ap;

    if ((flags & JS_PROP_THROW) ||
        ((flags & JS_PROP_THROW_STRICT) && is_strict_mode(ctx))) {
        va_start(ap, fmt);
        JS_ThrowError(ctx, JS_TYPE_ERROR, fmt, ap);
        va_end(ap);
        return -1;
    } else {
        return false;
    }
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif // __GNUC__
static JSValue JS_ThrowTypeErrorAtom(JSContext *ctx, const char *fmt, JSAtom atom)
{
    char buf[ATOM_GET_STR_BUF_SIZE];
    JS_AtomGetStr(ctx, buf, sizeof(buf), atom);
    return JS_ThrowTypeError(ctx, fmt, buf);
}

static JSValue JS_ThrowSyntaxErrorAtom(JSContext *ctx, const char *fmt, JSAtom atom)
{
    char buf[ATOM_GET_STR_BUF_SIZE];
    JS_AtomGetStr(ctx, buf, sizeof(buf), atom);
    return JS_ThrowSyntaxError(ctx, fmt, buf);
}
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop // ignored "-Wformat-nonliteral"
#endif // __GNUC__

static int JS_ThrowTypeErrorReadOnly(JSContext *ctx, int flags, JSAtom atom)
{
    if ((flags & JS_PROP_THROW) ||
        ((flags & JS_PROP_THROW_STRICT) && is_strict_mode(ctx))) {
        JS_ThrowTypeErrorAtom(ctx, "'%s' is read-only", atom);
        return -1;
    } else {
        return false;
    }
}

JSValue JS_ThrowOutOfMemory(JSContext *ctx)
{
    JSRuntime *rt = ctx->rt;
    if (!rt->in_out_of_memory) {
        rt->in_out_of_memory = true;
        JS_ThrowInternalError(ctx, "out of memory");
        rt->in_out_of_memory = false;
    }
    return JS_EXCEPTION;
}

static JSValue JS_ThrowStackOverflow(JSContext *ctx)
{
    return JS_ThrowRangeError(ctx, "Maximum call stack size exceeded");
}

static JSValue JS_ThrowTypeErrorNotAConstructor(JSContext *ctx,
                                                JSValueConst func_obj)
{
    JSObject *p;
    JSAtom name;

    if (JS_TAG_OBJECT != JS_VALUE_GET_TAG(func_obj))
        goto fini;
    p = JS_VALUE_GET_OBJ(func_obj);
    if (!js_class_has_bytecode(p->class_id))
        goto fini;
    name = p->u.func.function_bytecode->func_name;
    if (name == JS_ATOM_NULL)
        goto fini;
    return JS_ThrowTypeErrorAtom(ctx, "%s is not a constructor", name);
fini:
    return JS_ThrowTypeError(ctx, "not a constructor");
}

static JSValue JS_ThrowTypeErrorNotAFunction(JSContext *ctx)
{
    return JS_ThrowTypeError(ctx, "not a function");
}

static JSValue JS_ThrowTypeErrorNotAnObject(JSContext *ctx)
{
    return JS_ThrowTypeError(ctx, "not an object");
}

static JSValue JS_ThrowTypeErrorNotASymbol(JSContext *ctx)
{
    return JS_ThrowTypeError(ctx, "not a symbol");
}

static JSValue JS_ThrowReferenceErrorNotDefined(JSContext *ctx, JSAtom name)
{
    char buf[ATOM_GET_STR_BUF_SIZE];
    return JS_ThrowReferenceError(ctx, "%s is not defined",
                                  JS_AtomGetStr(ctx, buf, sizeof(buf), name));
}

static JSValue JS_ThrowReferenceErrorUninitialized(JSContext *ctx, JSAtom name)
{
    char buf[ATOM_GET_STR_BUF_SIZE];
    return JS_ThrowReferenceError(ctx, "%s is not initialized",
                                  name == JS_ATOM_NULL ? "lexical variable" :
                                  JS_AtomGetStr(ctx, buf, sizeof(buf), name));
}

static JSValue JS_ThrowReferenceErrorUninitialized2(JSContext *ctx,
                                                    JSFunctionBytecode *b,
                                                    int idx, bool is_ref)
{
    JSAtom atom = JS_ATOM_NULL;
    if (is_ref) {
        atom = b->closure_var[idx].var_name;
    } else {
        /* not present if the function is stripped and contains no eval() */
        if (b->vardefs)
            atom = b->vardefs[b->arg_count + idx].var_name;
    }
    return JS_ThrowReferenceErrorUninitialized(ctx, atom);
}

static JSValue JS_ThrowTypeErrorInvalidClass(JSContext *ctx, int class_id)
{
    JSRuntime *rt = ctx->rt;
    JSAtom name;
    name = rt->class_array[class_id].class_name;
    return JS_ThrowTypeErrorAtom(ctx, "%s object expected", name);
}

static void JS_ThrowInterrupted(JSContext *ctx)
{
    JS_ThrowInternalError(ctx, "interrupted");
    JS_SetUncatchableError(ctx, ctx->rt->current_exception);
}

static no_inline __exception int __js_poll_interrupts(JSContext *ctx)
{
    JSRuntime *rt = ctx->rt;
    ctx->interrupt_counter = JS_INTERRUPT_COUNTER_INIT;
    if (rt->interrupt_handler) {
        if (rt->interrupt_handler(rt, rt->interrupt_opaque)) {
            JS_ThrowInterrupted(ctx);
            return -1;
        }
    }
    return 0;
}

static inline __exception int js_poll_interrupts(JSContext *ctx)
{
    if (unlikely(--ctx->interrupt_counter <= 0)) {
        return __js_poll_interrupts(ctx);
    } else {
        return 0;
    }
}

/* return -1 (exception) or true/false */
static int JS_SetPrototypeInternal(JSContext *ctx, JSValueConst obj,
                                   JSValueConst proto_val, bool throw_flag)
{
    JSObject *proto, *p, *p1;
    JSShape *sh;

    if (throw_flag) {
        if (JS_VALUE_GET_TAG(obj) == JS_TAG_NULL ||
            JS_VALUE_GET_TAG(obj) == JS_TAG_UNDEFINED)
            goto not_obj;
    } else {
        if (JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT)
            goto not_obj;
    }
    p = JS_VALUE_GET_OBJ(obj);
    if (JS_VALUE_GET_TAG(proto_val) != JS_TAG_OBJECT) {
        if (JS_VALUE_GET_TAG(proto_val) != JS_TAG_NULL) {
        not_obj:
            JS_ThrowTypeErrorNotAnObject(ctx);
            return -1;
        }
        proto = NULL;
    } else {
        proto = JS_VALUE_GET_OBJ(proto_val);
    }

    if (throw_flag && JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT)
        return true;

    if (unlikely(p->class_id == JS_CLASS_PROXY))
        return js_proxy_setPrototypeOf(ctx, obj, proto_val, throw_flag);
    sh = p->shape;
    if (sh->proto == proto)
        return true;
    if (p == JS_VALUE_GET_OBJ(ctx->class_proto[JS_CLASS_OBJECT])) {
        if (throw_flag) {
            JS_ThrowTypeError(ctx, "'Immutable prototype object \'Object.prototype\' cannot have their prototype set'");
            return -1;
        }
        return false;
    }
    if (!p->extensible) {
        if (throw_flag) {
            JS_ThrowTypeError(ctx, "object is not extensible");
            return -1;
        } else {
            return false;
        }
    }
    if (proto) {
        /* check if there is a cycle */
        p1 = proto;
        do {
            if (p1 == p) {
                if (throw_flag) {
                    JS_ThrowTypeError(ctx, "circular prototype chain");
                    return -1;
                } else {
                    return false;
                }
            }
            /* Note: for Proxy objects, proto is NULL */
            p1 = p1->shape->proto;
        } while (p1 != NULL);
        js_dup(proto_val);
    }

    if (js_shape_prepare_update(ctx, p, NULL))
        return -1;
    sh = p->shape;
    if (sh->proto)
        JS_FreeValue(ctx, JS_MKPTR(JS_TAG_OBJECT, sh->proto));
    sh->proto = proto;
    if (proto)
        proto->is_prototype = true;
    if (p->is_prototype) {
        /* track modification of Array.prototype */
        if (unlikely(p == JS_VALUE_GET_OBJ(ctx->class_proto[JS_CLASS_ARRAY]))) {
            ctx->std_array_prototype = false;
        }
    }
    return true;
}

/* return -1 (exception) or true/false */
int JS_SetPrototype(JSContext *ctx, JSValueConst obj, JSValueConst proto_val)
{
    return JS_SetPrototypeInternal(ctx, obj, proto_val, true);
}

/* Only works for primitive types, otherwise return JS_NULL. */
static JSValueConst JS_GetPrototypePrimitive(JSContext *ctx, JSValueConst val)
{
    JSValue ret;
    switch(JS_VALUE_GET_NORM_TAG(val)) {
    case JS_TAG_SHORT_BIG_INT:
    case JS_TAG_BIG_INT:
        ret = ctx->class_proto[JS_CLASS_BIG_INT];
        break;
    case JS_TAG_INT:
    case JS_TAG_FLOAT64:
        ret = ctx->class_proto[JS_CLASS_NUMBER];
        break;
    case JS_TAG_BOOL:
        ret = ctx->class_proto[JS_CLASS_BOOLEAN];
        break;
    case JS_TAG_STRING:
    case JS_TAG_STRING_ROPE:
        ret = ctx->class_proto[JS_CLASS_STRING];
        break;
    case JS_TAG_SYMBOL:
        ret = ctx->class_proto[JS_CLASS_SYMBOL];
        break;
    case JS_TAG_OBJECT:
    case JS_TAG_NULL:
    case JS_TAG_UNDEFINED:
    default:
        ret = JS_NULL;
        break;
    }
    return ret;
}

/* Return an Object, JS_NULL or JS_EXCEPTION in case of Proxy object. */
JSValue JS_GetPrototype(JSContext *ctx, JSValueConst obj)
{
    JSValue val;
    if (JS_VALUE_GET_TAG(obj) == JS_TAG_OBJECT) {
        JSObject *p;
        p = JS_VALUE_GET_OBJ(obj);
        if (unlikely(p->class_id == JS_CLASS_PROXY)) {
            val = js_proxy_getPrototypeOf(ctx, obj);
        } else {
            p = p->shape->proto;
            if (!p)
                val = JS_NULL;
            else
                val = js_dup(JS_MKPTR(JS_TAG_OBJECT, p));
        }
    } else {
        val = js_dup(JS_GetPrototypePrimitive(ctx, obj));
    }
    return val;
}

static JSValue JS_GetPrototypeFree(JSContext *ctx, JSValue obj)
{
    JSValue obj1;
    obj1 = JS_GetPrototype(ctx, obj);
    JS_FreeValue(ctx, obj);
    return obj1;
}

int JS_GetLength(JSContext *ctx, JSValueConst obj, int64_t *pres) {
    return js_get_length64(ctx, pres, obj);
}

int JS_SetLength(JSContext *ctx, JSValueConst obj, int64_t len) {
    return js_set_length64(ctx, obj, len);
}

/* return true, false or (-1) in case of exception */
static int JS_OrdinaryIsInstanceOf(JSContext *ctx, JSValueConst val,
                                   JSValueConst obj)
{
    JSValue obj_proto;
    JSObject *proto;
    const JSObject *p, *proto1;
    int ret;

    if (!JS_IsFunction(ctx, obj))
        return false;
    p = JS_VALUE_GET_OBJ(obj);
    if (p->class_id == JS_CLASS_BOUND_FUNCTION) {
        JSBoundFunction *s = p->u.bound_function;
        return JS_IsInstanceOf(ctx, val, s->func_obj);
    }

    /* Only explicitly boxed values are instances of constructors */
    if (JS_VALUE_GET_TAG(val) != JS_TAG_OBJECT)
        return false;
    obj_proto = JS_GetProperty(ctx, obj, JS_ATOM_prototype);
    if (JS_VALUE_GET_TAG(obj_proto) != JS_TAG_OBJECT) {
        if (!JS_IsException(obj_proto))
            JS_ThrowTypeError(ctx, "operand 'prototype' property is not an object");
        ret = -1;
        goto done;
    }
    proto = JS_VALUE_GET_OBJ(obj_proto);
    p = JS_VALUE_GET_OBJ(val);
    for(;;) {
        proto1 = p->shape->proto;
        if (!proto1) {
            /* slow case if proxy in the prototype chain */
            if (unlikely(p->class_id == JS_CLASS_PROXY)) {
                JSValue obj1;
                obj1 = js_dup(JS_MKPTR(JS_TAG_OBJECT, (JSObject *)p));
                for(;;) {
                    obj1 = JS_GetPrototypeFree(ctx, obj1);
                    if (JS_IsException(obj1)) {
                        ret = -1;
                        break;
                    }
                    if (JS_IsNull(obj1)) {
                        ret = false;
                        break;
                    }
                    if (proto == JS_VALUE_GET_OBJ(obj1)) {
                        JS_FreeValue(ctx, obj1);
                        ret = true;
                        break;
                    }
                    /* must check for timeout to avoid infinite loop */
                    if (js_poll_interrupts(ctx)) {
                        JS_FreeValue(ctx, obj1);
                        ret = -1;
                        break;
                    }
                }
            } else {
                ret = false;
            }
            break;
        }
        p = proto1;
        if (proto == p) {
            ret = true;
            break;
        }
    }
done:
    JS_FreeValue(ctx, obj_proto);
    return ret;
}

/* return true, false or (-1) in case of exception */
int JS_IsInstanceOf(JSContext *ctx, JSValueConst val, JSValueConst obj)
{
    JSValue method;

    if (!JS_IsObject(obj))
        goto fail;
    method = JS_GetProperty(ctx, obj, JS_ATOM_Symbol_hasInstance);
    if (JS_IsException(method))
        return -1;
    if (!JS_IsNull(method) && !JS_IsUndefined(method)) {
        JSValue ret;
        ret = JS_CallFree(ctx, method, obj, 1, &val);
        return JS_ToBoolFree(ctx, ret);
    }

    /* legacy case */
    if (!JS_IsFunction(ctx, obj)) {
    fail:
        JS_ThrowTypeError(ctx, "invalid 'instanceof' right operand");
        return -1;
    }
    return JS_OrdinaryIsInstanceOf(ctx, val, obj);
}

#include "src/generated/builtins/array_fromasync.h"
#include "src/generated/builtins/iterator_zip_keyed.h"
#include "src/generated/builtins/iterator_zip.h"

// like Function.prototype.call but monkey patch-proof
static JSValue js_call_function(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    return JS_Call(ctx, argv[1], argv[0], argc-2, argv+2);
}

// returns enumerable and non-enumerable strings *and* symbols
static JSValue js_getOwnPropertyKeys(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    int flags = JS_GPN_STRING_MASK|JS_GPN_SYMBOL_MASK;
    return JS_GetOwnPropertyNames2(ctx, argv[0], flags, JS_ITERATOR_KIND_KEY);
}

static JSValue js_hasOwnEnumProperty(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    JSObject *p;
    JSAtom key;
    int flags, res;

    if (JS_TAG_OBJECT != JS_VALUE_GET_TAG(argv[0]))
        return JS_ThrowTypeErrorNotAnObject(ctx);
    p = JS_VALUE_GET_OBJ(argv[0]);
    key = JS_ValueToAtomInternal(ctx, argv[1], JS_TO_STRING_NO_SIDE_EFFECTS);
    if (key == JS_ATOM_NULL)
        return JS_EXCEPTION;
    res = JS_GetOwnPropertyFlagsInternal(ctx, &flags, p, key);
    JS_FreeAtom(ctx, key);
    if (res < 0)
        return JS_EXCEPTION;
    if (res > 0 && (flags & JS_PROP_ENUMERABLE))
        return JS_TRUE;
    return JS_FALSE;
}

// note: takes ownership of |argv|
static JSValue js_bytecode_eval(JSContext *ctx, const uint8_t *bytecode,
                                size_t len, int argc, JSValue *argv)
{
    JSValue obj, fun, result;
    int i;

    obj = JS_ReadObject(ctx, bytecode, len, JS_READ_OBJ_BYTECODE);
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    fun = JS_EvalFunction(ctx, obj);
    if (JS_IsException(fun))
        return JS_EXCEPTION;
    assert(JS_IsFunction(ctx, fun));
    result = JS_Call(ctx, fun, JS_UNDEFINED, argc, vc(argv));
    for (i = 0; i < argc; i++)
        JS_FreeValue(ctx, argv[i]);
    JS_FreeValue(ctx, fun);
    if (JS_SetPrototypeInternal(ctx, result, ctx->function_proto,
                                /*throw_flag*/true) < 0) {
        JS_FreeValue(ctx, result);
        return JS_EXCEPTION;
    }
    return result;
}

static JSValue js_bytecode_autoinit(JSContext *ctx, JSObject *p, JSAtom atom,
                                    void *opaque)
{
    switch ((uintptr_t)opaque) {
    default:
        abort();
    case JS_BUILTIN_ARRAY_FROMASYNC:
        {
            JSValue argv[] = {
                JS_NewCFunction(ctx, js_array_constructor, "Array", 0),
                JS_NewCFunctionMagic(ctx, js_error_constructor, "TypeError",
                                     1, JS_CFUNC_constructor_or_func_magic,
                                     JS_TYPE_ERROR),
                JS_AtomToValue(ctx, JS_ATOM_Symbol_asyncIterator),
                JS_NewCFunctionMagic(ctx, js_object_defineProperty,
                                     "Object.defineProperty", 3,
                                     JS_CFUNC_generic_magic, 0),
                JS_AtomToValue(ctx, JS_ATOM_Symbol_iterator),
            };
            return js_bytecode_eval(ctx, qjsc_builtin_array_fromasync,
                                    sizeof(qjsc_builtin_array_fromasync),
                                    countof(argv), argv);
        }
    case JS_BUILTIN_ITERATOR_ZIP:
        {
            JSValue argv[] = {
                js_dup(ctx->class_proto[JS_CLASS_ITERATOR_HELPER]),
                JS_NewCFunctionMagic(ctx, js_error_constructor, "InternalError",
                                     1, JS_CFUNC_constructor_or_func_magic,
                                     JS_INTERNAL_ERROR),
                JS_NewCFunctionMagic(ctx, js_error_constructor, "TypeError",
                                     1, JS_CFUNC_constructor_or_func_magic,
                                     JS_TYPE_ERROR),
                JS_NewCFunction(ctx, js_call_function, "call", 2),
                JS_AtomToValue(ctx, JS_ATOM_Symbol_iterator),
            };
            JSValue result = js_bytecode_eval(ctx, qjsc_builtin_iterator_zip,
                                              sizeof(qjsc_builtin_iterator_zip),
                                              countof(argv), argv);
            JS_SetConstructorBit(ctx, result, false);
            return result;
        }
    case JS_BUILTIN_ITERATOR_ZIP_KEYED:
        {
            JSValue argv[] = {
                js_dup(ctx->class_proto[JS_CLASS_ITERATOR_HELPER]),
                JS_NewCFunctionMagic(ctx, js_error_constructor, "InternalError",
                                     1, JS_CFUNC_constructor_or_func_magic,
                                     JS_INTERNAL_ERROR),
                JS_NewCFunctionMagic(ctx, js_error_constructor, "TypeError",
                                     1, JS_CFUNC_constructor_or_func_magic,
                                     JS_TYPE_ERROR),
                JS_NewCFunction(ctx, js_call_function, "call", 2),
                JS_NewCFunction(ctx, js_hasOwnEnumProperty,
                                "hasOwnEnumProperty", 2),
                JS_NewCFunction(ctx, js_getOwnPropertyKeys,
                                "getOwnPropertyKeys", 1),
                JS_AtomToValue(ctx, JS_ATOM_Symbol_iterator),
            };
            JSValue result = js_bytecode_eval(ctx, qjsc_builtin_iterator_zip_keyed,
                                              sizeof(qjsc_builtin_iterator_zip_keyed),
                                              countof(argv), argv);
            JS_SetConstructorBit(ctx, result, false);
            return result;
        }
    }
    return JS_UNDEFINED;
}

/* return the value associated to the autoinit property or an exception */
typedef JSValue JSAutoInitFunc(JSContext *ctx, JSObject *p, JSAtom atom, void *opaque);

static JSAutoInitFunc *const js_autoinit_func_table[] = {
    js_instantiate_prototype, /* JS_AUTOINIT_ID_PROTOTYPE */
    js_module_ns_autoinit, /* JS_AUTOINIT_ID_MODULE_NS */
    JS_InstantiateFunctionListItem2, /* JS_AUTOINIT_ID_PROP */
    js_bytecode_autoinit, /* JS_AUTOINIT_ID_BYTECODE */
};

/* warning: 'prs' is reallocated after it */
static int JS_AutoInitProperty(JSContext *ctx, JSObject *p, JSAtom prop,
                               JSProperty *pr, JSShapeProperty *prs)
{
    JSValue val;
    JSContext *realm;
    JSAutoInitFunc *func;

    if (js_shape_prepare_update(ctx, p, &prs))
        return -1;

    realm = js_autoinit_get_realm(pr);
    func = js_autoinit_func_table[js_autoinit_get_id(pr)];
    /* 'func' shall not modify the object properties 'pr' */
    val = func(realm, p, prop, pr->u.init.opaque);
    js_autoinit_free(ctx->rt, pr);
    prs->flags &= ~JS_PROP_TMASK;
    pr->u.value = JS_UNDEFINED;
    if (JS_IsException(val))
        return -1;
    pr->u.value = val;
    return 0;
}

