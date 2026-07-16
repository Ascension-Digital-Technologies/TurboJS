/* Engine domain source: builtins/core_builtins.inc -> number_math_builtin.
 * Ownership: builtins subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static JSValue js_string_substring(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    JSValue str, ret;
    int a, b, start, end;
    JSString *p;

    str = JS_ToStringCheckObject(ctx, this_val);
    if (JS_IsException(str))
        return str;
    p = JS_VALUE_GET_STRING(str);
    if (JS_ToInt32Clamp(ctx, &a, argv[0], 0, p->len, 0)) {
        JS_FreeValue(ctx, str);
        return JS_EXCEPTION;
    }
    b = p->len;
    if (!JS_IsUndefined(argv[1])) {
        if (JS_ToInt32Clamp(ctx, &b, argv[1], 0, p->len, 0)) {
            JS_FreeValue(ctx, str);
            return JS_EXCEPTION;
        }
    }
    if (a < b) {
        start = a;
        end = b;
    } else {
        start = b;
        end = a;
    }
    ret = js_sub_string(ctx, p, start, end);
    JS_FreeValue(ctx, str);
    return ret;
}

static JSValue js_string_substr(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    JSValue str, ret;
    int a, len, n;
    JSString *p;

    str = JS_ToStringCheckObject(ctx, this_val);
    if (JS_IsException(str))
        return str;
    p = JS_VALUE_GET_STRING(str);
    len = p->len;
    if (JS_ToInt32Clamp(ctx, &a, argv[0], 0, len, len)) {
        JS_FreeValue(ctx, str);
        return JS_EXCEPTION;
    }
    n = len - a;
    if (!JS_IsUndefined(argv[1])) {
        if (JS_ToInt32Clamp(ctx, &n, argv[1], 0, len - a, 0)) {
            JS_FreeValue(ctx, str);
            return JS_EXCEPTION;
        }
    }
    ret = js_sub_string(ctx, p, a, a + n);
    JS_FreeValue(ctx, str);
    return ret;
}

static JSValue js_string_slice(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    JSValue str, ret;
    int len, start, end;
    JSString *p;

    str = JS_ToStringCheckObject(ctx, this_val);
    if (JS_IsException(str))
        return str;
    p = JS_VALUE_GET_STRING(str);
    len = p->len;
    if (JS_ToInt32Clamp(ctx, &start, argv[0], 0, len, len)) {
        JS_FreeValue(ctx, str);
        return JS_EXCEPTION;
    }
    end = len;
    if (!JS_IsUndefined(argv[1])) {
        if (JS_ToInt32Clamp(ctx, &end, argv[1], 0, len, len)) {
            JS_FreeValue(ctx, str);
            return JS_EXCEPTION;
        }
    }
    ret = js_sub_string(ctx, p, start, max_int(end, start));
    JS_FreeValue(ctx, str);
    return ret;
}

static JSValue js_string_pad(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv, int padEnd)
{
    JSValue str, v = JS_UNDEFINED;
    StringBuffer b_s, *b = &b_s;
    JSString *p, *p1 = NULL;
    int n, len, c = ' ';

    str = JS_ToStringCheckObject(ctx, this_val);
    if (JS_IsException(str))
        goto fail1;
    if (JS_ToInt32Sat(ctx, &n, argv[0]))
        goto fail2;
    p = JS_VALUE_GET_STRING(str);
    len = p->len;
    if (len >= n)
        return str;
    if (argc > 1 && !JS_IsUndefined(argv[1])) {
        v = JS_ToString(ctx, argv[1]);
        if (JS_IsException(v))
            goto fail2;
        p1 = JS_VALUE_GET_STRING(v);
        if (p1->len == 0) {
            JS_FreeValue(ctx, v);
            return str;
        }
        if (p1->len == 1) {
            c = string_get(p1, 0);
            p1 = NULL;
        }
    }
    if (n > JS_STRING_LEN_MAX) {
        JS_ThrowRangeError(ctx, "invalid string length");
        goto fail3;
    }
    if (string_buffer_init(ctx, b, n))
        goto fail3;
    n -= len;
    if (padEnd) {
        if (string_buffer_concat(b, p, 0, len))
            goto fail;
    }
    if (p1) {
        while (n > 0) {
            int chunk = min_int(n, p1->len);
            if (string_buffer_concat(b, p1, 0, chunk))
                goto fail;
            n -= chunk;
        }
    } else {
        if (string_buffer_fill(b, c, n))
            goto fail;
    }
    if (!padEnd) {
        if (string_buffer_concat(b, p, 0, len))
            goto fail;
    }
    JS_FreeValue(ctx, v);
    JS_FreeValue(ctx, str);
    return string_buffer_end(b);

fail:
    string_buffer_free(b);
fail3:
    JS_FreeValue(ctx, v);
fail2:
    JS_FreeValue(ctx, str);
fail1:
    return JS_EXCEPTION;
}

static JSValue js_string_repeat(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    JSValue str;
    StringBuffer b_s, *b = &b_s;
    JSString *p;
    int64_t val;
    int n, len;

    str = JS_ToStringCheckObject(ctx, this_val);
    if (JS_IsException(str))
        goto fail;
    if (JS_ToInt64Sat(ctx, &val, argv[0]))
        goto fail;
    if (val < 0 || val > 2147483647) {
        JS_ThrowRangeError(ctx, "invalid repeat count");
        goto fail;
    }
    n = val;
    p = JS_VALUE_GET_STRING(str);
    len = p->len;
    if (len == 0 || n == 1)
        return str;
    if (val * len > JS_STRING_LEN_MAX) {
        JS_ThrowRangeError(ctx, "invalid string length");
        goto fail;
    }
    if (string_buffer_init2(ctx, b, n * len, p->is_wide_char))
        goto fail;
    if (len == 1) {
        string_buffer_fill(b, string_get(p, 0), n);
    } else {
        while (n-- > 0) {
            string_buffer_concat(b, p, 0, len);
        }
    }
    JS_FreeValue(ctx, str);
    return string_buffer_end(b);

fail:
    JS_FreeValue(ctx, str);
    return JS_EXCEPTION;
}

static JSValue js_string_trim(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv, int magic)
{
    JSValue str, ret;
    int a, b, len;
    JSString *p;

    str = JS_ToStringCheckObject(ctx, this_val);
    if (JS_IsException(str))
        return str;
    p = JS_VALUE_GET_STRING(str);
    a = 0;
    b = len = p->len;
    if (magic & 1) {
        while (a < len && lre_is_space(string_get(p, a)))
            a++;
    }
    if (magic & 2) {
        while (b > a && lre_is_space(string_get(p, b - 1)))
            b--;
    }
    ret = js_sub_string(ctx, p, a, b);
    JS_FreeValue(ctx, str);
    return ret;
}

/* return 0 if before the first char */
static int string_prevc(JSString *p, int *pidx)
{
    int idx, c, c1;

    idx = *pidx;
    if (idx <= 0)
        return 0;
    idx--;
    if (p->is_wide_char) {
        c = str16(p)[idx];
        if (is_lo_surrogate(c) && idx > 0) {
            c1 = str16(p)[idx - 1];
            if (is_hi_surrogate(c1)) {
                c = from_surrogate(c1, c);
                idx--;
            }
        }
    } else {
        c = str8(p)[idx];
    }
    *pidx = idx;
    return c;
}

