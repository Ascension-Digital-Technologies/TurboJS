/* Engine domain source: builtins/core_builtins.inc -> string_builtin.
 * Ownership: builtins subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static JSValue js_thisNumberValue(JSContext *ctx, JSValueConst this_val)
{
    if (JS_IsNumber(this_val))
        return js_dup(this_val);

    if (JS_VALUE_GET_TAG(this_val) == JS_TAG_OBJECT) {
        JSObject *p = JS_VALUE_GET_OBJ(this_val);
        if (p->class_id == JS_CLASS_NUMBER) {
            if (JS_IsNumber(p->u.object_data))
                return js_dup(p->u.object_data);
        }
    }
    return JS_ThrowTypeError(ctx, "not a number");
}

static JSValue js_number_valueOf(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    return js_thisNumberValue(ctx, this_val);
}

static int js_get_radix(JSContext *ctx, JSValueConst val)
{
    int radix;
    if (JS_ToInt32Sat(ctx, &radix, val))
        return -1;
    if (radix < 2 || radix > 36) {
        JS_ThrowRangeError(ctx, "toString() radix argument must be between 2 and 36");
        return -1;
    }
    return radix;
}

static JSValue js_number_toString(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv, int magic)
{
    JSValue val;
    int base, flags;
    double d;

    val = js_thisNumberValue(ctx, this_val);
    if (JS_IsException(val))
        return val;
    if (magic || JS_IsUndefined(argv[0])) {
        base = 10;
    } else {
        base = js_get_radix(ctx, argv[0]);
        if (base < 0)
            goto fail;
    }
    if (JS_VALUE_GET_TAG(val) == JS_TAG_INT) {
        char buf1[70];
        int len;
        len = i64toa_radix(buf1, JS_VALUE_GET_INT(val), base);
        return js_new_string8_len(ctx, buf1, len);
    }
    if (JS_ToFloat64Free(ctx, &d, val))
        return JS_EXCEPTION;
    flags = JS_DTOA_FORMAT_FREE;
    if (base != 10)
        flags |= JS_DTOA_EXP_DISABLED;
    return js_dtoa2(ctx, d, base, 0, flags);
 fail:
    JS_FreeValue(ctx, val);
    return JS_EXCEPTION;
}

static JSValue js_number_toFixed(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    JSValue val;
    int f, flags;
    double d;

    val = js_thisNumberValue(ctx, this_val);
    if (JS_IsException(val))
        return val;
    if (JS_ToFloat64Free(ctx, &d, val))
        return JS_EXCEPTION;
    if (JS_ToInt32Sat(ctx, &f, argv[0]))
        return JS_EXCEPTION;
    if (f < 0 || f > 100)
        return JS_ThrowRangeError(ctx, "invalid number of digits");
    if (fabs(d) >= 1e21)
        flags = JS_DTOA_FORMAT_FREE;
    else
        flags = JS_DTOA_FORMAT_FRAC;
    return js_dtoa2(ctx, d, 10, f, flags);
}

static JSValue js_number_toExponential(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    JSValue val;
    int f, flags;
    double d;

    val = js_thisNumberValue(ctx, this_val);
    if (JS_IsException(val))
        return val;
    if (JS_ToFloat64Free(ctx, &d, val))
        return JS_EXCEPTION;
    if (JS_ToInt32Sat(ctx, &f, argv[0]))
        return JS_EXCEPTION;
    if (!isfinite(d)) {
        return JS_ToStringFree(ctx,  __JS_NewFloat64(d));
    }
    if (JS_IsUndefined(argv[0])) {
        flags = JS_DTOA_FORMAT_FREE;
        f = 0;
    } else {
        if (f < 0 || f > 100)
            return JS_ThrowRangeError(ctx, "invalid number of digits");
        f++;
        flags = JS_DTOA_FORMAT_FIXED;
    }
    return js_dtoa2(ctx, d, 10, f, flags | JS_DTOA_EXP_ENABLED);
}

static JSValue js_number_toPrecision(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    JSValue val;
    int p;
    double d;

    val = js_thisNumberValue(ctx, this_val);
    if (JS_IsException(val))
        return val;
    if (JS_ToFloat64Free(ctx, &d, val))
        return JS_EXCEPTION;
    if (JS_IsUndefined(argv[0]))
        goto to_string;
    if (JS_ToInt32Sat(ctx, &p, argv[0]))
        return JS_EXCEPTION;
    if (!isfinite(d)) {
    to_string:
        return JS_ToStringFree(ctx,  __JS_NewFloat64(d));
    }
    if (p < 1 || p > 100)
        return JS_ThrowRangeError(ctx, "invalid number of digits");
    return js_dtoa2(ctx, d, 10, p, JS_DTOA_FORMAT_FIXED);
}

static const JSCFunctionListEntry js_number_proto_funcs[] = {
    JS_CFUNC_DEF("toExponential", 1, js_number_toExponential ),
    JS_CFUNC_DEF("toFixed", 1, js_number_toFixed ),
    JS_CFUNC_DEF("toPrecision", 1, js_number_toPrecision ),
    JS_CFUNC_MAGIC_DEF("toString", 1, js_number_toString, 0 ),
    JS_CFUNC_MAGIC_DEF("toLocaleString", 0, js_number_toString, 1 ),
    JS_CFUNC_DEF("valueOf", 0, js_number_valueOf ),
};

static JSValue js_parseInt(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    const char *str, *p;
    int radix, flags;
    JSValue ret;

    str = JS_ToCString(ctx, argv[0]);
    if (!str)
        return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &radix, argv[1])) {
        JS_FreeCString(ctx, str);
        return JS_EXCEPTION;
    }
    if (radix != 0 && (radix < 2 || radix > 36)) {
        ret = JS_NAN;
    } else {
        p = str;
        p += skip_spaces(p);
        flags = ATOD_INT_ONLY | ATOD_ACCEPT_PREFIX_AFTER_SIGN;
        ret = js_atof(ctx, p, NULL, radix, flags);
    }
    JS_FreeCString(ctx, str);
    return ret;
}

static JSValue js_parseFloat(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    const char *str, *p;
    JSValue ret;

    str = JS_ToCString(ctx, argv[0]);
    if (!str)
        return JS_EXCEPTION;
    p = str;
    p += skip_spaces(p);
    ret = js_atof(ctx, p, NULL, 10, 0);
    JS_FreeCString(ctx, str);
    return ret;
}

/* Boolean */
static JSValue js_boolean_constructor(JSContext *ctx, JSValueConst new_target,
                                     int argc, JSValueConst *argv)
{
    JSValue val, obj;
    val = js_bool(JS_ToBool(ctx, argv[0]));
    if (!JS_IsUndefined(new_target)) {
        obj = js_create_from_ctor(ctx, new_target, JS_CLASS_BOOLEAN);
        if (!JS_IsException(obj))
            JS_SetObjectData(ctx, obj, val);
        return obj;
    } else {
        return val;
    }
}

