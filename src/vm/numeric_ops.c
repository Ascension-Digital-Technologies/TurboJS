/* Engine domain source: vm/interpreter_async.inc -> numeric_ops.
 * Ownership: vm subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

int JS_ToFloat64(JSContext *ctx, double *pres, JSValueConst val)
{
    return JS_ToFloat64Free(ctx, pres, js_dup(val));
}

JSValue JS_ToNumber(JSContext *ctx, JSValueConst val)
{
    return JS_ToNumberFree(ctx, js_dup(val));
}

/* same as JS_ToNumber() but return 0 in case of NaN/Undefined */
static __maybe_unused JSValue JS_ToIntegerFree(JSContext *ctx, JSValue val)
{
    uint32_t tag;
    JSValue ret;

 redo:
    tag = JS_VALUE_GET_NORM_TAG(val);
    switch(tag) {
    case JS_TAG_INT:
    case JS_TAG_BOOL:
    case JS_TAG_NULL:
    case JS_TAG_UNDEFINED:
        ret = js_int32(JS_VALUE_GET_INT(val));
        break;
    case JS_TAG_FLOAT64:
        {
            double d = JS_VALUE_GET_FLOAT64(val);
            if (isnan(d)) {
                ret = js_int32(0);
            } else {
                /* convert -0 to +0 */
                d = trunc(d) + 0.0;
                ret = js_number(d);
            }
        }
        break;
    default:
        val = JS_ToNumberFree(ctx, val);
        if (JS_IsException(val))
            return val;
        goto redo;
    }
    return ret;
}

/* Note: the integer value is satured to 32 bits */
static int JS_ToInt32SatFree(JSContext *ctx, int *pres, JSValue val)
{
    uint32_t tag;
    int ret;

 redo:
    tag = JS_VALUE_GET_NORM_TAG(val);
    switch(tag) {
    case JS_TAG_INT:
    case JS_TAG_BOOL:
    case JS_TAG_NULL:
    case JS_TAG_UNDEFINED:
        ret = JS_VALUE_GET_INT(val);
        break;
    case JS_TAG_EXCEPTION:
        *pres = 0;
        return -1;
    case JS_TAG_FLOAT64:
        {
            double d = JS_VALUE_GET_FLOAT64(val);
            if (isnan(d)) {
                ret = 0;
            } else {
                if (d < INT32_MIN)
                    ret = INT32_MIN;
                else if (d > INT32_MAX)
                    ret = INT32_MAX;
                else
                    ret = (int)d;
            }
        }
        break;
    default:
        val = JS_ToNumberFree(ctx, val);
        if (JS_IsException(val)) {
            *pres = 0;
            return -1;
        }
        goto redo;
    }
    *pres = ret;
    return 0;
}

static int JS_ToInt32Sat(JSContext *ctx, int *pres, JSValueConst val)
{
    return JS_ToInt32SatFree(ctx, pres, js_dup(val));
}

static int JS_ToInt32Clamp(JSContext *ctx, int *pres, JSValueConst val,
                           int min, int max, int min_offset)
{
    int res = JS_ToInt32SatFree(ctx, pres, js_dup(val));
    if (res == 0) {
        if (*pres < min) {
            *pres += min_offset;
            if (*pres < min)
                *pres = min;
        } else {
            if (*pres > max)
                *pres = max;
        }
    }
    return res;
}

static int JS_ToInt64SatFree(JSContext *ctx, int64_t *pres, JSValue val)
{
    uint32_t tag;

 redo:
    tag = JS_VALUE_GET_NORM_TAG(val);
    switch(tag) {
    case JS_TAG_INT:
    case JS_TAG_BOOL:
    case JS_TAG_NULL:
    case JS_TAG_UNDEFINED:
        *pres = JS_VALUE_GET_INT(val);
        return 0;
    case JS_TAG_EXCEPTION:
        *pres = 0;
        return -1;
    case JS_TAG_FLOAT64:
        {
            double d = JS_VALUE_GET_FLOAT64(val);
            if (isnan(d)) {
                *pres = 0;
            } else {
                if (d < INT64_MIN)
                    *pres = INT64_MIN;
                else if (d >= 0x1p63)
                    *pres = INT64_MAX;
                else
                    *pres = (int64_t)d;
            }
        }
        return 0;
    default:
        val = JS_ToNumberFree(ctx, val);
        if (JS_IsException(val)) {
            *pres = 0;
            return -1;
        }
        goto redo;
    }
}

int JS_ToInt64Sat(JSContext *ctx, int64_t *pres, JSValueConst val)
{
    return JS_ToInt64SatFree(ctx, pres, js_dup(val));
}

int JS_ToInt64Clamp(JSContext *ctx, int64_t *pres, JSValueConst val,
                    int64_t min, int64_t max, int64_t neg_offset)
{
    int res = JS_ToInt64SatFree(ctx, pres, js_dup(val));
    if (res == 0) {
        if (*pres < 0)
            *pres += neg_offset;
        if (*pres < min)
            *pres = min;
        else if (*pres > max)
            *pres = max;
    }
    return res;
}

/* Same as JS_ToInt32Free() but with a 64 bit result. Return (<0, 0)
   in case of exception */
static int JS_ToInt64Free(JSContext *ctx, int64_t *pres, JSValue val)
{
    uint32_t tag;
    int64_t ret;

 redo:
    tag = JS_VALUE_GET_NORM_TAG(val);
    switch(tag) {
    case JS_TAG_INT:
    case JS_TAG_BOOL:
    case JS_TAG_NULL:
    case JS_TAG_UNDEFINED:
        ret = JS_VALUE_GET_INT(val);
        break;
    case JS_TAG_FLOAT64:
        {
            JSFloat64Union u;
            double d;
            int e;
            d = JS_VALUE_GET_FLOAT64(val);
            u.d = d;
            /* we avoid doing fmod(x, 2^64) */
            e = (u.u64 >> 52) & 0x7ff;
            if (likely(e <= (1023 + 62))) {
                /* fast case */
                ret = (int64_t)d;
            } else if (e <= (1023 + 62 + 53)) {
                uint64_t v;
                /* remainder modulo 2^64 */
                v = (u.u64 & (((uint64_t)1 << 52) - 1)) | ((uint64_t)1 << 52);
                ret = v << ((e - 1023) - 52);
                /* take the sign into account */
                if (u.u64 >> 63)
                    if (ret != INT64_MIN)
                        ret = -ret;
            } else {
                ret = 0; /* also handles NaN and +inf */
            }
        }
        break;
    default:
        val = JS_ToNumberFree(ctx, val);
        if (JS_IsException(val)) {
            *pres = 0;
            return -1;
        }
        goto redo;
    }
    *pres = ret;
    return 0;
}

int JS_ToInt64(JSContext *ctx, int64_t *pres, JSValueConst val)
{
    return JS_ToInt64Free(ctx, pres, js_dup(val));
}

int JS_ToInt64Ext(JSContext *ctx, int64_t *pres, JSValueConst val)
{
    if (JS_IsBigInt(val))
        return JS_ToBigInt64(ctx, pres, val);
    else
        return JS_ToInt64(ctx, pres, val);
}

/* return (<0, 0) in case of exception */
static int JS_ToInt32Free(JSContext *ctx, int32_t *pres, JSValue val)
{
    uint32_t tag;
    int32_t ret;

 redo:
    tag = JS_VALUE_GET_NORM_TAG(val);
    switch(tag) {
    case JS_TAG_INT:
    case JS_TAG_BOOL:
    case JS_TAG_NULL:
    case JS_TAG_UNDEFINED:
        ret = JS_VALUE_GET_INT(val);
        break;
    case JS_TAG_FLOAT64:
        {
            JSFloat64Union u;
            double d;
            int e;
            d = JS_VALUE_GET_FLOAT64(val);
            u.d = d;
            /* we avoid doing fmod(x, 2^32) */
            e = (u.u64 >> 52) & 0x7ff;
            if (likely(e <= (1023 + 30))) {
                /* fast case */
                ret = (int32_t)d;
            } else if (e <= (1023 + 30 + 53)) {
                uint64_t v;
                /* remainder modulo 2^32 */
                v = (u.u64 & (((uint64_t)1 << 52) - 1)) | ((uint64_t)1 << 52);
                v = v << ((e - 1023) - 52 + 32);
                ret = v >> 32;
                /* take the sign into account */
                if (u.u64 >> 63)
                    if (ret != INT32_MIN)
                        ret = -ret;
            } else {
                ret = 0; /* also handles NaN and +inf */
            }
        }
        break;
    default:
        val = JS_ToNumberFree(ctx, val);
        if (JS_IsException(val)) {
            *pres = 0;
            return -1;
        }
        goto redo;
    }
    *pres = ret;
    return 0;
}