static bool test_final_sigma(JSString *p, int sigma_pos)
{
    int k, c1;

    /* before C: skip case ignorable chars and check there is
       a cased letter */
    k = sigma_pos;
    for(;;) {
        c1 = string_prevc(p, &k);
        if (!lre_is_case_ignorable(c1))
            break;
    }
    if (!lre_is_cased(c1))
        return false;

    /* after C: skip case ignorable chars and check there is
       no cased letter */
    k = sigma_pos + 1;
    for(;;) {
        if (k >= p->len)
            return true;
        c1 = string_getc(p, &k);
        if (!lre_is_case_ignorable(c1))
            break;
    }
    return !lre_is_cased(c1);
}

static int to_utf32_buf(JSContext *ctx, JSString *p, uint32_t **pbuf)
{
    uint32_t *b;
    int i, j, n;

    j = -1;
    n = p->len;
    b = js_malloc(ctx, max_int(1, n) * sizeof(*b));
    if (b)
        for (i = j = 0; i < n;)
            b[j++] = string_getc(p, &i);
    *pbuf = b;
    return j;
}

static JSValue js_string_localeCompare(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    int i, n, an, bn, cmp;
    uint32_t *as, *bs, *ts;
    JSValue a, b, ret;

    ret = JS_EXCEPTION;
    as = NULL;
    bs = NULL;

    a = JS_ToStringCheckObject(ctx, this_val);
    if (JS_IsException(a))
        return JS_EXCEPTION;

    b = JS_ToString(ctx, argv[0]);
    if (JS_IsException(b))
        goto exception;

    an = to_utf32_buf(ctx, JS_VALUE_GET_STRING(a), &as);
    if (an == -1)
        goto exception;

    bn = to_utf32_buf(ctx, JS_VALUE_GET_STRING(b), &bs);
    if (bn == -1)
        goto exception;

    // TODO(bnoordhuis) skip normalization when input is latin1
    an = unicode_normalize(&ts, as, an, UNICODE_NFC, ctx,
                           (DynBufReallocFunc *)js_realloc);
    if (an == -1)
        goto exception;
    js_free(ctx, as);
    as = ts;

    // TODO(bnoordhuis) skip normalization when input is latin1
    bn = unicode_normalize(&ts, bs, bn, UNICODE_NFC, ctx,
                           (DynBufReallocFunc *)js_realloc);
    if (bn == -1)
        goto exception;
    js_free(ctx, bs);
    bs = ts;

    n = min_int(an, bn);
    for (i = 0; i < n; i++)
        if (as[i] != bs[i])
            break;
    if (i < n)
        cmp = compare_u32(as[i], bs[i]);
    else
        cmp = compare_u32(an, bn);
    ret = js_int32(cmp);

exception:
    JS_FreeValue(ctx, a);
    JS_FreeValue(ctx, b);
    js_free(ctx, as);
    js_free(ctx, bs);
    return ret;
}

static JSValue js_string_toLowerCase(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv, int to_lower)
{
    JSValue val;
    StringBuffer b_s, *b = &b_s;
    JSString *p;
    int i, c, j, l;
    uint32_t res[LRE_CC_RES_LEN_MAX];

    val = JS_ToStringCheckObject(ctx, this_val);
    if (JS_IsException(val))
        return val;
    p = JS_VALUE_GET_STRING(val);
    if (p->len == 0)
        return val;
    if (string_buffer_init(ctx, b, p->len))
        goto fail;
    for(i = 0; i < p->len;) {
        c = string_getc(p, &i);
        if (c == 0x3a3 && to_lower && test_final_sigma(p, i - 1)) {
            res[0] = 0x3c2; /* final sigma */
            l = 1;
        } else {
            l = lre_case_conv(res, c, to_lower);
        }
        for(j = 0; j < l; j++) {
            if (string_buffer_putc(b, res[j]))
                goto fail;
        }
    }
    JS_FreeValue(ctx, val);
    return string_buffer_end(b);
 fail:
    JS_FreeValue(ctx, val);
    string_buffer_free(b);
    return JS_EXCEPTION;
}