static JSValue js_thisBooleanValue(JSContext *ctx, JSValueConst this_val)
{
    if (JS_VALUE_GET_TAG(this_val) == JS_TAG_BOOL)
        return js_dup(this_val);

    if (JS_VALUE_GET_TAG(this_val) == JS_TAG_OBJECT) {
        JSObject *p = JS_VALUE_GET_OBJ(this_val);
        if (p->class_id == JS_CLASS_BOOLEAN) {
            if (JS_VALUE_GET_TAG(p->u.object_data) == JS_TAG_BOOL)
                return p->u.object_data;
        }
    }
    return JS_ThrowTypeError(ctx, "not a boolean");
}

static JSValue js_boolean_toString(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    JSValue val = js_thisBooleanValue(ctx, this_val);
    if (JS_IsException(val))
        return val;
    return JS_AtomToString(ctx, JS_VALUE_GET_BOOL(val) ?
                       JS_ATOM_true : JS_ATOM_false);
}

static JSValue js_boolean_valueOf(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    return js_thisBooleanValue(ctx, this_val);
}

static const JSCFunctionListEntry js_boolean_proto_funcs[] = {
    JS_CFUNC_DEF("toString", 0, js_boolean_toString ),
    JS_CFUNC_DEF("valueOf", 0, js_boolean_valueOf ),
};

/* String */

static int js_string_get_own_property(JSContext *ctx,
                                      JSPropertyDescriptor *desc,
                                      JSValueConst obj, JSAtom prop)
{
    JSObject *p;
    JSString *p1;
    uint32_t idx, ch;

    /* This is a class exotic method: obj class_id is JS_CLASS_STRING */
    if (__JS_AtomIsTaggedInt(prop)) {
        p = JS_VALUE_GET_OBJ(obj);
        if (JS_VALUE_GET_TAG(p->u.object_data) == JS_TAG_STRING) {
            p1 = JS_VALUE_GET_STRING(p->u.object_data);
            idx = __JS_AtomToUInt32(prop);
            if (idx < p1->len) {
                if (desc) {
                    ch = string_get(p1, idx);
                    desc->flags = JS_PROP_ENUMERABLE;
                    desc->value = js_new_string_char(ctx, ch);
                    desc->getter = JS_UNDEFINED;
                    desc->setter = JS_UNDEFINED;
                }
                return true;
            }
        }
    }
    return false;
}

static int js_string_define_own_property(JSContext *ctx,
                                         JSValueConst this_obj,
                                         JSAtom prop, JSValueConst val,
                                         JSValueConst getter,
                                         JSValueConst setter, int flags)
{
    uint32_t idx;
    JSObject *p;
    JSString *p1, *p2;

    if (__JS_AtomIsTaggedInt(prop)) {
        idx = __JS_AtomToUInt32(prop);
        p = JS_VALUE_GET_OBJ(this_obj);
        if (JS_VALUE_GET_TAG(p->u.object_data) != JS_TAG_STRING)
            goto def;
        p1 = JS_VALUE_GET_STRING(p->u.object_data);
        if (idx >= p1->len)
            goto def;
        if (!check_define_prop_flags(JS_PROP_ENUMERABLE, flags))
            goto fail;
        /* check that the same value is configured */
        if (flags & JS_PROP_HAS_VALUE) {
            if (JS_VALUE_GET_TAG(val) != JS_TAG_STRING)
                goto fail;
            p2 = JS_VALUE_GET_STRING(val);
            if (p2->len != 1)
                goto fail;
            if (string_get(p1, idx) != string_get(p2, 0)) {
            fail:
                return JS_ThrowTypeErrorOrFalse(ctx, flags, "property is not configurable");
            }
        }
        return true;
    } else {
    def:
        return JS_DefineProperty(ctx, this_obj, prop, val, getter, setter,
                                 flags | JS_PROP_NO_EXOTIC);
    }
}