int JS_ToInt32(JSContext *ctx, int32_t *pres, JSValueConst val)
{
    return JS_ToInt32Free(ctx, pres, js_dup(val));
}

static inline int JS_ToUint32Free(JSContext *ctx, uint32_t *pres, JSValue val)
{
    return JS_ToInt32Free(ctx, (int32_t *)pres, val);
}

static int JS_ToUint8ClampFree(JSContext *ctx, int32_t *pres, JSValue val)
{
    uint32_t tag;
    int res;

 redo:
    tag = JS_VALUE_GET_NORM_TAG(val);
    switch(tag) {
    case JS_TAG_INT:
    case JS_TAG_BOOL:
    case JS_TAG_NULL:
    case JS_TAG_UNDEFINED:
        res = JS_VALUE_GET_INT(val);
        res = max_int(0, min_int(255, res));
        break;
    case JS_TAG_FLOAT64:
        {
            double d = JS_VALUE_GET_FLOAT64(val);
            if (isnan(d)) {
                res = 0;
            } else {
                if (d < 0)
                    res = 0;
                else if (d > 255)
                    res = 255;
                else
                    res = lrint(d);
            }
        }
        break;
    default:
        val = JS_ToNumberFree(ctx, val);
        if (JS_IsException(val)) {
            *pres = 0;
            return -1;
        }
        goto redo;
    }
    *pres = res;
    return 0;
}

static __exception int JS_ToArrayLengthFree(JSContext *ctx, uint32_t *plen,
                                            JSValue val, bool is_array_ctor)
{
    uint32_t tag, len;

    tag = JS_VALUE_GET_TAG(val);
    switch(tag) {
    case JS_TAG_INT:
    case JS_TAG_BOOL:
    case JS_TAG_NULL:
        {
            int v;
            v = JS_VALUE_GET_INT(val);
            if (v < 0)
                goto fail;
            len = v;
        }
        break;
    default:
        if (JS_TAG_IS_FLOAT64(tag)) {
            double d;
            d = JS_VALUE_GET_FLOAT64(val);
            if (!(d >= 0 && d <= UINT32_MAX))
                goto fail;
            len = (uint32_t)d;
            if (len != d)
                goto fail;
        } else {
            uint32_t len1;

            if (is_array_ctor) {
                val = JS_ToNumberFree(ctx, val);
                if (JS_IsException(val))
                    return -1;
                /* cannot recurse because val is a number */
                if (JS_ToArrayLengthFree(ctx, &len, val, true))
                    return -1;
            } else {
                /* legacy behavior: must do the conversion twice and compare */
                if (JS_ToUint32(ctx, &len, val)) {
                    JS_FreeValue(ctx, val);
                    return -1;
                }
                val = JS_ToNumberFree(ctx, val);
                if (JS_IsException(val))
                    return -1;
                /* cannot recurse because val is a number */
                if (JS_ToArrayLengthFree(ctx, &len1, val, false))
                    return -1;
                if (len1 != len) {
                fail:
                    JS_ThrowRangeError(ctx, "invalid array length");
                    return -1;
                }
            }
        }
        break;
    }
    *plen = len;
    return 0;
}

#define MAX_SAFE_INTEGER (((int64_t)1 << 53) - 1)

static bool is_safe_integer(double d)
{
    return isfinite(d) && floor(d) == d &&
        fabs(d) <= (double)MAX_SAFE_INTEGER;
}

int JS_ToIndex(JSContext *ctx, uint64_t *plen, JSValueConst val)
{
    int64_t v;
    if (JS_ToInt64Sat(ctx, &v, val))
        return -1;
    if (v < 0 || v > MAX_SAFE_INTEGER) {
        JS_ThrowRangeError(ctx, "invalid array index");
        *plen = 0;
        return -1;
    }
    *plen = v;
    return 0;
}

/* convert a value to a length between 0 and MAX_SAFE_INTEGER.
   return -1 for exception */
static __exception int JS_ToLengthFree(JSContext *ctx, int64_t *plen,
                                       JSValue val)
{
    int res = JS_ToInt64Clamp(ctx, plen, val, 0, MAX_SAFE_INTEGER, 0);
    JS_FreeValue(ctx, val);
    return res;
}

/* Note: can return an exception */
static int JS_NumberIsInteger(JSContext *ctx, JSValueConst val)
{
    double d;
    if (!JS_IsNumber(val))
        return false;
    if (unlikely(JS_ToFloat64(ctx, &d, val)))
        return -1;
    return isfinite(d) && floor(d) == d;
}

static bool JS_NumberIsNegativeOrMinusZero(JSContext *ctx, JSValueConst val)
{
    uint32_t tag;

    tag = JS_VALUE_GET_NORM_TAG(val);
    switch(tag) {
    case JS_TAG_INT:
        {
            int v;
            v = JS_VALUE_GET_INT(val);
            return (v < 0);
        }
    case JS_TAG_FLOAT64:
        {
            JSFloat64Union u;
            u.d = JS_VALUE_GET_FLOAT64(val);
            return (u.u64 >> 63);
        }
    case JS_TAG_SHORT_BIG_INT:
        return (JS_VALUE_GET_SHORT_BIG_INT(val) < 0);
    case JS_TAG_BIG_INT:
        {
            JSBigInt *p = JS_VALUE_GET_PTR(val);
            return js_bigint_sign(p);
        }
    default:
        return false;
    }
}

static JSValue js_bigint_to_string(JSContext *ctx, JSValueConst val)
{
    return js_bigint_to_string1(ctx, val, 10);
}

/*---- floating point number to string conversions ----*/

static JSValue js_dtoa2(JSContext *ctx,
                        double d, int radix, int n_digits, int flags)
{
    char static_buf[128], *buf, *tmp_buf;
    int len, len_max;
    JSValue res;
    JSDTOATempMem dtoa_mem;
    len_max = js_dtoa_max_len(d, radix, n_digits, flags);

    /* longer buffer may be used if radix != 10 */
    if (len_max > sizeof(static_buf) - 1) {
        tmp_buf = js_malloc(ctx, len_max + 1);
        if (!tmp_buf)
            return JS_EXCEPTION;
        buf = tmp_buf;
    } else {
        tmp_buf = NULL;
        buf = static_buf;
    }
    len = js_dtoa(buf, d, radix, n_digits, flags, &dtoa_mem);
    res = js_new_string8_len(ctx, buf, len);
    js_free(ctx, tmp_buf);
    return res;
}

static JSValue JS_ToStringInternal(JSContext *ctx, JSValueConst val,
                                   int flags)
{
    uint32_t tag;
    char buf[32];
    size_t len;

    tag = JS_VALUE_GET_NORM_TAG(val);
    switch(tag) {
    case JS_TAG_STRING:
        return js_dup(val);
    case JS_TAG_STRING_ROPE:
        return js_linearize_string_rope(ctx, val);
    case JS_TAG_INT:
        len = i32toa(buf, JS_VALUE_GET_INT(val));
        return js_new_string8_len(ctx, buf, len);
    case JS_TAG_BOOL:
        return JS_AtomToString(ctx, JS_VALUE_GET_BOOL(val) ?
                          JS_ATOM_true : JS_ATOM_false);
    case JS_TAG_NULL:
        return JS_AtomToString(ctx, JS_ATOM_null);
    case JS_TAG_UNDEFINED:
        return JS_AtomToString(ctx, JS_ATOM_undefined);
    case JS_TAG_EXCEPTION:
        return JS_EXCEPTION;
    case JS_TAG_OBJECT:
        if (flags & JS_TO_STRING_NO_SIDE_EFFECTS) {
            return js_new_string8(ctx, "{}");
        } else {
            JSValue val1, ret;
            val1 = JS_ToPrimitive(ctx, val, HINT_STRING);
            if (JS_IsException(val1))
                return val1;
            ret = JS_ToStringInternal(ctx, val1, flags);
            JS_FreeValue(ctx, val1);
            return ret;
        }
        break;
    case JS_TAG_FUNCTION_BYTECODE:
        return js_new_string8(ctx, "[function bytecode]");
    case JS_TAG_SYMBOL:
        if (flags & JS_TO_STRING_IS_PROPERTY_KEY) {
            return js_dup(val);
        } else {
            return JS_ThrowTypeError(ctx, "cannot convert symbol to string");
        }
    case JS_TAG_FLOAT64:
        return js_dtoa2(ctx, JS_VALUE_GET_FLOAT64(val), 10, 0,
                        JS_DTOA_FORMAT_FREE);
    case JS_TAG_SHORT_BIG_INT:
    case JS_TAG_BIG_INT:
        return js_bigint_to_string(ctx, val);
    case JS_TAG_UNINITIALIZED:
        return js_new_string8(ctx, "[uninitialized]");
    default:
        return js_new_string8(ctx, "[unsupported type]");
    }
}