/* return (-1, NULL) if exception, otherwise (len, buf) */
static int JS_ToUTF32String(JSContext *ctx, uint32_t **pbuf, JSValue val1)
{
    JSValue val;
    int len;

    val = JS_ToString(ctx, val1);
    if (JS_IsException(val))
        return -1;
    len = to_utf32_buf(ctx, JS_VALUE_GET_STRING(val), pbuf);
    JS_FreeValue(ctx, val);
    return len;
}

static JSValue JS_NewUTF32String(JSContext *ctx, const uint32_t *buf, int len)
{
    int i;
    StringBuffer b_s, *b = &b_s;
    if (string_buffer_init(ctx, b, len))
        return JS_EXCEPTION;
    for(i = 0; i < len; i++) {
        if (string_buffer_putc(b, buf[i]))
            goto fail;
    }
    return string_buffer_end(b);
 fail:
    string_buffer_free(b);
    return JS_EXCEPTION;
}

static JSValue js_string_normalize(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    const char *form, *p;
    size_t form_len;
    int is_compat, buf_len, out_len;
    UnicodeNormalizationEnum n_type;
    JSValue val;
    uint32_t *buf, *out_buf;

    val = JS_ToStringCheckObject(ctx, this_val);
    if (JS_IsException(val))
        return val;
    buf = NULL; // appease bogus -Wmaybe-uninitialized warning
    buf_len = JS_ToUTF32String(ctx, &buf, val);
    JS_FreeValue(ctx, val);
    if (buf_len < 0)
        return JS_EXCEPTION;

    if (argc == 0 || JS_IsUndefined(argv[0])) {
        n_type = UNICODE_NFC;
    } else {
        form = JS_ToCStringLen(ctx, &form_len, argv[0]);
        if (!form)
            goto fail1;
        p = form;
        if (p[0] != 'N' || p[1] != 'F')
            goto bad_form;
        p += 2;
        is_compat = false;
        if (*p == 'K') {
            is_compat = true;
            p++;
        }
        if (*p == 'C' || *p == 'D') {
            n_type = UNICODE_NFC + is_compat * 2 + (*p - 'C');
            if ((p + 1 - form) != form_len)
                goto bad_form;
        } else {
        bad_form:
            JS_FreeCString(ctx, form);
            JS_ThrowRangeError(ctx, "bad normalization form");
        fail1:
            js_free(ctx, buf);
            return JS_EXCEPTION;
        }
        JS_FreeCString(ctx, form);
    }

    out_len = unicode_normalize(&out_buf, buf, buf_len, n_type,
                                ctx->rt, (DynBufReallocFunc *)js_realloc_rt);
    js_free(ctx, buf);
    if (out_len < 0)
        return JS_EXCEPTION;
    val = JS_NewUTF32String(ctx, out_buf, out_len);
    js_free(ctx, out_buf);
    return val;
}

/* also used for String.prototype.valueOf */
static JSValue js_string_toString(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    return js_thisStringValue(ctx, this_val);
}

/* String Iterator */

static JSValue js_string_iterator_next(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv,
                                       int *pdone, int magic)
{
    JSArrayIteratorData *it;
    uint32_t idx, c, start;
    JSString *p;

    it = JS_GetOpaque2(ctx, this_val, JS_CLASS_STRING_ITERATOR);
    if (!it) {
        *pdone = false;
        return JS_EXCEPTION;
    }
    if (JS_IsUndefined(it->obj))
        goto done;
    p = JS_VALUE_GET_STRING(it->obj);
    idx = it->idx;
    if (idx >= p->len) {
        JS_FreeValue(ctx, it->obj);
        it->obj = JS_UNDEFINED;
    done:
        *pdone = true;
        return JS_UNDEFINED;
    }

    start = idx;
    c = string_getc(p, (int *)&idx);
    it->idx = idx;
    *pdone = false;
    if (c <= 0xffff) {
        return js_new_string_char(ctx, c);
    } else {
        return js_new_string16_len(ctx, str16(p) + start, 2);
    }
}

/* ES6 Annex B 2.3.2 etc. */
enum {
    magic_string_anchor,
    magic_string_big,
    magic_string_blink,
    magic_string_bold,
    magic_string_fixed,
    magic_string_fontcolor,
    magic_string_fontsize,
    magic_string_italics,
    magic_string_link,
    magic_string_small,
    magic_string_strike,
    magic_string_sub,
    magic_string_sup,
};

static JSValue js_string_CreateHTML(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv, int magic)
{
    JSValue str;
    JSString *p;
    StringBuffer b_s, *b = &b_s;
    static struct { const char *tag, *attr; } const defs[] = {
        { "a", "name" }, { "big", NULL }, { "blink", NULL }, { "b", NULL },
        { "tt", NULL }, { "font", "color" }, { "font", "size" }, { "i", NULL },
        { "a", "href" }, { "small", NULL }, { "strike", NULL },
        { "sub", NULL }, { "sup", NULL },
    };

    str = JS_ToStringCheckObject(ctx, this_val);
    if (JS_IsException(str))
        return JS_EXCEPTION;
    string_buffer_init(ctx, b, 7);
    string_buffer_putc8(b, '<');
    string_buffer_puts8(b, defs[magic].tag);
    if (defs[magic].attr) {
        // r += " " + attr + "=\"" + value + "\"";
        JSValue value;
        int i;

        string_buffer_putc8(b, ' ');
        string_buffer_puts8(b, defs[magic].attr);
        string_buffer_puts8(b, "=\"");
        value = JS_ToStringCheckObject(ctx, argv[0]);
        if (JS_IsException(value)) {
            JS_FreeValue(ctx, str);
            string_buffer_free(b);
            return JS_EXCEPTION;
        }
        p = JS_VALUE_GET_STRING(value);
        for (i = 0; i < p->len; i++) {
            int c = string_get(p, i);
            if (c == '"') {
                string_buffer_puts8(b, "&quot;");
            } else {
                string_buffer_putc16(b, c);
            }
        }
        JS_FreeValue(ctx, value);
        string_buffer_putc8(b, '\"');
    }
    // return r + ">" + str + "</" + tag + ">";
    string_buffer_putc8(b, '>');
    string_buffer_concat_value_free(b, str);
    string_buffer_puts8(b, "</");
    string_buffer_puts8(b, defs[magic].tag);
    string_buffer_putc8(b, '>');
    return string_buffer_end(b);
}