static int js_string_delete_property(JSContext *ctx,
                                     JSValueConst obj, JSAtom prop)
{
    uint32_t idx;

    if (__JS_AtomIsTaggedInt(prop)) {
        idx = __JS_AtomToUInt32(prop);
        if (idx < js_string_obj_get_length(ctx, obj)) {
            return false;
        }
    }
    return true;
}

static const JSClassExoticMethods js_string_exotic_methods = {
    .get_own_property = js_string_get_own_property,
    .define_own_property = js_string_define_own_property,
    .delete_property = js_string_delete_property,
};

static JSValue js_string_constructor(JSContext *ctx, JSValueConst new_target,
                                     int argc, JSValueConst *argv)
{
    JSValue val, obj;
    if (argc == 0) {
        val = js_empty_string(ctx->rt);
    } else {
        if (JS_IsUndefined(new_target) && JS_IsSymbol(argv[0])) {
            JSAtomStruct *p = JS_VALUE_GET_PTR(argv[0]);
            val = JS_ConcatString3(ctx, "Symbol(", JS_AtomToString(ctx, js_get_atom_index(ctx->rt, p)), ")");
        } else {
            val = JS_ToString(ctx, argv[0]);
        }
        if (JS_IsException(val))
            return val;
    }
    if (!JS_IsUndefined(new_target)) {
        JSString *p1 = JS_VALUE_GET_STRING(val);

        obj = js_create_from_ctor(ctx, new_target, JS_CLASS_STRING);
        if (JS_IsException(obj)) {
            JS_FreeValue(ctx, val);
        } else {
            JS_SetObjectData(ctx, obj, val);
            JS_DefinePropertyValue(ctx, obj, JS_ATOM_length, js_int32(p1->len), 0);
        }
        return obj;
    } else {
        return val;
    }
}

static JSValue js_thisStringValue(JSContext *ctx, JSValueConst this_val)
{
    if (tag_is_string(JS_VALUE_GET_TAG(this_val)))
        return js_dup(this_val);

    if (JS_VALUE_GET_TAG(this_val) == JS_TAG_OBJECT) {
        JSObject *p = JS_VALUE_GET_OBJ(this_val);
        if (p->class_id == JS_CLASS_STRING) {
            if (JS_VALUE_GET_TAG(p->u.object_data) == JS_TAG_STRING)
                return js_dup(p->u.object_data);
        }
    }
    return JS_ThrowTypeError(ctx, "not a string");
}

static JSValue js_string_fromCharCode(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    int i;
    StringBuffer b_s, *b = &b_s;

    // shortcut for single argument common case
    if (argc == 1 && JS_VALUE_GET_TAG(argv[0]) == JS_TAG_INT) {
        uint16_t c16 = JS_VALUE_GET_INT(argv[0]);
        return js_new_string_char(ctx, c16);
    }

    string_buffer_init(ctx, b, argc);

    for(i = 0; i < argc; i++) {
        int32_t c;
        if (JS_ToInt32(ctx, &c, argv[i]) || string_buffer_putc16(b, c & 0xffff)) {
            string_buffer_free(b);
            return JS_EXCEPTION;
        }
    }
    return string_buffer_end(b);
}

static JSValue js_string_fromCodePoint(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    double d;
    int i, c;
    StringBuffer b_s, *b = NULL;

    // shortcut for single argument common case
    if (argc == 1 && JS_VALUE_GET_TAG(argv[0]) == JS_TAG_INT) {
        c = JS_VALUE_GET_INT(argv[0]);
        if (c < 0 || c > 0x10ffff)
            goto range_error;
        if (c <= 0xffff) {
            return js_new_string_char(ctx, c);
        } else {
            uint16_t c16[2];
            c16[0] = get_hi_surrogate(c);
            c16[1] = get_lo_surrogate(c);
            return js_new_string16_len(ctx, c16, 2);
        }
    }

    /* XXX: could pre-compute string length if all arguments are JS_TAG_INT */
    b = &b_s;
    if (string_buffer_init(ctx, b, argc))
        goto fail;
    for(i = 0; i < argc; i++) {
        if (JS_VALUE_GET_TAG(argv[i]) == JS_TAG_INT) {
            c = JS_VALUE_GET_INT(argv[i]);
            if (c < 0 || c > 0x10ffff)
                goto range_error;
        } else {
            if (JS_ToFloat64(ctx, &d, argv[i]))
                goto fail;
            if (!(d >= 0 && d <= 0x10ffff) || (c = (int)d) != d)
                goto range_error;
        }
        if (string_buffer_putc(b, c))
            goto fail;
    }
    return string_buffer_end(b);

 range_error:
    JS_ThrowRangeError(ctx, "invalid code point");
 fail:
    if (b) string_buffer_free(b);
    return JS_EXCEPTION;
}