JSValue JS_ToString(JSContext *ctx, JSValueConst val)
{
    return JS_ToStringInternal(ctx, val, /*flags*/0);
}

static JSValue JS_ToStringFree(JSContext *ctx, JSValue val)
{
    if (JS_VALUE_GET_TAG(val) == JS_TAG_STRING)
        return val;
    JSValue ret = JS_ToString(ctx, val);
    JS_FreeValue(ctx, val);
    return ret;
}

static JSValue JS_ToLocaleStringFree(JSContext *ctx, JSValue val)
{
    if (JS_IsUndefined(val) || JS_IsNull(val))
        return JS_ToStringFree(ctx, val);
    return JS_InvokeFree(ctx, val, JS_ATOM_toLocaleString, 0, NULL);
}

static JSValue JS_ToPropertyKeyInternal(JSContext *ctx, JSValueConst val,
                                        int flags)
{
    return JS_ToStringInternal(ctx, val, flags | JS_TO_STRING_IS_PROPERTY_KEY);
}

JSValue JS_ToPropertyKey(JSContext *ctx, JSValueConst val)
{
    return JS_ToPropertyKeyInternal(ctx, val, /*flags*/0);
}

static JSValue JS_ToStringCheckObject(JSContext *ctx, JSValueConst val)
{
    uint32_t tag = JS_VALUE_GET_TAG(val);
    if (tag == JS_TAG_NULL || tag == JS_TAG_UNDEFINED)
        return JS_ThrowTypeError(ctx, "null or undefined are forbidden");
    return JS_ToString(ctx, val);
}

static JSValue JS_ToQuotedString(JSContext *ctx, JSValueConst val1)
{
    JSValue val;
    JSString *p;
    int i;
    uint32_t c;
    StringBuffer b_s, *b = &b_s;
    char buf[16];

    val = JS_ToStringCheckObject(ctx, val1);
    if (JS_IsException(val))
        return val;
    p = JS_VALUE_GET_STRING(val);

    if (string_buffer_init(ctx, b, p->len + 2))
        goto fail;

    if (string_buffer_putc8(b, '\"'))
        goto fail;
    for(i = 0; i < p->len; ) {
        c = string_getc(p, &i);
        switch(c) {
        case '\t':
            c = 't';
            goto quote;
        case '\r':
            c = 'r';
            goto quote;
        case '\n':
            c = 'n';
            goto quote;
        case '\b':
            c = 'b';
            goto quote;
        case '\f':
            c = 'f';
            goto quote;
        case '\"':
        case '\\':
        quote:
            if (string_buffer_putc8(b, '\\'))
                goto fail;
            if (string_buffer_putc8(b, c))
                goto fail;
            break;
        default:
            if (c < 32 || is_surrogate(c)) {
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                if (string_buffer_write8(b, (uint8_t*)buf, 6))
                    goto fail;
            } else {
                if (string_buffer_putc(b, c))
                    goto fail;
            }
            break;
        }
    }
    if (string_buffer_putc8(b, '\"'))
        goto fail;
    JS_FreeValue(ctx, val);
    return string_buffer_end(b);
 fail:
    JS_FreeValue(ctx, val);
    string_buffer_free(b);
    return JS_EXCEPTION;
}

static __maybe_unused void JS_DumpObjectHeader(JSRuntime *rt)
{
    printf("%14s %4s %4s %14s %10s %s\n",
           "ADDRESS", "REFS", "SHRF", "PROTO", "CLASS", "PROPS");
}

/* for debug only: dump an object without side effect */
static __maybe_unused void JS_DumpObject(JSRuntime *rt, JSObject *p)
{
    uint32_t i;
    char atom_buf[ATOM_GET_STR_BUF_SIZE];
    JSShape *sh;
    JSShapeProperty *prs;
    JSProperty *pr;
    bool is_first = true;

    /* XXX: should encode atoms with special characters */
    sh = p->shape; /* the shape can be NULL while freeing an object */
    printf("%14p %4d ",
           (void *)p,
           JS_REF_COUNT(p));
    if (sh) {
        printf("%3d%c %14p ",
               JS_REF_COUNT(sh),
               " *"[sh->is_hashed],
               (void *)sh->proto);
    } else {
        printf("%3s  %14s ", "-", "-");
    }
    printf("%10s ",
           JS_AtomGetStrRT(rt, atom_buf, sizeof(atom_buf), rt->class_array[p->class_id].class_name));
    if (p->is_exotic && p->fast_array) {
        printf("[ ");
        for(i = 0; i < p->u.array.count; i++) {
            if (i != 0)
                printf(", ");
            switch (p->class_id) {
            case JS_CLASS_ARRAY:
            case JS_CLASS_ARGUMENTS:
                JS_DumpValue(rt, p->u.array.u.values[i]);
                break;
            case JS_CLASS_UINT8C_ARRAY:
            case JS_CLASS_INT8_ARRAY:
            case JS_CLASS_UINT8_ARRAY:
            case JS_CLASS_INT16_ARRAY:
            case JS_CLASS_UINT16_ARRAY:
            case JS_CLASS_INT32_ARRAY:
            case JS_CLASS_UINT32_ARRAY:
            case JS_CLASS_BIG_INT64_ARRAY:
            case JS_CLASS_BIG_UINT64_ARRAY:
            case JS_CLASS_FLOAT16_ARRAY:
            case JS_CLASS_FLOAT32_ARRAY:
            case JS_CLASS_FLOAT64_ARRAY:
                {
                    int size = 1 << typed_array_size_log2(p->class_id);
                    const uint8_t *b = p->u.array.u.uint8_ptr + i * size;
                    while (size-- > 0)
                        printf("%02X", *b++);
                }
                break;
            }
        }
        printf(" ] ");
    }

    if (sh) {
        printf("{ ");
        for(i = 0, prs = get_shape_prop(sh); i < sh->prop_count; i++, prs++) {
            if (prs->atom != JS_ATOM_NULL) {
                pr = &p->prop[i];
                if (!is_first)
                    printf(", ");
                printf("%s: ",
                       JS_AtomGetStrRT(rt, atom_buf, sizeof(atom_buf), prs->atom));
                if ((prs->flags & JS_PROP_TMASK) == JS_PROP_GETSET) {
                    printf("[getset %p %p]", (void *)pr->u.getset.getter,
                           (void *)pr->u.getset.setter);
                } else if ((prs->flags & JS_PROP_TMASK) == JS_PROP_VARREF) {
                    printf("[varref %p]", (void *)pr->u.var_ref);
                } else if ((prs->flags & JS_PROP_TMASK) == JS_PROP_AUTOINIT) {
                    printf("[autoinit %p %d %p]",
                           (void *)js_autoinit_get_realm(pr),
                           js_autoinit_get_id(pr),
                           (void *)pr->u.init.opaque);
                } else {
                    JS_DumpValue(rt, pr->u.value);
                }
                is_first = false;
            }
        }
        printf(" }");
    }

    if (js_class_has_bytecode(p->class_id)) {
        JSFunctionBytecode *b = p->u.func.function_bytecode;
        JSVarRef **var_refs;
        if (b->closure_var_count) {
            var_refs = p->u.func.var_refs;
            printf(" Closure:");
            for(i = 0; i < b->closure_var_count; i++) {
                printf(" ");
                JS_DumpValue(rt, var_refs[i]->value);
            }
            if (p->u.func.home_object) {
                printf(" HomeObject: ");
                JS_DumpValue(rt, JS_MKPTR(JS_TAG_OBJECT, p->u.func.home_object));
            }
        }
    }
    printf("\n");
}