static const JSCFunctionListEntry js_string_funcs[] = {
    JS_CFUNC_DEF("fromCharCode", 1, js_string_fromCharCode ),
    JS_CFUNC_DEF("fromCodePoint", 1, js_string_fromCodePoint ),
    JS_CFUNC_DEF("raw", 1, js_string_raw ),
};

static const JSCFunctionListEntry js_string_proto_funcs[] = {
    JS_PROP_INT32_DEF("length", 0, JS_PROP_CONFIGURABLE ),
    JS_CFUNC_DEF("at", 1, js_string_at ),
    JS_CFUNC_DEF("charCodeAt", 1, js_string_charCodeAt ),
    JS_CFUNC_DEF("charAt", 1, js_string_charAt ),
    JS_CFUNC_DEF("concat", 1, js_string_concat ),
    JS_CFUNC_DEF("codePointAt", 1, js_string_codePointAt ),
    JS_CFUNC_DEF("isWellFormed", 0, js_string_isWellFormed ),
    JS_CFUNC_DEF("toWellFormed", 0, js_string_toWellFormed ),
    JS_CFUNC_MAGIC_DEF("indexOf", 1, js_string_indexOf, 0 ),
    JS_CFUNC_MAGIC_DEF("lastIndexOf", 1, js_string_indexOf, 1 ),
    JS_CFUNC_MAGIC_DEF("includes", 1, js_string_includes, 0 ),
    JS_CFUNC_MAGIC_DEF("endsWith", 1, js_string_includes, 2 ),
    JS_CFUNC_MAGIC_DEF("startsWith", 1, js_string_includes, 1 ),
    JS_CFUNC_MAGIC_DEF("match", 1, js_string_match, JS_ATOM_Symbol_match ),
    JS_CFUNC_MAGIC_DEF("matchAll", 1, js_string_match, JS_ATOM_Symbol_matchAll ),
    JS_CFUNC_MAGIC_DEF("search", 1, js_string_match, JS_ATOM_Symbol_search ),
    JS_CFUNC_DEF("split", 2, js_string_split ),
    JS_CFUNC_DEF("substring", 2, js_string_substring ),
    JS_CFUNC_DEF("substr", 2, js_string_substr ),
    JS_CFUNC_DEF("slice", 2, js_string_slice ),
    JS_CFUNC_DEF("repeat", 1, js_string_repeat ),
    JS_CFUNC_MAGIC_DEF("replace", 2, js_string_replace, 0 ),
    JS_CFUNC_MAGIC_DEF("replaceAll", 2, js_string_replace, 1 ),
    JS_CFUNC_MAGIC_DEF("padEnd", 1, js_string_pad, 1 ),
    JS_CFUNC_MAGIC_DEF("padStart", 1, js_string_pad, 0 ),
    JS_CFUNC_MAGIC_DEF("trim", 0, js_string_trim, 3 ),
    JS_CFUNC_MAGIC_DEF("trimEnd", 0, js_string_trim, 2 ),
    JS_ALIAS_DEF("trimRight", "trimEnd" ),
    JS_CFUNC_MAGIC_DEF("trimStart", 0, js_string_trim, 1 ),
    JS_ALIAS_DEF("trimLeft", "trimStart" ),
    JS_CFUNC_DEF("toString", 0, js_string_toString ),
    JS_CFUNC_DEF("valueOf", 0, js_string_toString ),
    JS_CFUNC_DEF("localeCompare", 1, js_string_localeCompare ),
    JS_CFUNC_DEF("normalize", 0, js_string_normalize ),
    JS_CFUNC_MAGIC_DEF("toLowerCase", 0, js_string_toLowerCase, 1 ),
    JS_CFUNC_MAGIC_DEF("toUpperCase", 0, js_string_toLowerCase, 0 ),
    JS_CFUNC_MAGIC_DEF("toLocaleLowerCase", 0, js_string_toLowerCase, 1 ),
    JS_CFUNC_MAGIC_DEF("toLocaleUpperCase", 0, js_string_toLowerCase, 0 ),
    JS_CFUNC_MAGIC_DEF("[Symbol.iterator]", 0, js_create_array_iterator, JS_ITERATOR_KIND_VALUE | 4 ),
    /* ES6 Annex B 2.3.2 etc. */
    JS_CFUNC_MAGIC_DEF("anchor", 1, js_string_CreateHTML, magic_string_anchor ),
    JS_CFUNC_MAGIC_DEF("big", 0, js_string_CreateHTML, magic_string_big ),
    JS_CFUNC_MAGIC_DEF("blink", 0, js_string_CreateHTML, magic_string_blink ),
    JS_CFUNC_MAGIC_DEF("bold", 0, js_string_CreateHTML, magic_string_bold ),
    JS_CFUNC_MAGIC_DEF("fixed", 0, js_string_CreateHTML, magic_string_fixed ),
    JS_CFUNC_MAGIC_DEF("fontcolor", 1, js_string_CreateHTML, magic_string_fontcolor ),
    JS_CFUNC_MAGIC_DEF("fontsize", 1, js_string_CreateHTML, magic_string_fontsize ),
    JS_CFUNC_MAGIC_DEF("italics", 0, js_string_CreateHTML, magic_string_italics ),
    JS_CFUNC_MAGIC_DEF("link", 1, js_string_CreateHTML, magic_string_link ),
    JS_CFUNC_MAGIC_DEF("small", 0, js_string_CreateHTML, magic_string_small ),
    JS_CFUNC_MAGIC_DEF("strike", 0, js_string_CreateHTML, magic_string_strike ),
    JS_CFUNC_MAGIC_DEF("sub", 0, js_string_CreateHTML, magic_string_sub ),
    JS_CFUNC_MAGIC_DEF("sup", 0, js_string_CreateHTML, magic_string_sup ),
};