static JSValue js_string_raw(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    // raw(temp,...a)
    JSValue cooked, val, raw;
    StringBuffer b_s, *b = &b_s;
    int64_t i, n;

    string_buffer_init(ctx, b, 0);
    raw = JS_UNDEFINED;
    cooked = JS_ToObject(ctx, argv[0]);
    if (JS_IsException(cooked))
        goto exception;
    raw = JS_ToObjectFree(ctx, JS_GetProperty(ctx, cooked, JS_ATOM_raw));
    if (JS_IsException(raw))
        goto exception;
    if (js_get_length64(ctx, &n, raw) < 0)
        goto exception;

    for (i = 0; i < n; i++) {
        val = JS_ToStringFree(ctx, JS_GetPropertyInt64(ctx, raw, i));
        if (JS_IsException(val))
            goto exception;
        string_buffer_concat_value_free(b, val);
        if (i < n - 1 && i + 1 < argc) {
            if (string_buffer_concat_value(b, argv[i + 1]))
                goto exception;
        }
    }
    JS_FreeValue(ctx, cooked);
    JS_FreeValue(ctx, raw);
    return string_buffer_end(b);

exception:
    JS_FreeValue(ctx, cooked);
    JS_FreeValue(ctx, raw);
    string_buffer_free(b);
    return JS_EXCEPTION;
}

/* only used in test262 */
JSValue js_string_codePointRange(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    uint32_t start, end, i, n;
    StringBuffer b_s, *b = &b_s;

    if (JS_ToUint32(ctx, &start, argv[0]) ||
        JS_ToUint32(ctx, &end, argv[1]))
        return JS_EXCEPTION;
    end = min_uint32(end, 0x10ffff + 1);

    if (start > end) {
        start = end;
    }
    n = end - start;
    if (end > 0x10000) {
        n += end - max_uint32(start, 0x10000);
    }
    if (string_buffer_init2(ctx, b, n, end >= 0x100))
        return JS_EXCEPTION;
    for(i = start; i < end; i++) {
        string_buffer_putc(b, i);
    }
    return string_buffer_end(b);
}

static JSValue js_string_at(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv)
{
    JSValue val, ret;
    JSString *p;
    int idx, c;

    val = JS_ToStringCheckObject(ctx, this_val);
    if (JS_IsException(val))
        return val;
    p = JS_VALUE_GET_STRING(val);
    if (JS_ToInt32Sat(ctx, &idx, argv[0])) {
        JS_FreeValue(ctx, val);
        return JS_EXCEPTION;
    }
    if (idx < 0)
        idx = p->len + idx;
    if (idx < 0 || idx >= p->len) {
        ret = JS_UNDEFINED;
    } else {
        c = string_get(p, idx);
        ret = js_new_string_char(ctx, c);
    }
    JS_FreeValue(ctx, val);
    return ret;
}

JSValue js_string_charCodeAt(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    JSValue val, ret;
    JSString *p;
    int idx, c;

    val = JS_ToStringCheckObject(ctx, this_val);
    if (JS_IsException(val))
        return val;
    p = JS_VALUE_GET_STRING(val);
    if (JS_ToInt32Sat(ctx, &idx, argv[0])) {
        JS_FreeValue(ctx, val);
        return JS_EXCEPTION;
    }
    if (idx < 0 || idx >= p->len) {
        ret = JS_NAN;
    } else {
        c = string_get(p, idx);
        ret = js_int32(c);
    }
    JS_FreeValue(ctx, val);
    return ret;
}

static JSValue js_string_charAt(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    JSValue val, ret;
    JSString *p;
    int idx, c;

    val = JS_ToStringCheckObject(ctx, this_val);
    if (JS_IsException(val))
        return val;
    p = JS_VALUE_GET_STRING(val);
    if (JS_ToInt32Sat(ctx, &idx, argv[0])) {
        JS_FreeValue(ctx, val);
        return JS_EXCEPTION;
    }
    if (idx < 0 || idx >= p->len) {
        ret = js_empty_string(ctx->rt);
    } else {
        c = string_get(p, idx);
        ret = js_new_string_char(ctx, c);
    }
    JS_FreeValue(ctx, val);
    return ret;
}

static JSValue js_string_codePointAt(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    JSValue val, ret;
    JSString *p;
    int idx, c;

    val = JS_ToStringCheckObject(ctx, this_val);
    if (JS_IsException(val))
        return val;
    p = JS_VALUE_GET_STRING(val);
    if (JS_ToInt32Sat(ctx, &idx, argv[0])) {
        JS_FreeValue(ctx, val);
        return JS_EXCEPTION;
    }
    if (idx < 0 || idx >= p->len) {
        ret = JS_UNDEFINED;
    } else {
        c = string_getc(p, &idx);
        ret = js_int32(c);
    }
    JS_FreeValue(ctx, val);
    return ret;
}

