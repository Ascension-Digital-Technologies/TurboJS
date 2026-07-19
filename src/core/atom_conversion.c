/* Engine domain source: runtime/atoms.inc -> atom_conversion.
 * Ownership: runtime subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static void JS_FreeAtomStruct(JSRuntime *rt, JSAtomStruct *p)
{
    uint32_t i = p->hash_next;  /* atom_index */
    if (p->atom_type != JS_ATOM_TYPE_SYMBOL) {
        JSAtomStruct *p0, *p1;
        uint32_t h0;

        h0 = p->hash & (rt->atom_hash_size - 1);
        i = rt->atom_hash[h0];
        p1 = rt->atom_array[i];
        if (p1 == p) {
            rt->atom_hash[h0] = p1->hash_next;
        } else {
            for(;;) {
                assert(i != 0);
                p0 = p1;
                i = p1->hash_next;
                p1 = rt->atom_array[i];
                if (p1 == p) {
                    p0->hash_next = p1->hash_next;
                    break;
                }
            }
        }
    }
    /* insert in free atom list */
    rt->atom_array[i] = atom_set_free(rt->atom_free_index);
    rt->atom_free_index = i;
    if (unlikely(p->first_weak_ref)) {
        reset_weak_ref(rt, &p->first_weak_ref);
    }
    /* free the string structure */
#ifdef ENABLE_DUMPS // JS_DUMP_LEAKS
    list_del(&p->link);
#endif
    js_free_rt(rt, p);
    rt->atom_count--;
    assert(rt->atom_count >= 0);
}

static void __JS_FreeAtom(JSRuntime *rt, uint32_t i)
{
    JSAtomStruct *p;

    p = rt->atom_array[i];
    if (--JS_REF_COUNT(p) > 0)
        return;
    JS_FreeAtomStruct(rt, p);
}

/* Warning: 'p' is freed */
static JSAtom JS_NewAtomStr(JSContext *ctx, JSString *p)
{
    JSRuntime *rt = ctx->rt;
    uint32_t n;
    if (is_num_string(&n, p)) {
        if (n <= JS_ATOM_MAX_INT) {
            js_free_string(rt, p);
            return __JS_AtomFromUInt32(n);
        }
    }
    /* XXX: should generate an exception */
    return __JS_NewAtom(rt, p, JS_ATOM_TYPE_STRING);
}

/* `str` may be pure ASCII or UTF-8 encoded */
JSAtom JS_NewAtomLen(JSContext *ctx, const char *str, size_t len)
{
    JSValue val;

    if (len == 0 || !is_digit(*str)) {
        // TODO(chqrlie): this does not work if `str` has UTF-8 encoded contents
        // bug example: `({ "\u00c3\u00a9": 1 }).\u00e9` evaluates to `1`.
        JSAtom atom = __JS_FindAtom(ctx->rt, str, len, JS_ATOM_TYPE_STRING);
        if (atom)
            return atom;
    }
    val = JS_NewStringLen(ctx, str, len);
    if (JS_IsException(val))
        return JS_ATOM_NULL;
    return JS_NewAtomStr(ctx, JS_VALUE_GET_STRING(val));
}

/* `str` may be pure ASCII or UTF-8 encoded */
JSAtom JS_NewAtom(JSContext *ctx, const char *str)
{
    return JS_NewAtomLen(ctx, str, strlen(str));
}

JSAtom JS_NewAtomUInt32(JSContext *ctx, uint32_t n)
{
    if (n <= JS_ATOM_MAX_INT) {
        return __JS_AtomFromUInt32(n);
    } else {
        char buf[16];
        size_t len = u32toa(buf, n);
        JSValue val = js_new_string8_len(ctx, buf, len);
        if (JS_IsException(val))
            return JS_ATOM_NULL;
        return __JS_NewAtom(ctx->rt, JS_VALUE_GET_STRING(val),
                            JS_ATOM_TYPE_STRING);
    }
}

static JSAtom JS_NewAtomInt64(JSContext *ctx, int64_t n)
{
    if ((uint64_t)n <= JS_ATOM_MAX_INT) {
        return __JS_AtomFromUInt32((uint32_t)n);
    } else {
        char buf[24];
        size_t len = i64toa(buf, n);
        JSValue val = js_new_string8_len(ctx, buf, len);
        if (JS_IsException(val))
            return JS_ATOM_NULL;
        return __JS_NewAtom(ctx->rt, JS_VALUE_GET_STRING(val),
                            JS_ATOM_TYPE_STRING);
    }
}