static const JSCFunctionListEntry js_string_iterator_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_string_iterator_next, 0 ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "String Iterator", JS_PROP_CONFIGURABLE ),
};


/* Math */

/* precondition: a and b are not NaN */
static double js_fmin(double a, double b)
{
    if (a == 0 && b == 0) {
        JSFloat64Union a1, b1;
        a1.d = a;
        b1.d = b;
        a1.u64 |= b1.u64;
        return a1.d;
    } else {
        return a < b ? a : b;
    }
}

/* precondition: a and b are not NaN */
static double js_fmax(double a, double b)
{
    if (a == 0 && b == 0) {
        JSFloat64Union a1, b1;
        a1.d = a;
        b1.d = b;
        a1.u64 &= b1.u64;
        return a1.d;
    } else {
        return a < b ? b : a;
    }
}

static JSValue js_math_min_max(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv, int magic)
{
    bool is_max = magic;
    double r, a;
    int i;
    uint32_t tag;

    if (unlikely(argc == 0)) {
        return js_float64(is_max ? -INFINITY : INFINITY);
    }

    tag = JS_VALUE_GET_TAG(argv[0]);
    if (tag == JS_TAG_INT) {
        int a1, r1 = JS_VALUE_GET_INT(argv[0]);
        for(i = 1; i < argc; i++) {
            tag = JS_VALUE_GET_TAG(argv[i]);
            if (tag != JS_TAG_INT) {
                r = r1;
                goto generic_case;
            }
            a1 = JS_VALUE_GET_INT(argv[i]);
            if (is_max)
                r1 = max_int(r1, a1);
            else
                r1 = min_int(r1, a1);

        }
        return js_int32(r1);
    } else {
        if (JS_ToFloat64(ctx, &r, argv[0]))
            return JS_EXCEPTION;
        i = 1;
    generic_case:
        while (i < argc) {
            if (JS_ToFloat64(ctx, &a, argv[i]))
                return JS_EXCEPTION;
            if (!isnan(r)) {
                if (isnan(a)) {
                    r = a;
                } else {
                    if (is_max)
                        r = js_fmax(r, a);
                    else
                        r = js_fmin(r, a);
                }
            }
            i++;
        }
        return js_number(r);
    }
}

static double js_math_sign(double a)
{
    if (isnan(a) || a == 0.0)
        return a;
    if (a < 0)
        return -1;
    else
        return 1;
}

static double js_math_round(double a)
{
    JSFloat64Union u;
    uint64_t frac_mask, one;
    unsigned int e, s;

    u.d = a;
    e = (u.u64 >> 52) & 0x7ff;
    if (e < 1023) {
        /* abs(a) < 1 */
        if (e == (1023 - 1) && u.u64 != 0xbfe0000000000000) {
            /* abs(a) > 0.5 or a = 0.5: return +/-1.0 */
            u.u64 = (u.u64 & ((uint64_t)1 << 63)) | ((uint64_t)1023 << 52);
        } else {
            /* return +/-0.0 */
            u.u64 &= (uint64_t)1 << 63;
        }
    } else if (e < (1023 + 52)) {
        s = u.u64 >> 63;
        one = (uint64_t)1 << (52 - (e - 1023));
        frac_mask = one - 1;
        u.u64 += (one >> 1) - s;
        u.u64 &= ~frac_mask; /* truncate to an integer */
    }
    /* otherwise: abs(a) >= 2^52, or NaN, +/-Infinity: no change */
    return u.d;
}

static JSValue js_math_hypot(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    double r, a;
    int i;

    r = 0;
    if (argc > 0) {
        if (JS_ToFloat64(ctx, &r, argv[0]))
            return JS_EXCEPTION;
        if (argc == 1) {
            r = fabs(r);
        } else {
            /* use the built-in function to minimize precision loss */
            for (i = 1; i < argc; i++) {
                if (JS_ToFloat64(ctx, &a, argv[i]))
                    return JS_EXCEPTION;
                r = hypot(r, a);
            }
        }
    }
    return js_float64(r);
}

static double js_math_f16round(double a)
{
    return fromfp16(tofp16(a));
}

static double js_math_fround(double a)
{
    return (float)a;
}

static JSValue js_math_imul(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv)
{
    uint32_t a, b, c;
    int32_t d;

    if (JS_ToUint32(ctx, &a, argv[0]))
        return JS_EXCEPTION;
    if (JS_ToUint32(ctx, &b, argv[1]))
        return JS_EXCEPTION;
    c = a * b;
    memcpy(&d, &c, sizeof(d));
    return js_int32(d);
}

static JSValue js_math_clz32(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    uint32_t a, r;

    if (JS_ToUint32(ctx, &a, argv[0]))
        return JS_EXCEPTION;
    if (a == 0)
        r = 32;
    else
        r = clz32(a);
    return js_int32(r);
}

/* we add one extra limb to avoid having to test for overflows during the sum */
#define SUM_PRECISE_ACC_LEN 34