static JSValue js_string_concat(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    int i, is_wide_char;
    JSString *p, *q;
    int64_t len;
    uint32_t n;
    JSValue r;
    char *d;

    if (JS_TAG_STRING != JS_VALUE_GET_TAG(this_val))
        goto slow_path;
    p = JS_VALUE_GET_STRING(this_val);
    len = p->len;
    is_wide_char = p->is_wide_char;
    for (i = 0; i < argc; i++) {
        if (JS_TAG_STRING != JS_VALUE_GET_TAG(argv[i]))
            goto slow_path;
        p = JS_VALUE_GET_STRING(argv[i]);
        if (p->is_wide_char ^ is_wide_char)
            goto slow_path;
        len += p->len;
    }
    if (len > INT_MAX>>is_wide_char)
        return JS_ThrowOutOfMemory(ctx);
    p = JS_VALUE_GET_STRING(this_val);
    if (p->len == len)
        return js_dup(this_val);
    q = js_alloc_string(ctx, len, is_wide_char);
    if (!q)
        return JS_EXCEPTION;
    d = strv(q);
    n = p->len << is_wide_char;
    memcpy(d, strv(p), n);
    d += n;
    for (i = 0; i < argc; i++) {
        p = JS_VALUE_GET_STRING(argv[i]);
        n = p->len << is_wide_char;
        memcpy(d, strv(p), n);
        d += n;
    }
    if (!is_wide_char)
        *d = '\0';
    return JS_MKPTR(JS_TAG_STRING, q);
slow_path:
    r = JS_ToStringCheckObject(ctx, this_val);
    for (i = 0; i < argc; i++) {
        if (JS_IsException(r))
            break;
        r = JS_ConcatString(ctx, r, js_dup(argv[i]));
    }
    return r;
}

static int string_cmp(JSString *p1, JSString *p2, int x1, int x2, int len)
{
    int i, c1, c2;
    for (i = 0; i < len; i++) {
        if ((c1 = string_get(p1, x1 + i)) != (c2 = string_get(p2, x2 + i)))
            return c1 - c2;
    }
    return 0;
}

static int string_indexof_char(JSString *p, int c, int from)
{
    /* assuming 0 <= from <= p->len */
    int i, len = p->len;
    if (p->is_wide_char) {
        for (i = from; i < len; i++) {
            if (str16(p)[i] == c)
                return i;
        }
    } else {
        if ((c & ~0xff) == 0) {
            for (i = from; i < len; i++) {
                if (str8(p)[i] == (uint8_t)c)
                    return i;
            }
        }
    }
    return -1;
}

static int string_indexof(JSString *p1, JSString *p2, int from)
{
    /* assuming 0 <= from <= p1->len */
    int c, i, j, len1 = p1->len, len2 = p2->len;
    if (len2 == 0)
        return from;
    for (i = from, c = string_get(p2, 0); i + len2 <= len1; i = j + 1) {
        j = string_indexof_char(p1, c, i);
        if (j < 0 || j + len2 > len1)
            break;
        if (!string_cmp(p1, p2, j + 1, 1, len2 - 1))
            return j;
    }
    return -1;
}

static int64_t string_advance_index(JSString *p, int64_t index, bool unicode)
{
    if (!unicode || index >= p->len || !p->is_wide_char) {
        index++;
    } else {
        int index32 = (int)index;
        string_getc(p, &index32);
        index = index32;
    }
    return index;
}

static JSValue js_string_isWellFormed(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    JSValue str;
    JSValue ret;
    JSString *p;
    uint32_t c, i, n;

    ret = JS_TRUE;
    str = JS_ToStringCheckObject(ctx, this_val);
    if (JS_IsException(str))
        return JS_EXCEPTION;

    p = JS_VALUE_GET_STRING(str);
    if (!p->is_wide_char || p->len == 0)
        goto done; // by definition well-formed

    for (i = 0, n = p->len; i < n; i++) {
        c = str16(p)[i];
        if (!is_surrogate(c))
            continue;
        if (is_lo_surrogate(c) || i + 1 == n)
            break;
        c = str16(p)[++i];
        if (!is_lo_surrogate(c))
            break;
    }

    if (i < n)
        ret = JS_FALSE;

done:
    JS_FreeValue(ctx, str);
    return ret;
}

static JSValue js_string_toWellFormed(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    JSValue str;
    JSValue ret;
    JSString *p;
    uint32_t c, i, n;

    str = JS_ToStringCheckObject(ctx, this_val);
    if (JS_IsException(str))
        return JS_EXCEPTION;

    p = JS_VALUE_GET_STRING(str);
    if (!p->is_wide_char || p->len == 0)
        return str; // by definition well-formed

    // TODO(bnoordhuis) don't clone when input is well-formed
    ret = js_new_string16_len(ctx, str16(p), p->len);
    JS_FreeValue(ctx, str);
    if (JS_IsException(ret))
        return JS_EXCEPTION;

    p = JS_VALUE_GET_STRING(ret);
    for (i = 0, n = p->len; i < n; i++) {
        c = str16(p)[i];
        if (!is_surrogate(c))
            continue;
        if (is_lo_surrogate(c) || i + 1 == n) {
            str16(p)[i] = 0xFFFD;
            continue;
        }
        c = str16(p)[++i];
        if (!is_lo_surrogate(c))
            str16(p)[--i] = 0xFFFD;
    }

    return ret;
}

