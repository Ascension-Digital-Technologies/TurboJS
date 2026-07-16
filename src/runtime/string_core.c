/* Engine domain source: runtime/classes_strings.inc -> string_core.
 * Ownership: runtime subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static int string_buffer_concat(StringBuffer *s, JSString *p,
                                uint32_t from, uint32_t to)
{
    if (to <= from)
        return 0;
    if (p->is_wide_char)
        return string_buffer_write16(s, str16(p) + from, to - from);
    else
        return string_buffer_write8(s, str8(p) + from, to - from);
}

static int string_buffer_concat_value(StringBuffer *s, JSValueConst v)
{
    JSString *p;
    JSValue v1;
    int res;
    int tag;

    if (s->error_status) {
        /* prevent exception overload */
        return -1;
    }
    tag = JS_VALUE_GET_TAG(v);
    if (tag == JS_TAG_STRING_ROPE) {
        /* recursively concatenate rope children */
        JSStringRope *r = JS_VALUE_GET_STRING_ROPE(v);
        if (string_buffer_concat_value(s, r->left))
            return -1;
        return string_buffer_concat_value(s, r->right);
    }
    if (unlikely(tag != JS_TAG_STRING)) {
        v1 = JS_ToString(s->ctx, v);
        if (JS_IsException(v1))
            return string_buffer_set_error(s);
        p = JS_VALUE_GET_STRING(v1);
        res = string_buffer_concat(s, p, 0, p->len);
        JS_FreeValue(s->ctx, v1);
        return res;
    }
    p = JS_VALUE_GET_STRING(v);
    return string_buffer_concat(s, p, 0, p->len);
}

static int string_buffer_concat_value_free(StringBuffer *s, JSValue v)
{
    JSString *p;
    int res;
    int tag;

    if (s->error_status) {
        /* prevent exception overload */
        JS_FreeValue(s->ctx, v);
        return -1;
    }
    tag = JS_VALUE_GET_TAG(v);
    if (tag == JS_TAG_STRING_ROPE) {
        /* concatenate rope (don't free since concat_value doesn't free) */
        res = string_buffer_concat_value(s, v);
        JS_FreeValue(s->ctx, v);
        return res;
    }
    if (unlikely(tag != JS_TAG_STRING)) {
        v = JS_ToStringFree(s->ctx, v);
        if (JS_IsException(v))
            return string_buffer_set_error(s);
    }
    p = JS_VALUE_GET_STRING(v);
    res = string_buffer_concat(s, p, 0, p->len);
    JS_FreeValue(s->ctx, v);
    return res;
}

static int string_buffer_fill(StringBuffer *s, int c, int count)
{
    /* XXX: optimize */
    if (s->len + count > s->size) {
        if (string_buffer_realloc(s, s->len + count, c))
            return -1;
    }
    while (count-- > 0) {
        if (string_buffer_putc16(s, c))
            return -1;
    }
    return 0;
}

static JSValue string_buffer_end(StringBuffer *s)
{
    JSString *str;
    str = s->str;
    if (s->error_status)
        return JS_EXCEPTION;
    if (s->len == 0) {
        js_free(s->ctx, str);
        s->str = NULL;
        return js_empty_string(s->ctx->rt);
    }
    if (s->len < s->size) {
        /* smaller size so js_realloc should not fail, but OK if it does */
        /* XXX: should add some slack to avoid unnecessary calls */
        /* XXX: might need to use malloc+free to ensure smaller size */
        str = js_realloc_rt(s->ctx->rt, str, sizeof(JSString) +
                            (s->len << s->is_wide_char) + 1 - s->is_wide_char);
        if (str == NULL)
            str = s->str;
        s->str = str;
    }
    if (!s->is_wide_char)
        str8(str)[s->len] = 0;
#ifdef ENABLE_DUMPS // JS_DUMP_LEAKS
    list_add_tail(&str->link, &s->ctx->rt->string_list);
#endif
    str->is_wide_char = s->is_wide_char;
    str->len = s->len;
    s->str = NULL;
    return JS_MKPTR(JS_TAG_STRING, str);
}

/* create a string from a UTF-8 buffer */
JSValue JS_NewStringLen(JSContext *ctx, const char *buf, size_t buf_len)
{
    JSString *str;
    size_t len;
    int kind;

    if (unlikely(buf_len <= 0))
        return js_empty_string(ctx->rt);

    /* Compute string kind and length: 7-bit, 8-bit, 16-bit, 16-bit UTF-16 */
    kind = utf8_scan(buf, buf_len, &len);
    if (unlikely(len > JS_STRING_LEN_MAX))
        return JS_ThrowRangeError(ctx, "invalid string length");

    switch (kind) {
    case UTF8_PLAIN_ASCII:
        str = js_alloc_string(ctx, len, 0);
        if (unlikely(!str))
            return JS_EXCEPTION;
        memcpy(str8(str), buf, len);
        str8(str)[len] = '\0';
        break;
    case UTF8_NON_ASCII:
        /* buf contains non-ASCII code-points, but limited to 8-bit values */
        str = js_alloc_string(ctx, len, 0);
        if (unlikely(!str))
            return JS_EXCEPTION;
        utf8_decode_buf8(str8(str), len + 1, buf, buf_len);
        break;
    default:
        // This causes a potential problem in JS_ThrowError if message is invalid
        //if (kind & UTF8_HAS_ERRORS)
        //    return JS_ThrowRangeError(ctx, "invalid UTF-8 sequence");
        str = js_alloc_string(ctx, len, 1);
        if (unlikely(!str))
            return JS_EXCEPTION;
        utf8_decode_buf16(str16(str), len, buf, buf_len);
        break;
    }
    return JS_MKPTR(JS_TAG_STRING, str);
}