typedef enum {
    SUM_PRECISE_STATE_MINUS_ZERO,
    SUM_PRECISE_STATE_FINITE,
    SUM_PRECISE_STATE_INFINITY,
    SUM_PRECISE_STATE_MINUS_INFINITY, /* must be after SUM_PRECISE_STATE_INFINITY */
    SUM_PRECISE_STATE_NAN, /* must be after SUM_PRECISE_STATE_MINUS_INFINITY */
} SumPreciseStateEnum;

typedef struct {
    uint64_t acc[SUM_PRECISE_ACC_LEN];
    int n_limbs; /* acc is not necessarily normalized */
    SumPreciseStateEnum state;
} SumPreciseState;

static void sum_precise_init(SumPreciseState *s)
{
    s->state = SUM_PRECISE_STATE_MINUS_ZERO;
    s->acc[0] = 0;
    s->n_limbs = 1;
}

#define ADDC64(res, carry_out, op1, op2, carry_in)      \
do {                                                    \
    uint64_t __v, __a, __k, __k1;                       \
    __v = (op1);                                        \
    __a = __v + (op2);                                  \
    __k1 = __a < __v;                                   \
    __k = (carry_in);                                   \
    __a = __a + __k;                                    \
    carry_out = (__a < __k) | __k1;                     \
    res = __a;                                          \
} while (0)

static void sum_precise_add(SumPreciseState *s, double d)
{
    uint64_t a, m, a0, carry, acc_sign, a_sign;
    int sgn, e, p, n, i;
    unsigned shift;

    a = float64_as_uint64(d);
    sgn = a >> 63;
    e = (a >> 52) & ((1 << 11) - 1);
    m = a & (((uint64_t)1 << 52) - 1);
    if (unlikely(e == 2047)) {
        if (m == 0) {
            /* +/- infinity */
            if (s->state == SUM_PRECISE_STATE_NAN ||
                (s->state == SUM_PRECISE_STATE_MINUS_INFINITY && !sgn) ||
                (s->state == SUM_PRECISE_STATE_INFINITY && sgn)) {
                s->state = SUM_PRECISE_STATE_NAN;
            } else {
                s->state = SUM_PRECISE_STATE_INFINITY + sgn;
            }
        } else {
            /* NaN */
            s->state = SUM_PRECISE_STATE_NAN;
        }
    } else if (e == 0) {
        if (likely(m == 0)) {
            /* zero */
            if (s->state == SUM_PRECISE_STATE_MINUS_ZERO && !sgn)
                s->state = SUM_PRECISE_STATE_FINITE;
        } else {
            /* subnormal */
            p = 0;
            shift = 0;
            goto add;
        }
    } else {
        m |= (uint64_t)1 << 52;
        shift = e - 1;
        p = shift / 64;
        /* 'p' is the position of a0 in acc */
        shift %= 64;
    add:
        if (s->state >= SUM_PRECISE_STATE_INFINITY)
            return;
        s->state = SUM_PRECISE_STATE_FINITE;
        n = s->n_limbs;

        acc_sign = (int64_t)s->acc[n - 1] >> 63;

        /* sign extend acc */
        for(i = n; i <= p; i++)
            s->acc[i] = acc_sign;

        carry = sgn;
        a_sign = -sgn;
        a0 = m << shift;
        ADDC64(s->acc[p], carry, s->acc[p], a0 ^ a_sign, carry);
        if (shift >= 12) {
            p++;
            if (p >= n)
                s->acc[p] = acc_sign;
            a0 = m >> (64 - shift);
            ADDC64(s->acc[p], carry, s->acc[p], a0 ^ a_sign, carry);
        }
        p++;
        if (p >= n) {
            n = p;
        } else {
            /* carry */
            for(i = p; i < n; i++) {
                /* if 'a' positive: stop condition: carry = 0.
                   if 'a' negative: stop condition: carry = 1. */
                if (carry == sgn)
                    goto done;
                ADDC64(s->acc[i], carry, s->acc[i], a_sign, carry);
            }
        }

        /* extend the accumulator if needed */
        a0 = carry + acc_sign + a_sign;
        /* -1 <= a0 <= 1 (if both acc and a are negative, carry is set) */
        if (a0 != ((int64_t)s->acc[n - 1] >> 63)) {
            s->acc[n++] = a0;
        }
    done:
        s->n_limbs = n;
    }
}

static double sum_precise_get_result(SumPreciseState *s)
{
    int n, shift, e, p, is_neg, i;
    uint64_t m, addend, carry;

    if (s->state != SUM_PRECISE_STATE_FINITE) {
        switch(s->state) {
        default:
        case SUM_PRECISE_STATE_MINUS_ZERO:
            return -0.0;
        case SUM_PRECISE_STATE_INFINITY:
            return INFINITY;
        case SUM_PRECISE_STATE_MINUS_INFINITY:
            return -INFINITY;
        case SUM_PRECISE_STATE_NAN:
            return NAN;
        }
    }

    /* extract the sign and absolute value */
    n = s->n_limbs;
    is_neg = s->acc[n - 1] >> 63;
    if (is_neg) {
        /* acc = -acc */
        carry = 1;
        for(i = 0; i < n; i++) {
            ADDC64(s->acc[i], carry, ~s->acc[i], 0, carry);
        }
    }
    /* normalize */
    while (n > 0 && s->acc[n - 1] == 0)
        n--;
    /* zero result. The spec tells it is always positive in the finite case */
    if (n == 0)
        return 0.0;
    /* subnormal case */
    if (n == 1 && s->acc[0] < ((uint64_t)1 << 52))
        return uint64_as_float64(((uint64_t)is_neg << 63) | s->acc[0]);
    /* normal case */
    e = n * 64;
    p = n - 1;
    m = s->acc[p];
    shift = clz64(m);
    e = e - shift - 52;
    if (shift != 0) {
        m <<= shift;
        if (p > 0) {
            int shift1;
            uint64_t nz;
            p--;
            shift1 = 64 - shift;
            nz = s->acc[p] & (((uint64_t)1 << shift1) - 1);
            m = m | (s->acc[p] >> shift1) | (nz != 0);
        }
    }
    if ((m & ((1 << 10) - 1)) == 0) {
        /* see if the LSB part is non zero for the final rounding  */
        while (p > 0) {
            p--;
            if (s->acc[p] != 0) {
                m |= 1;
                break;
            }
        }
    }
    /* rounding to nearest with ties to even */
    addend = (1 << 10) - 1 + ((m >> 11) & 1);
    m = (m + addend) >> 11;
    /* handle overflow in the rounding */
    if (m == 0)
        e++;
    if (unlikely(e >= 2047)) {
        /* infinity */
        return uint64_as_float64(((uint64_t)is_neg << 63) | ((uint64_t)2047 << 52));
    } else {
        m &= (((uint64_t)1 << 52) - 1);
        return uint64_as_float64(((uint64_t)is_neg << 63) | ((uint64_t)e << 52) | m);
    }
}