static __maybe_unused void JS_DumpGCObject(JSRuntime *rt, JSGCObjectHeader *p)
{
    if (JS_GC_TYPE(p) == JS_GC_OBJ_TYPE_JS_OBJECT) {
        JS_DumpObject(rt, (JSObject *)p);
    } else {
        printf("%14p %4d ",
               (void *)p,
               JS_REF_COUNT(p));
        switch(JS_GC_TYPE(p)) {
        case JS_GC_OBJ_TYPE_FUNCTION_BYTECODE:
            printf("[function bytecode]");
            break;
        case JS_GC_OBJ_TYPE_SHAPE:
            printf("[shape]");
            break;
        case JS_GC_OBJ_TYPE_VAR_REF:
            printf("[var_ref]");
            break;
        case JS_GC_OBJ_TYPE_ASYNC_FUNCTION:
            printf("[async_function]");
            break;
        case JS_GC_OBJ_TYPE_JS_CONTEXT:
            printf("[js_context]");
            break;
        default:
            printf("[unknown %d]", JS_GC_TYPE(p));
            break;
        }
        printf("\n");
    }
}

static __maybe_unused void JS_DumpValue(JSRuntime *rt, JSValueConst val)
{
    uint32_t tag = JS_VALUE_GET_NORM_TAG(val);
    const char *str;

    switch(tag) {
    case JS_TAG_INT:
        printf("%d", JS_VALUE_GET_INT(val));
        break;
    case JS_TAG_BOOL:
        if (JS_VALUE_GET_BOOL(val))
            str = "true";
        else
            str = "false";
        goto print_str;
    case JS_TAG_NULL:
        str = "null";
        goto print_str;
    case JS_TAG_EXCEPTION:
        str = "exception";
        goto print_str;
    case JS_TAG_UNINITIALIZED:
        str = "uninitialized";
        goto print_str;
    case JS_TAG_UNDEFINED:
        str = "undefined";
    print_str:
        printf("%s", str);
        break;
    case JS_TAG_FLOAT64:
        printf("%.14g", JS_VALUE_GET_FLOAT64(val));
        break;
    case JS_TAG_SHORT_BIG_INT:
        printf("%" PRId64 "n", (int64_t)JS_VALUE_GET_SHORT_BIG_INT(val));
        break;
    case JS_TAG_BIG_INT:
        {
            JSBigInt *p = JS_VALUE_GET_PTR(val);
            int sgn, i;
            /* In order to avoid allocations we just dump the limbs */
            sgn = js_bigint_sign(p);
            if (sgn)
                printf("BigInt.asIntN(%d,", p->len * JS_LIMB_BITS);
            printf("0x");
            for(i = p->len - 1; i >= 0; i--) {
                if (i != p->len - 1)
                    printf("_");
                printf("%08x", p->tab[i]);
            }
            printf("n");
            if (sgn)
                printf(")");
        }
        break;
    case JS_TAG_STRING:
        {
            JSString *p;
            p = JS_VALUE_GET_STRING(val);
            JS_DumpString(rt, p);
        }
        break;
    case JS_TAG_STRING_ROPE:
        {
            JSStringRope *r = JS_VALUE_GET_STRING_ROPE(val);
            printf("[rope len=%d depth=%d]", r->len, r->depth);
        }
        break;
    case JS_TAG_FUNCTION_BYTECODE:
        {
            JSFunctionBytecode *b = JS_VALUE_GET_PTR(val);
            char buf[ATOM_GET_STR_BUF_SIZE];
            if (b->func_name) {
                printf("[bytecode %s]", JS_AtomGetStrRT(rt, buf, sizeof(buf), b->func_name));
            } else {
                printf("[bytecode (anonymous)]");
            }
        }
        break;
    case JS_TAG_OBJECT:
        {
            JSObject *p = JS_VALUE_GET_OBJ(val);
            JSAtom atom = rt->class_array[p->class_id].class_name;
            char atom_buf[ATOM_GET_STR_BUF_SIZE];
            printf("[%s %p]",
                   JS_AtomGetStrRT(rt, atom_buf, sizeof(atom_buf), atom), (void *)p);
        }
        break;
    case JS_TAG_SYMBOL:
        {
            JSAtomStruct *p = JS_VALUE_GET_PTR(val);
            char atom_buf[ATOM_GET_STR_BUF_SIZE];
            printf("Symbol(%s)",
                   JS_AtomGetStrRT(rt, atom_buf, sizeof(atom_buf), js_get_atom_index(rt, p)));
        }
        break;
    case JS_TAG_MODULE:
        printf("[module]");
        break;
    default:
        printf("[unknown tag %d]", tag);
        break;
    }
}

bool JS_IsArray(JSValueConst val)
{
    if (JS_VALUE_GET_TAG(val) == JS_TAG_OBJECT) {
        JSObject *p = JS_VALUE_GET_OBJ(val);
        return p->class_id == JS_CLASS_ARRAY;
    }
    return false;
}

/* return -1 if exception (proxy case) or true/false */
static int js_is_array(JSContext *ctx, JSValueConst val)
{
    JSObject *p;
    if (JS_VALUE_GET_TAG(val) == JS_TAG_OBJECT) {
        p = JS_VALUE_GET_OBJ(val);
        if (unlikely(p->class_id == JS_CLASS_PROXY))
            return js_proxy_isArray(ctx, val);
        else
            return p->class_id == JS_CLASS_ARRAY;
    } else {
        return false;
    }
}

static double js_math_pow(double a, double b)
{
    double d;

    if (unlikely(!isfinite(b)) && fabs(a) == 1) {
        /* not compatible with IEEE 754 */
        d = NAN;
    } else {
        JS_X87_FPCW_SAVE_AND_ADJUST(fpcw);
        d = pow(a, b);
        JS_X87_FPCW_RESTORE(fpcw);
    }
    return d;
}

JSValue JS_NewBigInt64(JSContext *ctx, int64_t v)
{
    if (v >= JS_SHORT_BIG_INT_MIN && v <= JS_SHORT_BIG_INT_MAX) {
        return __JS_NewShortBigInt(ctx, v);
    } else {
        JSBigInt *p;
        p = js_bigint_new_si64(ctx, v);
        if (!p)
            return JS_EXCEPTION;
        return JS_MKPTR(JS_TAG_BIG_INT, p);
    }
}

JSValue JS_NewBigUint64(JSContext *ctx, uint64_t v)
{
    if (v <= JS_SHORT_BIG_INT_MAX) {
        return __JS_NewShortBigInt(ctx, v);
    } else {
        JSBigInt *p;
        p = js_bigint_new_ui64(ctx, v);
        if (!p)
            return JS_EXCEPTION;
        return JS_MKPTR(JS_TAG_BIG_INT, p);
    }
}

/* return NaN if bad bigint literal */
static JSValue JS_StringToBigInt(JSContext *ctx, JSValue val)
{
    const char *str, *p;
    size_t len;
    int flags;

    str = JS_ToCStringLen(ctx, &len, val);
    JS_FreeValue(ctx, val);
    if (!str)
        return JS_EXCEPTION;
    p = str;
    p += skip_spaces(p);
    if ((p - str) == len) {
        val = JS_NewBigInt64(ctx, 0);
    } else {
        flags = ATOD_INT_ONLY | ATOD_ACCEPT_BIN_OCT | ATOD_TYPE_BIG_INT;
        val = js_atof(ctx, p, &p, 0, flags);
        p += skip_spaces(p);
        if (!JS_IsException(val)) {
            if ((p - str) != len) {
                JS_FreeValue(ctx, val);
                val = JS_NAN;
            }
        }
    }
    JS_FreeCString(ctx, str);
    return val;
}

static JSValue JS_StringToBigIntErr(JSContext *ctx, JSValue val)
{
    val = JS_StringToBigInt(ctx, val);
    if (JS_VALUE_IS_NAN(val))
        return JS_ThrowSyntaxError(ctx, "invalid BigInt literal");
    return val;
}