/* 'p' is freed */
static JSValue JS_NewSymbolInternal(JSContext *ctx, JSString *p, int atom_type)
{
    JSRuntime *rt = ctx->rt;
    JSAtom atom;
    atom = __JS_NewAtom(rt, p, atom_type);
    if (atom == JS_ATOM_NULL)
        return JS_ThrowOutOfMemory(ctx);
    return JS_MKPTR(JS_TAG_SYMBOL, rt->atom_array[atom]);
}

/* descr must be a non-numeric string atom */
static JSValue JS_NewSymbolFromAtom(JSContext *ctx, JSAtom descr,
                                    int atom_type)
{
    JSRuntime *rt = ctx->rt;
    JSString *p;

    assert(!__JS_AtomIsTaggedInt(descr));
    assert(descr < rt->atom_size);
    p = rt->atom_array[descr];
    js_dup(JS_MKPTR(JS_TAG_STRING, p));
    return JS_NewSymbolInternal(ctx, p, atom_type);
}

/* `description` may be pure ASCII or UTF-8 encoded */
JSValue JS_NewSymbol(JSContext *ctx, const char *description, bool is_global)
{
    if (description == NULL) {
        if (!is_global) {
            /* Local symbol without description: Symbol() */
            return JS_NewSymbolInternal(ctx, NULL, JS_ATOM_TYPE_SYMBOL);
        }
        /* Global symbol without description: Symbol.for() 
           Per ES spec, ToString(undefined) becomes "undefined" */
        description = "undefined";    
    }
    JSAtom atom = JS_NewAtom(ctx, description);
    if (atom == JS_ATOM_NULL)
        return JS_EXCEPTION;
    int atom_type =
        is_global ? JS_ATOM_TYPE_GLOBAL_SYMBOL : JS_ATOM_TYPE_SYMBOL;
    JSValue symbol = JS_NewSymbolFromAtom(ctx, atom, atom_type);
    JS_FreeAtom(ctx, atom);
    return symbol;
}

#define ATOM_GET_STR_BUF_SIZE 64

static const char *JS_AtomGetStrRT(JSRuntime *rt, char *buf, int buf_size,
                                   JSAtom atom)
{
    if (__JS_AtomIsTaggedInt(atom)) {
        snprintf(buf, buf_size, "%u", __JS_AtomToUInt32(atom));
    } else if (atom == JS_ATOM_NULL) {
        snprintf(buf, buf_size, "<null>");
    } else if (atom >= rt->atom_size) {
        assert(atom < rt->atom_size);
        snprintf(buf, buf_size, "<invalid %x>", atom);
    } else {
        JSAtomStruct *p = rt->atom_array[atom];
        *buf = '\0';
        if (atom_is_free(p)) {
            snprintf(buf, buf_size, "<free %x>", atom);
        } else if (p != NULL) {
            JSString *str = p;
            if (str->is_wide_char) {
                /* encode surrogates correctly */
                utf8_encode_buf16(buf, buf_size, str16(str), str->len);
            } else {
                utf8_encode_buf8(buf, buf_size, str8(str), str->len);
            }
        }
    }
    return buf;
}

static const char *JS_AtomGetStr(JSContext *ctx, char *buf, int buf_size, JSAtom atom)
{
    return JS_AtomGetStrRT(ctx->rt, buf, buf_size, atom);
}

static JSValue __JS_AtomToValue(JSContext *ctx, JSAtom atom, bool force_string)
{
    char buf[ATOM_GET_STR_BUF_SIZE];

    if (__JS_AtomIsTaggedInt(atom)) {
        size_t len = u32toa(buf, __JS_AtomToUInt32(atom));
        return js_new_string8_len(ctx, buf, len);
    } else {
        JSRuntime *rt = ctx->rt;
        JSAtomStruct *p;
        assert(atom < rt->atom_size);
        p = rt->atom_array[atom];
        if (p->atom_type == JS_ATOM_TYPE_STRING) {
            goto ret_string;
        } else if (force_string) {
            if (p->len == 0 && p->is_wide_char != 0) {
                /* no description string */
                p = rt->atom_array[JS_ATOM_empty_string];
            }
        ret_string:
            return js_dup(JS_MKPTR(JS_TAG_STRING, p));
        } else {
            return js_dup(JS_MKPTR(JS_TAG_SYMBOL, p));
        }
    }
}