static JSValue js_string_indexOf(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv, int lastIndexOf)
{
    JSValue str, v;
    int i, len, v_len, pos, start, stop, ret, inc;
    JSString *p;
    JSString *p1;

    str = JS_ToStringCheckObject(ctx, this_val);
    if (JS_IsException(str))
        return str;
    v = JS_ToString(ctx, argv[0]);
    if (JS_IsException(v))
        goto fail;
    p = JS_VALUE_GET_STRING(str);
    p1 = JS_VALUE_GET_STRING(v);
    len = p->len;
    v_len = p1->len;
    if (lastIndexOf) {
        pos = len - v_len;
        if (argc > 1) {
            double d;
            if (JS_ToFloat64(ctx, &d, argv[1]))
                goto fail;
            if (!isnan(d)) {
                if (d <= 0)
                    pos = 0;
                else if (d < pos)
                    pos = d;
            }
        }
        start = pos;
        stop = 0;
        inc = -1;
    } else {
        pos = 0;
        if (argc > 1) {
            if (JS_ToInt32Clamp(ctx, &pos, argv[1], 0, len, 0))
                goto fail;
        }
        start = pos;
        stop = len - v_len;
        inc = 1;
    }
    ret = -1;
    if (len >= v_len && inc * (stop - start) >= 0) {
        for (i = start;; i += inc) {
            if (!string_cmp(p, p1, i, 0, v_len)) {
                ret = i;
                break;
            }
            if (i == stop)
                break;
        }
    }
    JS_FreeValue(ctx, str);
    JS_FreeValue(ctx, v);
    return js_int32(ret);

fail:
    JS_FreeValue(ctx, str);
    JS_FreeValue(ctx, v);
    return JS_EXCEPTION;
}

/* return < 0 if exception or true/false */
static int js_is_regexp(JSContext *ctx, JSValueConst obj);

static JSValue js_string_includes(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv, int magic)
{
    JSValue str, v = JS_UNDEFINED;
    int i, len, v_len, pos, start, stop, ret;
    JSString *p;
    JSString *p1;

    str = JS_ToStringCheckObject(ctx, this_val);
    if (JS_IsException(str))
        return str;
    ret = js_is_regexp(ctx, argv[0]);
    if (ret) {
        if (ret > 0)
            JS_ThrowTypeError(ctx, "regexp not supported");
        goto fail;
    }
    v = JS_ToString(ctx, argv[0]);
    if (JS_IsException(v))
        goto fail;
    p = JS_VALUE_GET_STRING(str);
    p1 = JS_VALUE_GET_STRING(v);
    len = p->len;
    v_len = p1->len;
    pos = (magic == 2) ? len : 0;
    if (argc > 1 && !JS_IsUndefined(argv[1])) {
        if (JS_ToInt32Clamp(ctx, &pos, argv[1], 0, len, 0))
            goto fail;
    }
    len -= v_len;
    ret = 0;
    if (magic == 0) {
        start = pos;
        stop = len;
    } else {
        if (magic == 1) {
            if (pos > len)
                goto done;
        } else {
            pos -= v_len;
        }
        start = stop = pos;
    }
    if (start >= 0 && start <= stop) {
        for (i = start;; i++) {
            if (!string_cmp(p, p1, i, 0, v_len)) {
                ret = 1;
                break;
            }
            if (i == stop)
                break;
        }
    }
 done:
    JS_FreeValue(ctx, str);
    JS_FreeValue(ctx, v);
    return js_bool(ret);

fail:
    JS_FreeValue(ctx, str);
    JS_FreeValue(ctx, v);
    return JS_EXCEPTION;
}

static int check_regexp_g_flag(JSContext *ctx, JSValueConst regexp)
{
    int ret;
    JSValue flags;

    ret = js_is_regexp(ctx, regexp);
    if (ret < 0)
        return -1;
    if (ret) {
        flags = JS_GetProperty(ctx, regexp, JS_ATOM_flags);
        if (JS_IsException(flags))
            return -1;
        if (JS_IsUndefined(flags) || JS_IsNull(flags)) {
            JS_ThrowTypeError(ctx, "cannot convert to object");
            return -1;
        }
        flags = JS_ToStringFree(ctx, flags);
        if (JS_IsException(flags))
            return -1;
        ret = string_indexof_char(JS_VALUE_GET_STRING(flags), 'g', 0);
        JS_FreeValue(ctx, flags);
        if (ret < 0) {
            JS_ThrowTypeError(ctx, "regexp must have the 'g' flag");
            return -1;
        }
    }
    return 0;
}

static JSValue js_string_match(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv, int atom)
{
    // match(rx), search(rx), matchAll(rx)
    // atom is JS_ATOM_Symbol_match, JS_ATOM_Symbol_search, or JS_ATOM_Symbol_matchAll
    JSValueConst O = this_val, regexp = argv[0], args[2];
    JSValue matcher, S, rx, result, str;
    int args_len;

    if (JS_IsUndefined(O) || JS_IsNull(O))
        return JS_ThrowTypeError(ctx, "cannot convert to object");

    if (JS_IsObject(regexp)) {
        matcher = JS_GetProperty(ctx, regexp, atom);
        if (JS_IsException(matcher))
            return JS_EXCEPTION;
        if (atom == JS_ATOM_Symbol_matchAll) {
            if (check_regexp_g_flag(ctx, regexp) < 0) {
                JS_FreeValue(ctx, matcher);
                return JS_EXCEPTION;
            }
        }
        if (!JS_IsUndefined(matcher) && !JS_IsNull(matcher)) {
            return JS_CallFree(ctx, matcher, regexp, 1, &O);
        }
    }
    S = JS_ToString(ctx, O);
    if (JS_IsException(S))
        return JS_EXCEPTION;
    args_len = 1;
    args[0] = regexp;
    str = JS_UNDEFINED;
    if (atom == JS_ATOM_Symbol_matchAll) {
        str = js_new_string8(ctx, "g");
        if (JS_IsException(str))
            goto fail;
        args[args_len++] = str;
    }
    rx = JS_CallConstructor(ctx, ctx->regexp_ctor, args_len, args);
    JS_FreeValue(ctx, str);
    if (JS_IsException(rx)) {
    fail:
        JS_FreeValue(ctx, S);
        return JS_EXCEPTION;
    }
    result = JS_InvokeFree(ctx, rx, atom, 1, vc(&S));
    JS_FreeValue(ctx, S);
    return result;
}