/* JS Numbers are not allowed */
static JSValue JS_ToBigIntFree(JSContext *ctx, JSValue val)
{
    uint32_t tag;

 redo:
    tag = JS_VALUE_GET_NORM_TAG(val);
    switch(tag) {
    case JS_TAG_SHORT_BIG_INT:
    case JS_TAG_BIG_INT:
        break;
    case JS_TAG_INT:
    case JS_TAG_NULL:
    case JS_TAG_UNDEFINED:
    case JS_TAG_FLOAT64:
        goto fail;
    case JS_TAG_BOOL:
        val = __JS_NewShortBigInt(ctx, JS_VALUE_GET_INT(val));
        break;
    case JS_TAG_STRING:
    case JS_TAG_STRING_ROPE:
        val = JS_StringToBigIntErr(ctx, val);
        if (JS_IsException(val))
            return val;
        goto redo;
    case JS_TAG_OBJECT:
        val = JS_ToPrimitiveFree(ctx, val, HINT_NUMBER);
        if (JS_IsException(val))
            return val;
        goto redo;
    default:
    fail:
        JS_FreeValue(ctx, val);
        return JS_ThrowTypeError(ctx, "cannot convert to bigint");
    }
    return val;
}

static JSValue JS_ToBigInt(JSContext *ctx, JSValueConst val)
{
    return JS_ToBigIntFree(ctx, js_dup(val));
}

/* XXX: merge with JS_ToInt64Free with a specific flag */
static int JS_ToBigInt64Free(JSContext *ctx, int64_t *pres, JSValue val)
{
    uint64_t res;

    val = JS_ToBigIntFree(ctx, val);
    if (JS_IsException(val)) {
        *pres = 0;
        return -1;
    }
    if (JS_VALUE_GET_TAG(val) == JS_TAG_SHORT_BIG_INT) {
        res = JS_VALUE_GET_SHORT_BIG_INT(val);
    } else {
        JSBigInt *p = JS_VALUE_GET_PTR(val);
        /* return the value mod 2^64 */
        res = p->tab[0];
        if (p->len >= 2)
            res |= (uint64_t)p->tab[1] << 32;
        JS_FreeValue(ctx, val);
    }
    *pres = res;
    return 0;
}

int JS_ToBigInt64(JSContext *ctx, int64_t *pres, JSValueConst val)
{
    return JS_ToBigInt64Free(ctx, pres, js_dup(val));
}

int JS_ToBigUint64(JSContext *ctx, uint64_t *pres, JSValueConst val)
{
    return JS_ToBigInt64Free(ctx, (int64_t *)pres, js_dup(val));
}

static no_inline __exception int js_unary_arith_slow(JSContext *ctx,
                                                     JSValue *sp,
                                                     OPCodeEnum op)
{
    JSValue op1;
    int v;
    uint32_t tag;
    JSBigIntBuf buf1;
    JSBigInt *p1;

    op1 = sp[-1];
    /* fast path for float64 */
    if (JS_TAG_IS_FLOAT64(JS_VALUE_GET_TAG(op1)))
        goto handle_float64;
    op1 = JS_ToNumericFree(ctx, op1);
    if (JS_IsException(op1))
        goto exception;
    tag = JS_VALUE_GET_TAG(op1);
    switch(tag) {
    case JS_TAG_INT:
        {
            int64_t v64;
            v64 = JS_VALUE_GET_INT(op1);
            switch(op) {
            case OP_inc:
            case OP_dec:
                v = 2 * (op - OP_dec) - 1;
                v64 += v;
                break;
            case OP_plus:
                break;
            case OP_neg:
                if (v64 == 0) {
                    sp[-1] = js_float64(-0.0);
                    return 0;
                } else {
                    v64 = -v64;
                }
                break;
            default:
                abort();
            }
            sp[-1] = js_int64(v64);
        }
        break;
    case JS_TAG_SHORT_BIG_INT:
        {
            int64_t v;
            v = JS_VALUE_GET_SHORT_BIG_INT(op1);
            switch(op) {
            case OP_plus:
                JS_ThrowTypeError(ctx, "bigint argument with unary +");
                goto exception;
            case OP_inc:
                if (v == JS_SHORT_BIG_INT_MAX)
                    goto bigint_slow_case;
                sp[-1] = __JS_NewShortBigInt(ctx, v + 1);
                break;
            case OP_dec:
                if (v == JS_SHORT_BIG_INT_MIN)
                    goto bigint_slow_case;
                sp[-1] = __JS_NewShortBigInt(ctx, v - 1);
                break;
            case OP_neg:
                v = JS_VALUE_GET_SHORT_BIG_INT(op1);
                if (v == JS_SHORT_BIG_INT_MIN) {
                bigint_slow_case:
                    p1 = js_bigint_set_short(&buf1, op1);
                    goto bigint_slow_case1;
                }
                sp[-1] = __JS_NewShortBigInt(ctx, -v);
                break;
            default:
                abort();
            }
        }
        break;
    case JS_TAG_BIG_INT:
        {
            JSBigInt *r;
            p1 = JS_VALUE_GET_PTR(op1);
        bigint_slow_case1:
            switch(op) {
            case OP_plus:
                JS_ThrowTypeError(ctx, "bigint argument with unary +");
                JS_FreeValue(ctx, op1);
                goto exception;
            case OP_inc:
            case OP_dec:
                {
                    JSBigIntBuf buf2;
                    JSBigInt *p2;
                    p2 = js_bigint_set_si(&buf2, 2 * (op - OP_dec) - 1);
                    r = js_bigint_add(ctx, p1, p2, 0);
                }
                break;
            case OP_neg:
                r = js_bigint_neg(ctx, p1);
                break;
            case OP_not:
                r = js_bigint_not(ctx, p1);
                break;
            default:
                abort();
            }
            JS_FreeValue(ctx, op1);
            if (!r)
                goto exception;
            sp[-1] = JS_CompactBigInt(ctx, r);
        }
        break;
    default:
    handle_float64:
        {
            double d;
            d = JS_VALUE_GET_FLOAT64(op1);
            switch(op) {
            case OP_inc:
            case OP_dec:
                v = 2 * (op - OP_dec) - 1;
                d += v;
                break;
            case OP_plus:
                break;
            case OP_neg:
                d = -d;
                break;
            default:
                abort();
            }
            sp[-1] = js_float64(d);
        }
        break;
    }
    return 0;
 exception:
    sp[-1] = JS_UNDEFINED;
    return -1;
}

static __exception int js_post_inc_slow(JSContext *ctx,
                                        JSValue *sp, OPCodeEnum op)
{
    JSValue op1;

    /* XXX: allow custom operators */
    op1 = sp[-1];
    op1 = JS_ToNumericFree(ctx, op1);
    if (JS_IsException(op1)) {
        sp[-1] = JS_UNDEFINED;
        return -1;
    }
    sp[-1] = op1;
    sp[0] = js_dup(op1);
    return js_unary_arith_slow(ctx, sp + 1, op - OP_post_dec + OP_dec);
}

static no_inline int js_not_slow(JSContext *ctx, JSValue *sp)
{
    JSValue op1;

    op1 = sp[-1];
    op1 = JS_ToNumericFree(ctx, op1);
    if (JS_IsException(op1))
        goto exception;
    if (JS_VALUE_GET_TAG(op1) == JS_TAG_SHORT_BIG_INT) {
        sp[-1] = __JS_NewShortBigInt(ctx, ~JS_VALUE_GET_SHORT_BIG_INT(op1));
    } else if (JS_VALUE_GET_TAG(op1) == JS_TAG_BIG_INT) {
        JSBigInt *r;
        r = js_bigint_not(ctx, JS_VALUE_GET_PTR(op1));
        JS_FreeValue(ctx, op1);
        if (!r)
            goto exception;
        sp[-1] = JS_CompactBigInt(ctx, r);
    } else {
        int32_t v1;
        if (unlikely(JS_ToInt32Free(ctx, &v1, op1)))
            goto exception;
        sp[-1] = js_int32(~v1);
    }
    return 0;
 exception:
    sp[-1] = JS_UNDEFINED;
    return -1;
}