JSValue JS_AtomToValue(JSContext *ctx, JSAtom atom)
{
    return __JS_AtomToValue(ctx, atom, false);
}

JSValue JS_AtomToString(JSContext *ctx, JSAtom atom)
{
    return __JS_AtomToValue(ctx, atom, true);
}

/* return true if the atom is an array index (i.e. 0 <= index <=
   2^32-2 and return its value */
static bool JS_AtomIsArrayIndex(JSContext *ctx, uint32_t *pval, JSAtom atom)
{
    if (__JS_AtomIsTaggedInt(atom)) {
        *pval = __JS_AtomToUInt32(atom);
        return true;
    } else {
        JSRuntime *rt = ctx->rt;
        JSAtomStruct *p;
        uint32_t val;

        assert(atom < rt->atom_size);
        p = rt->atom_array[atom];
        if (p->atom_type == JS_ATOM_TYPE_STRING &&
            is_num_string(&val, p) && val != -1) {
            *pval = val;
            return true;
        } else {
            *pval = 0;
            return false;
        }
    }
}

/* This test must be fast if atom is not a numeric index (e.g. a
   method name). Return JS_UNDEFINED if not a numeric
   index. JS_EXCEPTION can also be returned. */
static JSValue JS_AtomIsNumericIndex1(JSContext *ctx, JSAtom atom)
{
    JSRuntime *rt = ctx->rt;
    JSAtomStruct *p1;
    JSString *p;
    int c, len, ret;
    JSValue num, str;

    if (__JS_AtomIsTaggedInt(atom))
        return js_int32(__JS_AtomToUInt32(atom));
    assert(atom < rt->atom_size);
    p1 = rt->atom_array[atom];
    if (p1->atom_type != JS_ATOM_TYPE_STRING)
        return JS_UNDEFINED;
    p = p1;
    len = p->len;
    if (p->is_wide_char) {
        const uint16_t *r = str16(p), *r_end = str16(p) + len;
        if (r >= r_end)
            return JS_UNDEFINED;
        c = *r;
        if (c == '-') {
            if (r >= r_end)
                return JS_UNDEFINED;
            r++;
            c = *r;
            /* -0 case is specific */
            if (c == '0' && len == 2)
                goto minus_zero;
        }
        /* XXX: should test NaN, but the tests do not check it */
        if (!is_num(c)) {
            /* XXX: String should be normalized, therefore 8-bit only */
            const uint16_t nfinity16[7] = { 'n', 'f', 'i', 'n', 'i', 't', 'y' };
            if (!(c =='I' && (r_end - r) == 8 &&
                  !memcmp(r + 1, nfinity16, sizeof(nfinity16))))
                return JS_UNDEFINED;
        }
    } else {
        const uint8_t *r = str8(p), *r_end = str8(p) + len;
        if (r >= r_end)
            return JS_UNDEFINED;
        c = *r;
        if (c == '-') {
            if (r >= r_end)
                return JS_UNDEFINED;
            r++;
            c = *r;
            /* -0 case is specific */
            if (c == '0' && len == 2) {
            minus_zero:
                return js_float64(-0.0);
            }
        }
        if (!is_num(c)) {
            if (!(c =='I' && (r_end - r) == 8 &&
                  !memcmp(r + 1, "nfinity", 7)))
                return JS_UNDEFINED;
        }
    }
    /* this is ECMA CanonicalNumericIndexString primitive */
    num = JS_ToNumber(ctx, JS_MKPTR(JS_TAG_STRING, p));
    if (JS_IsException(num))
        return num;
    str = JS_ToString(ctx, num);
    if (JS_IsException(str)) {
        JS_FreeValue(ctx, num);
        return str;
    }
    ret = js_string_eq(p, JS_VALUE_GET_STRING(str));
    JS_FreeValue(ctx, str);
    if (ret) {
        return num;
    } else {
        JS_FreeValue(ctx, num);
        return JS_UNDEFINED;
    }
}

/* return -1 if exception or true/false */
static int JS_AtomIsNumericIndex(JSContext *ctx, JSAtom atom)
{
    JSValue num;
    num = JS_AtomIsNumericIndex1(ctx, atom);
    if (likely(JS_IsUndefined(num)))
        return false;
    if (JS_IsException(num))
        return -1;
    JS_FreeValue(ctx, num);
    return true;
}