JSValue JS_NewStringUTF16(JSContext *ctx, const uint16_t *buf, size_t len)
{
    JSString *str;

    if (unlikely(!len))
        return js_empty_string(ctx->rt);
    if (unlikely(len > JS_STRING_LEN_MAX))
        return JS_ThrowRangeError(ctx, "invalid string length");

    str = js_alloc_string(ctx, len, 1);
    if (unlikely(!str))
        return JS_EXCEPTION;
    memcpy(str16(str), buf, len * sizeof(*buf));
    return JS_MKPTR(JS_TAG_STRING, str);
}

static JSValue JS_ConcatString3(JSContext *ctx, const char *str1,
                                JSValue str2, const char *str3)
{
    StringBuffer b_s, *b = &b_s;
    int len1, len3;
    JSString *p;

    if (unlikely(JS_VALUE_GET_TAG(str2) != JS_TAG_STRING)) {
        str2 = JS_ToStringFree(ctx, str2);
        if (JS_IsException(str2))
            goto fail;
    }
    p = JS_VALUE_GET_STRING(str2);
    len1 = strlen(str1);
    len3 = strlen(str3);

    if (string_buffer_init2(ctx, b, len1 + p->len + len3, p->is_wide_char))
        goto fail;

    string_buffer_write8(b, (const uint8_t *)str1, len1);
    string_buffer_concat(b, p, 0, p->len);
    string_buffer_write8(b, (const uint8_t *)str3, len3);

    JS_FreeValue(ctx, str2);
    return string_buffer_end(b);

 fail:
    JS_FreeValue(ctx, str2);
    return JS_EXCEPTION;
}

/* `str` may be pure ASCII or UTF-8 encoded */
JSValue JS_NewAtomString(JSContext *ctx, const char *str)
{
    JSAtom atom = JS_NewAtom(ctx, str);
    if (atom == JS_ATOM_NULL)
        return JS_EXCEPTION;
    JSValue val = JS_AtomToString(ctx, atom);
    JS_FreeAtom(ctx, atom);
    return val;
}

static JSValue js_force_tostring(JSContext *ctx, JSValueConst val1)
{
    JSObject *p;
    JSValue val;

    if (JS_VALUE_GET_TAG(val1) == JS_TAG_STRING)
        return js_dup(val1);
    val = JS_ToString(ctx, val1);
    if (!JS_IsException(val))
        return val;
    // Stringification can fail when there is an exception pending,
    // e.g. a stack overflow InternalError. Special-case exception
    // objects to make debugging easier, look up the .message property
    // and stringify that.
    if (JS_VALUE_GET_TAG(val1) != JS_TAG_OBJECT)
        return JS_EXCEPTION;
    p = JS_VALUE_GET_OBJ(val1);
    if (p->class_id != JS_CLASS_ERROR)
        return JS_EXCEPTION;
    val = JS_GetProperty(ctx, val1, JS_ATOM_message);
    if (JS_VALUE_GET_TAG(val) != JS_TAG_STRING) {
        JS_FreeValue(ctx, val);
        return JS_EXCEPTION;
    }
    return val;
}