static JSValue js_math_sumPrecise(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    JSValue iter, next, item, ret;
    uint32_t tag;
    int done;
    double d;
    SumPreciseState s_s, *s = &s_s;

    iter = JS_GetIterator(ctx, argv[0], /*is_async*/false);
    if (JS_IsException(iter))
        return JS_EXCEPTION;
    ret = JS_EXCEPTION;
    next = JS_GetProperty(ctx, iter, JS_ATOM_next);
    if (JS_IsException(next))
        goto fail;
    sum_precise_init(s);
    for (;;) {
        item = JS_IteratorNext(ctx, iter, next, 0, NULL, &done);
        if (JS_IsException(item))
            goto fail;
        if (done)
            break;
        tag = JS_VALUE_GET_TAG(item);
        if (JS_TAG_IS_FLOAT64(tag)) {
            d = JS_VALUE_GET_FLOAT64(item);
        } else if (tag == JS_TAG_INT) {
            d = JS_VALUE_GET_INT(item);
        } else {
            JS_FreeValue(ctx, item);
            JS_ThrowTypeError(ctx, "not a number");
            JS_IteratorClose(ctx, iter, /*is_exception_pending*/true);
            goto fail;
        }
        sum_precise_add(s, d);
    }
    ret = js_float64(sum_precise_get_result(s));
fail:
    JS_FreeValue(ctx, iter);
    JS_FreeValue(ctx, next);
    return ret;
}

/* xorshift* random number generator by Marsaglia */
static uint64_t xorshift64star(uint64_t *pstate)
{
    uint64_t x;
    x = *pstate;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *pstate = x;
    return x * 0x2545F4914F6CDD1D;
}

static void js_random_init(JSContext *ctx)
{
    ctx->random_state = js__gettimeofday_us();
    /* the state must be non zero */
    if (ctx->random_state == 0)
        ctx->random_state = 1;
}

static JSValue js_math_random(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    JSFloat64Union u;
    uint64_t v;

    v = xorshift64star(&ctx->random_state);
    /* 1.0 <= u.d < 2 */
    u.u64 = ((uint64_t)0x3ff << 52) | (v >> 12);
    return js_float64(u.d - 1.0);
}

/* use local wrappers for math functions to
   - avoid initializing data with dynamic library entry points.
   - avoid some overhead if the call can be inlined at compile or link time.
 */
static double js_math_fabs(double d) { return fabs(d); }
static double js_math_floor(double d) { return floor(d); }
static double js_math_ceil(double d) { return ceil(d); }
static double js_math_sqrt(double d) { return sqrt(d); }
static double js_math_acos(double d) { return acos(d); }
static double js_math_asin(double d) { return asin(d); }
static double js_math_atan(double d) { return atan(d); }
static double js_math_atan2(double a, double b) { return atan2(a, b); }
static double js_math_cos(double d) { return cos(d); }
static double js_math_exp(double d) { return exp(d); }
static double js_math_log(double d) { return log(d); }
static double js_math_sin(double d) { return sin(d); }
static double js_math_tan(double d) { return tan(d); }
static double js_math_trunc(double d) { return trunc(d); }
static double js_math_cosh(double d) { return cosh(d); }
static double js_math_sinh(double d) { return sinh(d); }
static double js_math_tanh(double d) { return tanh(d); }
static double js_math_acosh(double d) { return acosh(d); }
static double js_math_asinh(double d) { return asinh(d); }
static double js_math_atanh(double d) { return atanh(d); }
static double js_math_expm1(double d) { return expm1(d); }
static double js_math_log1p(double d) { return log1p(d); }
static double js_math_log2(double d) { return log2(d); }
static double js_math_log10(double d) { return log10(d); }
static double js_math_cbrt(double d) { return cbrt(d); }