static no_inline __exception int js_binary_arith_slow(JSContext *ctx, JSValue *sp,
                                                      OPCodeEnum op)
{
    JSValue op1, op2;
    uint32_t tag1, tag2;
    double d1, d2;

    op1 = sp[-2];
    op2 = sp[-1];
    tag1 = JS_VALUE_GET_NORM_TAG(op1);
    tag2 = JS_VALUE_GET_NORM_TAG(op2);
    /* fast path for float operations */
    if (tag1 == JS_TAG_FLOAT64 && tag2 == JS_TAG_FLOAT64) {
        d1 = JS_VALUE_GET_FLOAT64(op1);
        d2 = JS_VALUE_GET_FLOAT64(op2);
        goto handle_float64;
    }
    /* fast path for short big int operations */
    if (tag1 == JS_TAG_SHORT_BIG_INT && tag2 == JS_TAG_SHORT_BIG_INT) {
        js_slimb_t v1, v2;
        js_sdlimb_t v;
        v1 = JS_VALUE_GET_SHORT_BIG_INT(op1);
        v2 = JS_VALUE_GET_SHORT_BIG_INT(op2);
        switch(op) {
        case OP_sub:
            v = (js_sdlimb_t)v1 - (js_sdlimb_t)v2;
            break;
        case OP_mul:
            v = (js_sdlimb_t)v1 * (js_sdlimb_t)v2;
            break;
        case OP_div:
            if (v2 == 0 ||
                ((js_limb_t)v1 == (js_limb_t)1 << (JS_LIMB_BITS - 1) &&
                 v2 == -1)) {
                goto slow_big_int;
            }
            sp[-2] = __JS_NewShortBigInt(ctx, v1 / v2);
            return 0;
        case OP_mod:
            if (v2 == 0 ||
                ((js_limb_t)v1 == (js_limb_t)1 << (JS_LIMB_BITS - 1) &&
                 v2 == -1)) {
                goto slow_big_int;
            }
            sp[-2] = __JS_NewShortBigInt(ctx, v1 % v2);
            return 0;
        case OP_pow:
            goto slow_big_int;
        default:
            abort();
        }
        if (likely(v >= JS_SHORT_BIG_INT_MIN && v <= JS_SHORT_BIG_INT_MAX)) {
            sp[-2] = __JS_NewShortBigInt(ctx, v);
        } else {
            JSBigInt *r = js_bigint_new_di(ctx, v);
            if (!r)
                goto exception;
            sp[-2] = JS_MKPTR(JS_TAG_BIG_INT, r);
        }
        return 0;
    }
    op1 = JS_ToNumericFree(ctx, op1);
    if (JS_IsException(op1)) {
        JS_FreeValue(ctx, op2);
        goto exception;
    }
    op2 = JS_ToNumericFree(ctx, op2);
    if (JS_IsException(op2)) {
        JS_FreeValue(ctx, op1);
        goto exception;
    }
    tag1 = JS_VALUE_GET_NORM_TAG(op1);
    tag2 = JS_VALUE_GET_NORM_TAG(op2);

    if (tag1 == JS_TAG_INT && tag2 == JS_TAG_INT) {
        int32_t v1, v2;
        int64_t v;
        v1 = JS_VALUE_GET_INT(op1);
        v2 = JS_VALUE_GET_INT(op2);
        switch(op) {
        case OP_sub:
            v = (int64_t)v1 - (int64_t)v2;
            break;
        case OP_mul:
            v = (int64_t)v1 * (int64_t)v2;
            if (v == 0 && (v1 | v2) < 0) {
                sp[-2] = js_float64(-0.0);
                return 0;
            }
            break;
        case OP_div:
            {
                JS_X87_FPCW_SAVE_AND_ADJUST(fpcw);
                sp[-2] = js_number((double)v1 / (double)v2);
                JS_X87_FPCW_RESTORE(fpcw);
            }
            return 0;
        case OP_mod:
            if (v1 < 0 || v2 <= 0) {
                JS_X87_FPCW_SAVE_AND_ADJUST(fpcw);
                sp[-2] = js_number(fmod(v1, v2));
                JS_X87_FPCW_RESTORE(fpcw);
                return 0;
            } else {
                v = (int64_t)v1 % (int64_t)v2;
            }
            break;
        case OP_pow:
            sp[-2] = js_number(js_math_pow(v1, v2));
            return 0;
        default:
            abort();
        }
        sp[-2] = js_int64(v);
    } else if ((tag1 == JS_TAG_SHORT_BIG_INT || tag1 == JS_TAG_BIG_INT) &&
               (tag2 == JS_TAG_SHORT_BIG_INT || tag2 == JS_TAG_BIG_INT)) {
        JSBigInt *p1, *p2, *r;
        JSBigIntBuf buf1, buf2;
    slow_big_int:
        /* bigint result */
        if (JS_VALUE_GET_TAG(op1) == JS_TAG_SHORT_BIG_INT)
            p1 = js_bigint_set_short(&buf1, op1);
        else
            p1 = JS_VALUE_GET_PTR(op1);
        if (JS_VALUE_GET_TAG(op2) == JS_TAG_SHORT_BIG_INT)
            p2 = js_bigint_set_short(&buf2, op2);
        else
            p2 = JS_VALUE_GET_PTR(op2);
        switch(op) {
        case OP_add:
            r = js_bigint_add(ctx, p1, p2, 0);
            break;
        case OP_sub:
            r = js_bigint_add(ctx, p1, p2, 1);
            break;
        case OP_mul:
            r = js_bigint_mul(ctx, p1, p2);
            break;
        case OP_div:
            r = js_bigint_divrem(ctx, p1, p2, false);
            break;
        case OP_mod:
            r = js_bigint_divrem(ctx, p1, p2, true);
            break;
        case OP_pow:
            r = js_bigint_pow(ctx, p1, p2);
            break;
        default:
            abort();
        }
        JS_FreeValue(ctx, op1);
        JS_FreeValue(ctx, op2);
        if (!r)
            goto exception;
        sp[-2] = JS_CompactBigInt(ctx, r);
    } else {
        double dr;
        /* float64 result */
        if (JS_ToFloat64Free(ctx, &d1, op1)) {
            JS_FreeValue(ctx, op2);
            goto exception;
        }
        if (JS_ToFloat64Free(ctx, &d2, op2))
            goto exception;
    handle_float64:
        JS_X87_FPCW_SAVE_AND_ADJUST(fpcw);
        switch(op) {
        case OP_sub:
            dr = d1 - d2;
            break;
        case OP_mul:
            dr = d1 * d2;
            break;
        case OP_div:
            dr = d1 / d2;
            break;
        case OP_mod:
            dr = fmod(d1, d2);
            break;
        case OP_pow:
            dr = js_math_pow(d1, d2);
            break;
        default:
            abort();
        }
        JS_X87_FPCW_RESTORE(fpcw);
        sp[-2] = js_float64(dr);
    }
    return 0;
 exception:
    sp[-2] = JS_UNDEFINED;
    sp[-1] = JS_UNDEFINED;
    return -1;
}