void JS_FreeAtom(JSContext *ctx, JSAtom v)
{
    if (!__JS_AtomIsConst(v))
        __JS_FreeAtom(ctx->rt, v);
}

void JS_FreeAtomRT(JSRuntime *rt, JSAtom v)
{
    if (!__JS_AtomIsConst(v))
        __JS_FreeAtom(rt, v);
}

/* return true if 'v' is a symbol with a string description */
static bool JS_AtomSymbolHasDescription(JSContext *ctx, JSAtom v)
{
    JSRuntime *rt;
    JSAtomStruct *p;

    rt = ctx->rt;
    if (__JS_AtomIsTaggedInt(v))
        return false;
    p = rt->atom_array[v];
    return (((p->atom_type == JS_ATOM_TYPE_SYMBOL &&
              p->hash == JS_ATOM_HASH_SYMBOL) ||
             p->atom_type == JS_ATOM_TYPE_GLOBAL_SYMBOL) &&
            !(p->len == 0 && p->is_wide_char != 0));
}

static __maybe_unused void print_atom(JSContext *ctx, JSAtom atom)
{
    char buf[ATOM_GET_STR_BUF_SIZE];
    const char *p;
    int i;

    /* XXX: should handle embedded null characters */
    /* XXX: should move encoding code to JS_AtomGetStr */
    p = JS_AtomGetStr(ctx, buf, sizeof(buf), atom);
    for (i = 0; p[i]; i++) {
        int c = (unsigned char)p[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c == '_' || c == '$') || (c >= '0' && c <= '9' && i > 0)))
            break;
    }
    if (i > 0 && p[i] == '\0') {
        printf("%s", p);
    } else {
        putchar('"');
        printf("%.*s", i, p);
        for (; p[i]; i++) {
            int c = (unsigned char)p[i];
            if (c == '\"' || c == '\\') {
                putchar('\\');
                putchar(c);
            } else if (c >= ' ' && c <= 126) {
                putchar(c);
            } else if (c == '\n') {
                putchar('\\');
                putchar('n');
            } else {
                printf("\\u%04x", c);
            }
        }
        putchar('\"');
    }
}

/* free with JS_FreeCString() */
const char *JS_AtomToCStringLen(JSContext *ctx, size_t *plen, JSAtom atom)
{
    JSValue str;
    const char *cstr;

    str = JS_AtomToString(ctx, atom);
    if (JS_IsException(str)) {
        if (plen)
            *plen = 0;
        return NULL;
    }
    cstr = JS_ToCStringLen(ctx, plen, str);
    JS_FreeValue(ctx, str);
    return cstr;
}

#ifndef TURBOJS_DISABLE_PARSER

/* return a string atom containing name concatenated with str1 */
/* `str1` may be pure ASCII or UTF-8 encoded */
// TODO(chqrlie): use string concatenation instead of UTF-8 conversion
static JSAtom js_atom_concat_str(JSContext *ctx, JSAtom name, const char *str1)
{
    JSValue str;
    JSAtom atom;
    const char *cstr;
    char *cstr2;
    size_t len, len1;

    str = JS_AtomToString(ctx, name);
    if (JS_IsException(str))
        return JS_ATOM_NULL;
    cstr = JS_ToCStringLen(ctx, &len, str);
    if (!cstr)
        goto fail;
    len1 = strlen(str1);
    cstr2 = js_malloc(ctx, len + len1 + 1);
    if (!cstr2)
        goto fail;
    memcpy(cstr2, cstr, len);
    memcpy(cstr2 + len, str1, len1);
    cstr2[len + len1] = '\0';
    atom = JS_NewAtomLen(ctx, cstr2, len + len1);
    js_free(ctx, cstr2);
    JS_FreeCString(ctx, cstr);
    JS_FreeValue(ctx, str);
    return atom;
 fail:
    JS_FreeCString(ctx, cstr);
    JS_FreeValue(ctx, str);
    return JS_ATOM_NULL;
}

static JSAtom js_atom_concat_num(JSContext *ctx, JSAtom name, uint32_t n)
{
    char buf[16];
    size_t len;

    len = u32toa(buf, n);
    buf[len] = '\0';
    return js_atom_concat_str(ctx, name, buf);
}

#endif // TURBOJS_DISABLE_PARSER

static inline bool JS_IsEmptyString(JSValueConst v)
{
    return JS_VALUE_GET_TAG(v) == JS_TAG_STRING && JS_VALUE_GET_STRING(v)->len == 0;
}