static const JSCFunctionListEntry js_math_funcs[] = {
    JS_CFUNC_MAGIC_DEF("min", 2, js_math_min_max, 0 ),
    JS_CFUNC_MAGIC_DEF("max", 2, js_math_min_max, 1 ),
    JS_CFUNC_SPECIAL_DEF("abs", 1, f_f, js_math_fabs ),
    JS_CFUNC_SPECIAL_DEF("floor", 1, f_f, js_math_floor ),
    JS_CFUNC_SPECIAL_DEF("ceil", 1, f_f, js_math_ceil ),
    JS_CFUNC_SPECIAL_DEF("round", 1, f_f, js_math_round ),
    JS_CFUNC_SPECIAL_DEF("sqrt", 1, f_f, js_math_sqrt ),

    JS_CFUNC_SPECIAL_DEF("acos", 1, f_f, js_math_acos ),
    JS_CFUNC_SPECIAL_DEF("asin", 1, f_f, js_math_asin ),
    JS_CFUNC_SPECIAL_DEF("atan", 1, f_f, js_math_atan ),
    JS_CFUNC_SPECIAL_DEF("atan2", 2, f_f_f, js_math_atan2 ),
    JS_CFUNC_SPECIAL_DEF("cos", 1, f_f, js_math_cos ),
    JS_CFUNC_SPECIAL_DEF("exp", 1, f_f, js_math_exp ),
    JS_CFUNC_SPECIAL_DEF("log", 1, f_f, js_math_log ),
    JS_CFUNC_SPECIAL_DEF("pow", 2, f_f_f, js_math_pow ),
    JS_CFUNC_SPECIAL_DEF("sin", 1, f_f, js_math_sin ),
    JS_CFUNC_SPECIAL_DEF("tan", 1, f_f, js_math_tan ),
    /* ES6 */
    JS_CFUNC_SPECIAL_DEF("trunc", 1, f_f, js_math_trunc ),
    JS_CFUNC_SPECIAL_DEF("sign", 1, f_f, js_math_sign ),
    JS_CFUNC_SPECIAL_DEF("cosh", 1, f_f, js_math_cosh ),
    JS_CFUNC_SPECIAL_DEF("sinh", 1, f_f, js_math_sinh ),
    JS_CFUNC_SPECIAL_DEF("tanh", 1, f_f, js_math_tanh ),
    JS_CFUNC_SPECIAL_DEF("acosh", 1, f_f, js_math_acosh ),
    JS_CFUNC_SPECIAL_DEF("asinh", 1, f_f, js_math_asinh ),
    JS_CFUNC_SPECIAL_DEF("atanh", 1, f_f, js_math_atanh ),
    JS_CFUNC_SPECIAL_DEF("expm1", 1, f_f, js_math_expm1 ),
    JS_CFUNC_SPECIAL_DEF("log1p", 1, f_f, js_math_log1p ),
    JS_CFUNC_SPECIAL_DEF("log2", 1, f_f, js_math_log2 ),
    JS_CFUNC_SPECIAL_DEF("log10", 1, f_f, js_math_log10 ),
    JS_CFUNC_SPECIAL_DEF("cbrt", 1, f_f, js_math_cbrt ),
    JS_CFUNC_DEF("hypot", 2, js_math_hypot ),
    JS_CFUNC_DEF("random", 0, js_math_random ),
    JS_CFUNC_SPECIAL_DEF("f16round", 1, f_f, js_math_f16round ),
    JS_CFUNC_SPECIAL_DEF("fround", 1, f_f, js_math_fround ),
    JS_CFUNC_DEF("imul", 2, js_math_imul ),
    JS_CFUNC_DEF("clz32", 1, js_math_clz32 ),
    JS_CFUNC_DEF("sumPrecise", 1, js_math_sumPrecise ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Math", JS_PROP_CONFIGURABLE ),
    JS_PROP_DOUBLE_DEF("E", 2.718281828459045, 0 ),
    JS_PROP_DOUBLE_DEF("LN10", 2.302585092994046, 0 ),
    JS_PROP_DOUBLE_DEF("LN2", 0.6931471805599453, 0 ),
    JS_PROP_DOUBLE_DEF("LOG2E", 1.4426950408889634, 0 ),
    JS_PROP_DOUBLE_DEF("LOG10E", 0.4342944819032518, 0 ),
    JS_PROP_DOUBLE_DEF("PI", 3.141592653589793, 0 ),
    JS_PROP_DOUBLE_DEF("SQRT1_2", 0.7071067811865476, 0 ),
    JS_PROP_DOUBLE_DEF("SQRT2", 1.4142135623730951, 0 ),
};

static const JSCFunctionListEntry js_math_obj[] = {
    JS_OBJECT_DEF("Math", js_math_funcs, countof(js_math_funcs), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE ),
};

/* Date */

/* OS dependent. d = argv[0] is in ms from 1970. Return the difference
   between UTC time and local time 'd' in minutes */
static int getTimezoneOffset(int64_t time) {
#if defined(_WIN32)
    DWORD r;
    TIME_ZONE_INFORMATION t;
    r = GetTimeZoneInformation(&t);
    if (r == TIME_ZONE_ID_INVALID)
        return 0;
    if (r == TIME_ZONE_ID_DAYLIGHT)
         return (int)(t.Bias + t.DaylightBias);
    return (int)t.Bias;
#else
    time_t ti;
    struct tm tm;

    time /= 1000; /* convert to seconds */
    if (sizeof(time_t) == 4) {
        /* on 32-bit systems, we need to clamp the time value to the
           range of `time_t`. This is better than truncating values to
           32 bits and hopefully provides the same result as 64-bit
           implementation of localtime_r.
         */
        if ((time_t)-1 < 0) {
            if (time < INT32_MIN) {
                time = INT32_MIN;
            } else if (time > INT32_MAX) {
                time = INT32_MAX;
            }
        } else {
            if (time < 0) {
                time = 0;
            } else if (time > UINT32_MAX) {
                time = UINT32_MAX;
            }
        }
    }
    ti = time;
    localtime_r(&ti, &tm);
#ifdef NO_TM_GMTOFF
    struct tm gmt;
    gmtime_r(&ti, &gmt);

    /* disable DST adjustment on the local tm struct */
    tm.tm_isdst = 0;

    return (int)difftime(mktime(&gmt), mktime(&tm)) / 60;
#else
    return -tm.tm_gmtoff / 60;
#endif /* NO_TM_GMTOFF */
#endif
}