static no_inline __exception int js_add_slow(JSContext *ctx, JSValue *sp)
{
    JSValue op1, op2;
    uint32_t tag1, tag2;

    op1 = sp[-2];
    op2 = sp[-1];

    tag1 = JS_VALUE_GET_NORM_TAG(op1);
    tag2 = JS_VALUE_GET_NORM_TAG(op2);
    /* fast path for float64 */
    if (tag1 == JS_TAG_FLOAT64 && tag2 == JS_TAG_FLOAT64) {
        double d1, d2;
        d1 = JS_VALUE_GET_FLOAT64(op1);
        d2 = JS_VALUE_GET_FLOAT64(op2);
        sp[-2] = js_float64(d1 + d2);
        return 0;
    }
    /* fast path for short bigint */
    if (tag1 == JS_TAG_SHORT_BIG_INT && tag2 == JS_TAG_SHORT_BIG_INT) {
        js_slimb_t v1, v2;
        js_sdlimb_t v;
        v1 = JS_VALUE_GET_SHORT_BIG_INT(op1);
        v2 = JS_VALUE_GET_SHORT_BIG_INT(op2);
        v = (js_sdlimb_t)v1 + (js_sdlimb_t)v2;
        if (likely(v >= JS_SHORT_BIG_INT_MIN && v <= JS_SHORT_BIG_INT_MAX)) {
            sp[-2] = __JS_NewShortBigInt(ctx, v);
        } else {
            JSBigInt *r = js_bigint_new_di(ctx, v);
            if (!r)
                goto exception;
            sp[-2] = JS_MKPTR(JS_TAG_BIG_INT, r);
        }
        return 0;
    }

    if (tag1 == JS_TAG_OBJECT || tag2 == JS_TAG_OBJECT) {
        op1 = JS_ToPrimitiveFree(ctx, op1, HINT_NONE);
        if (JS_IsException(op1)) {
            JS_FreeValue(ctx, op2);
            goto exception;
        }

        op2 = JS_ToPrimitiveFree(ctx, op2, HINT_NONE);
        if (JS_IsException(op2)) {
            JS_FreeValue(ctx, op1);
            goto exception;
        }
        tag1 = JS_VALUE_GET_NORM_TAG(op1);
        tag2 = JS_VALUE_GET_NORM_TAG(op2);
    }

    if (tag_is_string(tag1) || tag_is_string(tag2)) {
        sp[-2] = JS_ConcatString(ctx, op1, op2);
        if (JS_IsException(sp[-2]))
            goto exception;
        return 0;
    }

    op1 = JS_ToNumericFree(ctx, op1);
    if (JS_IsException(op1)) {
        JS_FreeValue(ctx, op2);
        goto exception;
    }
    op2 = JS_ToNumericFree(ctx, op2);
    if (JS_IsException(op2)) {
        JS_FreeValue(ctx, op1);
        goto exception;
    }
    tag1 = JS_VALUE_GET_NORM_TAG(op1);
    tag2 = JS_VALUE_GET_NORM_TAG(op2);

    if (tag1 == JS_TAG_INT && tag2 == JS_TAG_INT) {
        int32_t v1, v2;
        int64_t v;
        v1 = JS_VALUE_GET_INT(op1);
        v2 = JS_VALUE_GET_INT(op2);
        v = (int64_t)v1 + (int64_t)v2;
        sp[-2] = js_int64(v);
    } else if ((tag1 == JS_TAG_BIG_INT || tag1 == JS_TAG_SHORT_BIG_INT) &&
               (tag2 == JS_TAG_BIG_INT || tag2 == JS_TAG_SHORT_BIG_INT)) {
        JSBigInt *p1, *p2, *r;
        JSBigIntBuf buf1, buf2;
        /* bigint result */
        if (JS_VALUE_GET_TAG(op1) == JS_TAG_SHORT_BIG_INT)
            p1 = js_bigint_set_short(&buf1, op1);
        else
            p1 = JS_VALUE_GET_PTR(op1);
        if (JS_VALUE_GET_TAG(op2) == JS_TAG_SHORT_BIG_INT)
            p2 = js_bigint_set_short(&buf2, op2);
        else
            p2 = JS_VALUE_GET_PTR(op2);
        r = js_bigint_add(ctx, p1, p2, 0);
        JS_FreeValue(ctx, op1);
        JS_FreeValue(ctx, op2);
        if (!r)
            goto exception;
        sp[-2] = JS_CompactBigInt(ctx, r);
    } else {
        double d1, d2;
        /* float64 result */
        if (JS_ToFloat64Free(ctx, &d1, op1)) {
            JS_FreeValue(ctx, op2);
            goto exception;
        }
        if (JS_ToFloat64Free(ctx, &d2, op2))
            goto exception;
        JS_X87_FPCW_SAVE_AND_ADJUST(fpcw);
        sp[-2] = js_float64(d1 + d2);
        JS_X87_FPCW_RESTORE(fpcw);
    }
    return 0;
 exception:
    sp[-2] = JS_UNDEFINED;
    sp[-1] = JS_UNDEFINED;
    return -1;
}

static no_inline __exception int js_binary_logic_slow(JSContext *ctx,
                                                      JSValue *sp,
                                                      OPCodeEnum op)
{
    JSValue op1, op2;
    uint32_t tag1, tag2;
    uint32_t v1, v2, r;

    op1 = sp[-2];
    op2 = sp[-1];
    tag1 = JS_VALUE_GET_NORM_TAG(op1);
    tag2 = JS_VALUE_GET_NORM_TAG(op2);

    if (tag1 == JS_TAG_SHORT_BIG_INT && tag2 == JS_TAG_SHORT_BIG_INT) {
        js_slimb_t v1, v2, v;
        js_sdlimb_t vd;
        v1 = JS_VALUE_GET_SHORT_BIG_INT(op1);
        v2 = JS_VALUE_GET_SHORT_BIG_INT(op2);
        /* bigint fast path */
        switch(op) {
        case OP_and:
            v = v1 & v2;
            break;
        case OP_or:
            v = v1 | v2;
            break;
        case OP_xor:
            v = v1 ^ v2;
            break;
        case OP_sar:
            if (v2 > (JS_LIMB_BITS - 1)) {
                goto slow_big_int;
            } else if (v2 < 0) {
                if (v2 < -(JS_LIMB_BITS - 1))
                    goto slow_big_int;
                v2 = -v2;
                goto bigint_shl;
            }
        bigint_sar:
            v = v1 >> v2;
            break;
        case OP_shl:
            if (v2 > (JS_LIMB_BITS - 1)) {
                goto slow_big_int;
            } else if (v2 < 0) {
                if (v2 < -(JS_LIMB_BITS - 1))
                    goto slow_big_int;
                v2 = -v2;
                goto bigint_sar;
            }
        bigint_shl:
            vd = (js_dlimb_t)v1 << v2;
            if (likely(vd >= JS_SHORT_BIG_INT_MIN &&
                       vd <= JS_SHORT_BIG_INT_MAX)) {
                v = vd;
            } else {
                JSBigInt *r = js_bigint_new_di(ctx, vd);
                if (!r)
                    goto exception;
                sp[-2] = JS_MKPTR(JS_TAG_BIG_INT, r);
                return 0;
            }
            break;
        default:
            abort();
        }
        sp[-2] = __JS_NewShortBigInt(ctx, v);
        return 0;
    }
    op1 = JS_ToNumericFree(ctx, op1);
    if (JS_IsException(op1)) {
        JS_FreeValue(ctx, op2);
        goto exception;
    }
    op2 = JS_ToNumericFree(ctx, op2);
    if (JS_IsException(op2)) {
        JS_FreeValue(ctx, op1);
        goto exception;
    }

    tag1 = JS_VALUE_GET_TAG(op1);
    tag2 = JS_VALUE_GET_TAG(op2);
    if ((tag1 == JS_TAG_BIG_INT || tag1 == JS_TAG_SHORT_BIG_INT) &&
        (tag2 == JS_TAG_BIG_INT || tag2 == JS_TAG_SHORT_BIG_INT)) {
        JSBigInt *p1, *p2, *r;
        JSBigIntBuf buf1, buf2;
    slow_big_int:
        /* buf2 is zero-initialized here (after the label, so it also runs on
           the goto slow_big_int paths) to silence a -Wmaybe-uninitialized
           false positive: GCC cannot prove buf2 is initialized through the
           inlined js_bigint_get_si_sat() -> js_bigint_sign() read below. */
        memset(&buf2, 0, sizeof(buf2));
        if (JS_VALUE_GET_TAG(op1) == JS_TAG_SHORT_BIG_INT)
            p1 = js_bigint_set_short(&buf1, op1);
        else
            p1 = JS_VALUE_GET_PTR(op1);
        if (JS_VALUE_GET_TAG(op2) == JS_TAG_SHORT_BIG_INT)
            p2 = js_bigint_set_short(&buf2, op2);
        else
            p2 = JS_VALUE_GET_PTR(op2);
        switch(op) {
        case OP_and:
        case OP_or:
        case OP_xor:
            r = js_bigint_logic(ctx, p1, p2, op);
            break;
        case OP_shl:
        case OP_sar:
            {
                js_slimb_t shift;
                shift = js_bigint_get_si_sat(p2);
                if (shift > INT32_MAX)
                    shift = INT32_MAX;
                else if (shift < -INT32_MAX)
                    shift = -INT32_MAX;
                if (op == OP_sar)
                    shift = -shift;
                if (shift >= 0)
                    r = js_bigint_shl(ctx, p1, shift);
                else
                    r = js_bigint_shr(ctx, p1, -shift);
            }
            break;
        default:
            abort();
        }
        JS_FreeValue(ctx, op1);
        JS_FreeValue(ctx, op2);
        if (!r)
            goto exception;
        sp[-2] = JS_CompactBigInt(ctx, r);
    } else {
        if (unlikely(JS_ToInt32Free(ctx, (int32_t *)&v1, op1))) {
            JS_FreeValue(ctx, op2);
            goto exception;
        }
        if (unlikely(JS_ToInt32Free(ctx, (int32_t *)&v2, op2)))
            goto exception;
        switch(op) {
        case OP_shl:
            r = v1 << (v2 & 0x1f);
            break;
        case OP_sar:
            r = (int)v1 >> (v2 & 0x1f);
            break;
        case OP_and:
            r = v1 & v2;
            break;
        case OP_or:
            r = v1 | v2;
            break;
        case OP_xor:
            r = v1 ^ v2;
            break;
        default:
            abort();
        }
        sp[-2] = js_int32(r);
    }
    return 0;
 exception:
    sp[-2] = JS_UNDEFINED;
    sp[-1] = JS_UNDEFINED;
    return -1;
}