/* return (NULL, 0) if exception. */
/* return pointer into a JSString with a live ref_count */
/* cesu8 determines if non-BMP1 codepoints are encoded as 1 or 2 utf-8 sequences */
const char *JS_ToCStringLen2(JSContext *ctx, size_t *plen, JSValueConst val1,
                             bool cesu8)
{
    JSValue val;
    JSString *str, *str_new;
    int pos, len, c, c1;
    uint8_t *q;

    val = js_force_tostring(ctx, val1);
    if (JS_IsException(val))
        goto fail;
    str = JS_VALUE_GET_STRING(val);
    len = str->len;
    if (!str->is_wide_char) {
        const uint8_t *src = str8(str);
        int count;

        /* count the number of non-ASCII characters */
        /* Scanning the whole string is required for ASCII strings,
           and computing the number of non-ASCII bytes is less expensive
           than testing each byte, hence this method is faster for ASCII
           strings, which is the most common case.
         */
        count = 0;
        for (pos = 0; pos < len; pos++) {
            count += src[pos] >> 7;
        }
        if (count == 0 && str->kind == JS_STRING_KIND_NORMAL) {
            if (plen)
                *plen = len;
            return (const char *)src;
        }
        str_new = js_alloc_string(ctx, len + count, 0);
        if (!str_new)
            goto fail;
        q = str8(str_new);
        for (pos = 0; pos < len; pos++) {
            c = src[pos];
            if (c < 0x80) {
                *q++ = c;
            } else {
                *q++ = (c >> 6) | 0xc0;
                *q++ = (c & 0x3f) | 0x80;
            }
        }
    } else {
        const uint16_t *src = str16(str);
        /* Allocate 3 bytes per 16 bit code point. Surrogate pairs may
           produce 4 bytes but use 2 code points.
         */
        str_new = js_alloc_string(ctx, len * 3, 0);
        if (!str_new)
            goto fail;
        q = str8(str_new);
        pos = 0;
        while (pos < len) {
            c = src[pos++];
            if (c < 0x80) {
                *q++ = c;
            } else {
                if (is_hi_surrogate(c)) {
                    if (pos < len && !cesu8) {
                        c1 = src[pos];
                        if (is_lo_surrogate(c1)) {
                            pos++;
                            c = from_surrogate(c, c1);
                        } else {
                            /* Keep unmatched surrogate code points */
                            /* c = 0xfffd; */ /* error */
                        }
                    } else {
                        /* Keep unmatched surrogate code points */
                        /* c = 0xfffd; */ /* error */
                    }
                }
                q += utf8_encode(q, c);
            }
        }
    }

    *q = '\0';
    str_new->len = q - str8(str_new);
    JS_FreeValue(ctx, val);
    if (plen)
        *plen = str_new->len;
    return (const char *)str8(str_new);
fail:
    if (plen)
        *plen = 0;
    return NULL;
}

const uint16_t *JS_ToCStringLenUTF16(JSContext *ctx, size_t *plen,
                                     JSValueConst val1)
{
    JSString *p, *q;
    uint32_t i;
    JSValue v;

    v = js_force_tostring(ctx, val1);
    if (JS_IsException(v))
        goto fail;
    p = JS_VALUE_GET_STRING(v);
    if (!p->is_wide_char) {
        q = js_alloc_string(ctx, p->len, /*is_wide_char*/true);
        if (!q)
            goto fail;
        for (i = 0; i < p->len; i++)
            str16(q)[i] = str8(p)[i];
        JS_FreeValue(ctx, v);
        p = q;
    }
    if (plen)
        *plen = p->len;
    return str16(p);
fail:
    JS_FreeValue(ctx, v);
    if (plen)
        *plen = 0;
    return NULL;
}

static void js_free_cstring(JSRuntime *rt, const void *ptr)
{
    if (!ptr)
        return;
    /* purposely removing constness */
    JS_FreeValueRT(rt, JS_MKPTR(JS_TAG_STRING, (JSString *)ptr - 1));
}

void JS_FreeCString(JSContext *ctx, const char *ptr)
{
    return js_free_cstring(ctx->rt, ptr);
}

void JS_FreeCStringRT(JSRuntime *rt, const char *ptr)
{
    return js_free_cstring(rt, ptr);
}

void JS_FreeCStringUTF16(JSContext *ctx, const uint16_t *ptr)
{
    return js_free_cstring(ctx->rt, ptr);
}

void JS_FreeCStringRT_UTF16(JSRuntime *rt, const uint16_t *ptr)
{
    return js_free_cstring(rt, ptr);
}

static int memcmp16_8(const uint16_t *src1, const uint8_t *src2, int len)
{
    int c, i;
    for(i = 0; i < len; i++) {
        c = src1[i] - src2[i];
        if (c != 0)
            return c;
    }
    return 0;
}

static int memcmp16(const uint16_t *src1, const uint16_t *src2, int len)
{
    int c, i;
    for(i = 0; i < len; i++) {
        c = src1[i] - src2[i];
        if (c != 0)
            return c;
    }
    return 0;
}

static int js_string_memcmp(JSString *p1, JSString *p2, int len)
{
    int res;

    if (likely(!p1->is_wide_char)) {
        if (likely(!p2->is_wide_char))
            res = memcmp(str8(p1), str8(p2), len);
        else
            res = -memcmp16_8(str16(p2), str8(p1), len);
    } else {
        if (!p2->is_wide_char)
            res = memcmp16_8(str16(p1), str8(p2), len);
        else
            res = memcmp16(str16(p1), str16(p2), len);
    }
    return res;
}

static bool js_string_eq(JSString *p1, JSString *p2) {
    if (p1->len != p2->len)
        return false;
    return js_string_memcmp(p1, p2, p1->len) == 0;
}

/* return < 0, 0 or > 0 */
static int js_string_compare(JSString *p1, JSString *p2)
{
    int res, len;
    len = min_int(p1->len, p2->len);
    res = js_string_memcmp(p1, p2, len);
    if (res == 0)
        res = compare_u32(p1->len, p2->len);
    return res;
}

/* Rope string support functions */

static inline bool tag_is_string(int tag)
{
    return tag == JS_TAG_STRING || tag == JS_TAG_STRING_ROPE;
}

static uint32_t string_rope_get_len(JSValueConst val)
{
    if (JS_VALUE_GET_TAG(val) == JS_TAG_STRING)
        return JS_VALUE_GET_STRING(val)->len;
    else
        return JS_VALUE_GET_STRING_ROPE(val)->len;
}