static JSValue js_string___GetSubstitution(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv)
{
    // GetSubstitution(matched, str, position, captures, namedCaptures, rep)
    JSValueConst matched, str, captures, namedCaptures, rep;
    JSValue capture, name, s;
    uint32_t position, len, matched_len, captures_len;
    int i, j, j0, k, k1;
    int c, c1;
    StringBuffer b_s, *b = &b_s;
    JSString *sp, *rp;

    matched = argv[0];
    str = argv[1];
    captures = argv[3];
    namedCaptures = argv[4];
    rep = argv[5];

    if (!JS_IsString(rep) || !JS_IsString(str))
        return JS_ThrowTypeError(ctx, "not a string");

    sp = JS_VALUE_GET_STRING(str);
    rp = JS_VALUE_GET_STRING(rep);

    string_buffer_init(ctx, b, 0);

    captures_len = 0;
    if (!JS_IsUndefined(captures)) {
        if (js_get_length32(ctx, &captures_len, captures))
            goto exception;
    }
    if (js_get_length32(ctx, &matched_len, matched))
        goto exception;
    if (JS_ToUint32(ctx, &position, argv[2]) < 0)
        goto exception;

    len = rp->len;
    i = 0;
    for(;;) {
        j = string_indexof_char(rp, '$', i);
        if (j < 0 || j + 1 >= len)
            break;
        string_buffer_concat(b, rp, i, j);
        j0 = j++;
        c = string_get(rp, j++);
        if (c == '$') {
            string_buffer_putc8(b, '$');
        } else if (c == '&') {
            if (string_buffer_concat_value(b, matched))
                goto exception;
        } else if (c == '`') {
            string_buffer_concat(b, sp, 0, position);
        } else if (c == '\'') {
            string_buffer_concat(b, sp, position + matched_len, sp->len);
        } else if (c >= '0' && c <= '9') {
            k = c - '0';
            if (j < len) {
                c1 = string_get(rp, j);
                if (c1 >= '0' && c1 <= '9') {
                    /* This behavior is specified in ES6 and refined in ECMA 2019 */
                    /* ECMA 2019 does not have the extra test, but
                       Test262 S15.5.4.11_A3_T1..3 require this behavior */
                    k1 = k * 10 + c1 - '0';
                    if (k1 >= 1 && k1 < captures_len) {
                        k = k1;
                        j++;
                    }
                }
            }
            if (k >= 1 && k < captures_len) {
                s = JS_GetPropertyInt64(ctx, captures, k);
                if (JS_IsException(s))
                    goto exception;
                if (!JS_IsUndefined(s)) {
                    if (string_buffer_concat_value_free(b, s))
                        goto exception;
                }
            } else {
                goto norep;
            }
        } else if (c == '<' && !JS_IsUndefined(namedCaptures)) {
            k = string_indexof_char(rp, '>', j);
            if (k < 0)
                goto norep;
            name = js_sub_string(ctx, rp, j, k);
            if (JS_IsException(name))
                goto exception;
            capture = JS_GetPropertyValue(ctx, namedCaptures, name);
            if (JS_IsException(capture))
                goto exception;
            if (!JS_IsUndefined(capture)) {
                if (string_buffer_concat_value_free(b, capture))
                    goto exception;
            }
            j = k + 1;
        } else {
        norep:
            string_buffer_concat(b, rp, j0, j);
        }
        i = j;
    }
    string_buffer_concat(b, rp, i, rp->len);
    return string_buffer_end(b);
exception:
    string_buffer_free(b);
    return JS_EXCEPTION;
}