/* op1 must be a bigint or int. */
static JSBigInt *JS_ToBigIntBuf(JSContext *ctx, JSBigIntBuf *buf1,
                                JSValue op1)
{
    JSBigInt *p1;

    switch(JS_VALUE_GET_TAG(op1)) {
    case JS_TAG_INT:
        p1 = js_bigint_set_si(buf1, JS_VALUE_GET_INT(op1));
        break;
    case JS_TAG_SHORT_BIG_INT:
        p1 = js_bigint_set_short(buf1, op1);
        break;
    case JS_TAG_BIG_INT:
        p1 = JS_VALUE_GET_PTR(op1);
        break;
    default:
        abort();
    }
    return p1;
}

/* op1 and op2 must be numeric types and at least one must be a
   bigint. No exception is generated. */
static int js_compare_bigint(JSContext *ctx, OPCodeEnum op,
                             JSValue op1, JSValue op2)
{
    int res, val, tag1, tag2;
    JSBigIntBuf buf1, buf2;
    JSBigInt *p1, *p2;

    tag1 = JS_VALUE_GET_NORM_TAG(op1);
    tag2 = JS_VALUE_GET_NORM_TAG(op2);
    if ((tag1 == JS_TAG_SHORT_BIG_INT || tag1 == JS_TAG_INT) &&
        (tag2 == JS_TAG_SHORT_BIG_INT || tag2 == JS_TAG_INT)) {
        /* fast path */
        js_slimb_t v1, v2;
        if (tag1 == JS_TAG_INT)
            v1 = JS_VALUE_GET_INT(op1);
        else
            v1 = JS_VALUE_GET_SHORT_BIG_INT(op1);
        if (tag2 == JS_TAG_INT)
            v2 = JS_VALUE_GET_INT(op2);
        else
            v2 = JS_VALUE_GET_SHORT_BIG_INT(op2);
        val = (v1 > v2) - (v1 < v2);
    } else {
        if (tag1 == JS_TAG_FLOAT64) {
            p2 = JS_ToBigIntBuf(ctx, &buf2, op2);
            val = js_bigint_float64_cmp(ctx, p2, JS_VALUE_GET_FLOAT64(op1));
            if (val == 2)
                goto unordered;
            val = -val;
        } else if (tag2 == JS_TAG_FLOAT64) {
            p1 = JS_ToBigIntBuf(ctx, &buf1, op1);
            val = js_bigint_float64_cmp(ctx, p1, JS_VALUE_GET_FLOAT64(op2));
            if (val == 2) {
            unordered:
                JS_FreeValue(ctx, op1);
                JS_FreeValue(ctx, op2);
                return false;
            }
        } else {
            p1 = JS_ToBigIntBuf(ctx, &buf1, op1);
            p2 = JS_ToBigIntBuf(ctx, &buf2, op2);
            val = js_bigint_cmp(ctx, p1, p2);
        }
        JS_FreeValue(ctx, op1);
        JS_FreeValue(ctx, op2);
    }

    switch(op) {
    case OP_lt:
        res = val < 0;
        break;
    case OP_lte:
        res = val <= 0;
        break;
    case OP_gt:
        res = val > 0;
        break;
    case OP_gte:
        res = val >= 0;
        break;
    case OP_eq:
        res = val == 0;
        break;
    default:
        abort();
    }
    return res;
}

static no_inline int js_relational_slow(JSContext *ctx, JSValue *sp,
                                        OPCodeEnum op)
{
    JSValue op1, op2;
    int res;
    uint32_t tag1, tag2;

    op1 = sp[-2];
    op2 = sp[-1];
    tag1 = JS_VALUE_GET_NORM_TAG(op1);
    tag2 = JS_VALUE_GET_NORM_TAG(op2);

    op1 = JS_ToPrimitiveFree(ctx, op1, HINT_NUMBER);
    if (JS_IsException(op1)) {
        JS_FreeValue(ctx, op2);
        goto exception;
    }
    op2 = JS_ToPrimitiveFree(ctx, op2, HINT_NUMBER);
    if (JS_IsException(op2)) {
        JS_FreeValue(ctx, op1);
        goto exception;
    }
    tag1 = JS_VALUE_GET_NORM_TAG(op1);
    tag2 = JS_VALUE_GET_NORM_TAG(op2);

    if (tag_is_string(tag1) && tag_is_string(tag2)) {
        if (tag1 == JS_TAG_STRING && tag2 == JS_TAG_STRING) {
            res = js_string_compare(JS_VALUE_GET_STRING(op1),
                                    JS_VALUE_GET_STRING(op2));
        } else {
            res = js_string_rope_compare(op1, op2, false);
        }
        switch(op) {
        case OP_lt:
            res = (res < 0);
            break;
        case OP_lte:
            res = (res <= 0);
            break;
        case OP_gt:
            res = (res > 0);
            break;
        default:
        case OP_gte:
            res = (res >= 0);
            break;
        }
        JS_FreeValue(ctx, op1);
        JS_FreeValue(ctx, op2);
    } else if ((tag1 <= JS_TAG_NULL || tag1 == JS_TAG_FLOAT64) &&
               (tag2 <= JS_TAG_NULL || tag2 == JS_TAG_FLOAT64)) {
        /* fast path for float64/int */
        goto float64_compare;
    } else {
        if ((((tag1 == JS_TAG_BIG_INT || tag1 == JS_TAG_SHORT_BIG_INT) &&
              tag2 == JS_TAG_STRING) ||
             ((tag2 == JS_TAG_BIG_INT || tag2 == JS_TAG_SHORT_BIG_INT) &&
              tag1 == JS_TAG_STRING))) {
            if (tag1 == JS_TAG_STRING) {
                op1 = JS_StringToBigInt(ctx, op1);
                if (JS_VALUE_GET_TAG(op1) != JS_TAG_BIG_INT &&
                    JS_VALUE_GET_TAG(op1) != JS_TAG_SHORT_BIG_INT)
                    goto invalid_bigint_string;
            }
            if (tag2 == JS_TAG_STRING) {
                op2 = JS_StringToBigInt(ctx, op2);
                if (JS_VALUE_GET_TAG(op2) != JS_TAG_BIG_INT &&
                    JS_VALUE_GET_TAG(op2) != JS_TAG_SHORT_BIG_INT) {
                invalid_bigint_string:
                    JS_FreeValue(ctx, op1);
                    JS_FreeValue(ctx, op2);
                    res = false;
                    goto done;
                }
            }
        } else {
            op1 = JS_ToNumericFree(ctx, op1);
            if (JS_IsException(op1)) {
                JS_FreeValue(ctx, op2);
                goto exception;
            }
            op2 = JS_ToNumericFree(ctx, op2);
            if (JS_IsException(op2)) {
                JS_FreeValue(ctx, op1);
                goto exception;
            }
        }

        tag1 = JS_VALUE_GET_NORM_TAG(op1);
        tag2 = JS_VALUE_GET_NORM_TAG(op2);

        if (tag1 == JS_TAG_BIG_INT || tag1 == JS_TAG_SHORT_BIG_INT ||
            tag2 == JS_TAG_BIG_INT || tag2 == JS_TAG_SHORT_BIG_INT) {
            res = js_compare_bigint(ctx, op, op1, op2);
        } else {
            double d1, d2;

        float64_compare:
            /* can use floating point comparison */
            if (tag1 == JS_TAG_FLOAT64) {
                d1 = JS_VALUE_GET_FLOAT64(op1);
            } else {
                d1 = JS_VALUE_GET_INT(op1);
            }
            if (tag2 == JS_TAG_FLOAT64) {
                d2 = JS_VALUE_GET_FLOAT64(op2);
            } else {
                d2 = JS_VALUE_GET_INT(op2);
            }
            switch(op) {
            case OP_lt:
                res = (d1 < d2); /* if NaN return false */
                break;
            case OP_lte:
                res = (d1 <= d2); /* if NaN return false */
                break;
            case OP_gt:
                res = (d1 > d2); /* if NaN return false */
                break;
            default:
            case OP_gte:
                res = (d1 >= d2); /* if NaN return false */
                break;
            }
        }
    }
 done:
    sp[-2] = js_bool(res);
    return 0;
 exception:
    sp[-2] = JS_UNDEFINED;
    sp[-1] = JS_UNDEFINED;
    return -1;
}