static JSValue js_string_replace(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv,
                                 int is_replaceAll)
{
    // replace(rx, rep)
    JSValueConst O = this_val, searchValue = argv[0], replaceValue = argv[1];
    JSValueConst args[6];
    JSValue str, search_str, replaceValue_str, repl_str;
    JSString *sp, *searchp;
    StringBuffer b_s, *b = &b_s;
    int pos, functionalReplace, endOfLastMatch;
    bool is_first;

    if (JS_IsUndefined(O) || JS_IsNull(O))
        return JS_ThrowTypeError(ctx, "cannot convert to object");

    search_str = JS_UNDEFINED;
    replaceValue_str = JS_UNDEFINED;
    repl_str = JS_UNDEFINED;

    if (JS_IsObject(searchValue)) {
        JSValue replacer;
        if (is_replaceAll) {
            if (check_regexp_g_flag(ctx, searchValue) < 0)
                return JS_EXCEPTION;
        }
        replacer = JS_GetProperty(ctx, searchValue, JS_ATOM_Symbol_replace);
        if (JS_IsException(replacer))
            return JS_EXCEPTION;
        if (!JS_IsUndefined(replacer) && !JS_IsNull(replacer)) {
            args[0] = O;
            args[1] = replaceValue;
            return JS_CallFree(ctx, replacer, searchValue, 2, args);
        }
    }
    string_buffer_init(ctx, b, 0);

    str = JS_ToString(ctx, O);
    if (JS_IsException(str))
        goto exception;
    search_str = JS_ToString(ctx, searchValue);
    if (JS_IsException(search_str))
        goto exception;
    functionalReplace = JS_IsFunction(ctx, replaceValue);
    if (!functionalReplace) {
        replaceValue_str = JS_ToString(ctx, replaceValue);
        if (JS_IsException(replaceValue_str))
            goto exception;
    }

    sp = JS_VALUE_GET_STRING(str);
    searchp = JS_VALUE_GET_STRING(search_str);
    endOfLastMatch = 0;
    is_first = true;
    for(;;) {
        if (unlikely(searchp->len == 0)) {
            if (is_first)
                pos = 0;
            else if (endOfLastMatch >= sp->len)
                pos = -1;
            else
                pos = endOfLastMatch + 1;
        } else {
            pos = string_indexof(sp, searchp, endOfLastMatch);
        }
        if (pos < 0) {
            if (is_first) {
                string_buffer_free(b);
                JS_FreeValue(ctx, search_str);
                JS_FreeValue(ctx, replaceValue_str);
                return str;
            } else {
                break;
            }
        }
        if (functionalReplace) {
            args[0] = search_str;
            args[1] = js_int32(pos);
            args[2] = str;
            repl_str = JS_ToStringFree(ctx, JS_Call(ctx, replaceValue, JS_UNDEFINED, 3, args));
        } else {
            args[0] = search_str;
            args[1] = str;
            args[2] = js_int32(pos);
            args[3] = JS_UNDEFINED;
            args[4] = JS_UNDEFINED;
            args[5] = replaceValue_str;
            repl_str = js_string___GetSubstitution(ctx, JS_UNDEFINED, 6, args);
        }
        if (JS_IsException(repl_str))
            goto exception;

        string_buffer_concat(b, sp, endOfLastMatch, pos);
        string_buffer_concat_value_free(b, repl_str);
        endOfLastMatch = pos + searchp->len;
        is_first = false;
        if (!is_replaceAll)
            break;
    }
    string_buffer_concat(b, sp, endOfLastMatch, sp->len);
    JS_FreeValue(ctx, search_str);
    JS_FreeValue(ctx, replaceValue_str);
    JS_FreeValue(ctx, str);
    return string_buffer_end(b);

exception:
    string_buffer_free(b);
    JS_FreeValue(ctx, search_str);
    JS_FreeValue(ctx, replaceValue_str);
    JS_FreeValue(ctx, str);
    return JS_EXCEPTION;
}

static JSValue js_string_split(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    // split(sep, limit)
    JSValueConst O = this_val, separator = argv[0], limit = argv[1];
    JSValueConst args[2];
    JSValue S, A, R, T;
    uint32_t lim, lengthA;
    int64_t p, q, s, r, e;
    JSString *sp, *rp;

    if (JS_IsUndefined(O) || JS_IsNull(O))
        return JS_ThrowTypeError(ctx, "cannot convert to object");

    S = JS_UNDEFINED;
    A = JS_UNDEFINED;
    R = JS_UNDEFINED;

    if (JS_IsObject(separator)) {
        JSValue splitter;
        splitter = JS_GetProperty(ctx, separator, JS_ATOM_Symbol_split);
        if (JS_IsException(splitter))
            return JS_EXCEPTION;
        if (!JS_IsUndefined(splitter) && !JS_IsNull(splitter)) {
            args[0] = O;
            args[1] = limit;
            return JS_CallFree(ctx, splitter, separator, 2, args);
        }
    }
    S = JS_ToString(ctx, O);
    if (JS_IsException(S))
        goto exception;
    A = JS_NewArray(ctx);
    if (JS_IsException(A))
        goto exception;
    lengthA = 0;
    if (JS_IsUndefined(limit)) {
        lim = 0xffffffff;
    } else {
        if (JS_ToUint32(ctx, &lim, limit) < 0)
            goto exception;
    }
    sp = JS_VALUE_GET_STRING(S);
    s = sp->len;
    R = JS_ToString(ctx, separator);
    if (JS_IsException(R))
        goto exception;
    rp = JS_VALUE_GET_STRING(R);
    r = rp->len;
    p = 0;
    if (lim == 0)
        goto done;
    if (JS_IsUndefined(separator))
        goto add_tail;
    if (s == 0) {
        if (r != 0)
            goto add_tail;
        goto done;
    }
    q = p;
    for (q = p; (q += !r) <= s - r - !r; q = p = e + r) {
        e = string_indexof(sp, rp, q);
        if (e < 0)
            break;
        T = js_sub_string(ctx, sp, p, e);
        if (JS_IsException(T))
            goto exception;
        if (JS_CreateDataPropertyUint32(ctx, A, lengthA++, T, 0) < 0)
            goto exception;
        if (lengthA == lim)
            goto done;
    }
add_tail:
    T = js_sub_string(ctx, sp, p, s);
    if (JS_IsException(T))
        goto exception;
    if (JS_CreateDataPropertyUint32(ctx, A, lengthA++, T,0 ) < 0)
        goto exception;
done:
    JS_FreeValue(ctx, S);
    JS_FreeValue(ctx, R);
    return A;

exception:
    JS_FreeValue(ctx, A);
    JS_FreeValue(ctx, S);
    JS_FreeValue(ctx, R);
    return JS_EXCEPTION;
}

