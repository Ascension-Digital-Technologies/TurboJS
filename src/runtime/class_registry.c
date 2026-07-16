/* Engine domain source: runtime/classes_strings.inc -> class_registry.
 * Ownership: runtime subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

/* JSClass support */

/* a new class ID is allocated if *pclass_id == 0, otherwise *pclass_id is left unchanged */
JSClassID JS_NewClassID(JSRuntime *rt, JSClassID *pclass_id)
{
    JSClassID class_id = *pclass_id;
    if (class_id == 0) {
        class_id = rt->js_class_id_alloc++;
        *pclass_id = class_id;
    }
    return class_id;
}

JSClassID JS_GetClassID(JSValueConst v)
{
  JSObject *p;
  if (JS_VALUE_GET_TAG(v) != JS_TAG_OBJECT)
    return JS_INVALID_CLASS_ID;
  p = JS_VALUE_GET_OBJ(v);
  return p->class_id;
}

bool JS_IsRegisteredClass(JSRuntime *rt, JSClassID class_id)
{
    return (class_id < rt->class_count &&
            rt->class_array[class_id].class_id != 0);
}

JSAtom JS_GetClassName(JSRuntime *rt, JSClassID class_id)
{
    if (JS_IsRegisteredClass(rt, class_id)) {
        return JS_DupAtomRT(rt, rt->class_array[class_id].class_id);
    } else {
        return JS_ATOM_NULL;
    }
}

/* create a new object internal class. Return -1 if error, 0 if
   OK. The finalizer can be NULL if none is needed. */
static int JS_NewClass1(JSRuntime *rt, JSClassID class_id,
                        const JSClassDef *class_def, JSAtom name)
{
    int new_size, i;
    JSClass *cl, *new_class_array;
    struct list_head *el;

    if (class_id >= (1 << 16))
        return -1;
    if (class_id < rt->class_count &&
        rt->class_array[class_id].class_id != 0)
        return -1;

    if (class_id >= rt->class_count) {
        new_size = max_int(JS_CLASS_INIT_COUNT,
                           max_int(class_id + 1, rt->class_count * 3 / 2));

        /* reallocate the context class prototype array, if any */
        list_for_each(el, &rt->context_list) {
            JSContext *ctx = list_entry(el, JSContext, link);
            JSValue *new_tab;
            new_tab = js_realloc_rt(rt, ctx->class_proto,
                                    sizeof(ctx->class_proto[0]) * new_size);
            if (!new_tab)
                return -1;
            for(i = rt->class_count; i < new_size; i++)
                new_tab[i] = JS_NULL;
            ctx->class_proto = new_tab;
        }
        /* reallocate the class array */
        new_class_array = js_realloc_rt(rt, rt->class_array,
                                        sizeof(JSClass) * new_size);
        if (!new_class_array)
            return -1;
        memset(new_class_array + rt->class_count, 0,
               (new_size - rt->class_count) * sizeof(JSClass));
        rt->class_array = new_class_array;
        rt->class_count = new_size;
    }
    cl = &rt->class_array[class_id];
    cl->class_id = class_id;
    cl->class_name = JS_DupAtomRT(rt, name);
    cl->finalizer = class_def->finalizer;
    cl->gc_mark = class_def->gc_mark;
    cl->call = class_def->call;
    cl->exotic = class_def->exotic;
    return 0;
}

int JS_NewClass(JSRuntime *rt, JSClassID class_id, const JSClassDef *class_def)
{
    int ret, len;
    JSAtom name;

    // XXX: class_def->class_name must be raw 8-bit contents. No UTF-8 encoded strings
    len = strlen(class_def->class_name);
    name = __JS_FindAtom(rt, class_def->class_name, len, JS_ATOM_TYPE_STRING);
    if (name == JS_ATOM_NULL) {
        name = __JS_NewAtomInit(rt, class_def->class_name, len, JS_ATOM_TYPE_STRING);
        if (name == JS_ATOM_NULL)
            return -1;
    }
    ret = JS_NewClass1(rt, class_id, class_def, name);
    JS_FreeAtomRT(rt, name);
    return ret;
}

static inline JSValue js_empty_string(JSRuntime *rt)
{
    JSAtomStruct *p = rt->atom_array[JS_ATOM_empty_string];
    return js_dup(JS_MKPTR(JS_TAG_STRING, p));
}

// XXX: `buf` contains raw 8-bit data, no UTF-8 decoding is performed
// XXX: no special case for len == 0
static JSValue js_new_string8_len(JSContext *ctx, const char *buf, int len)
{
    JSString *str;
    str = js_alloc_string(ctx, len, 0);
    if (!str)
        return JS_EXCEPTION;
    memcpy(str8(str), buf, len);
    str8(str)[len] = '\0';
    return JS_MKPTR(JS_TAG_STRING, str);
}

// XXX: `buf` contains raw 8-bit data, no UTF-8 decoding is performed
// XXX: no special case for the empty string
static inline JSValue js_new_string8(JSContext *ctx, const char *str)
{
    return js_new_string8_len(ctx, str, strlen(str));
}

static JSValue js_new_string16_len(JSContext *ctx, const uint16_t *buf, int len)
{
    JSString *str;
    str = js_alloc_string(ctx, len, 1);
    if (!str)
        return JS_EXCEPTION;
    memcpy(str16(str), buf, len * 2);
    return JS_MKPTR(JS_TAG_STRING, str);
}

static JSValue js_new_string_char(JSContext *ctx, uint16_t c)
{
    if (c < 0x100) {
        char ch8 = c;
        return js_new_string8_len(ctx, &ch8, 1);
    } else {
        uint16_t ch16 = c;
        return js_new_string16_len(ctx, &ch16, 1);
    }
}

static JSValue js_sub_string(JSContext *ctx, JSString *p, int start, int end)
{
    JSStringSlice *slice;
    JSString *q;
    int len;

    len = end - start;
    if (start == 0 && end == p->len) {
        return js_dup(JS_MKPTR(JS_TAG_STRING, p));
    }
    if (len <= 0) {
        return js_empty_string(ctx->rt);
    }
    if (len > (JS_STRING_SLICE_LEN_MAX >> p->is_wide_char)) {
        if (p->kind == JS_STRING_KIND_SLICE) {
            slice = (void *)&p[1];
            p = slice->parent;
            start += slice->start >> p->is_wide_char; // bytes -> chars
        }
        // allocate as 16 bit wide string to avoid wastage;
        // js_alloc_string allocates 1 byte extra for 8 bit strings;
        q = js_alloc_string(ctx, sizeof(*slice)/2, /*is_wide_char*/true);
        if (!q)
            return JS_EXCEPTION;
        q->is_wide_char = p->is_wide_char;
        q->kind = JS_STRING_KIND_SLICE;
        q->len = len;
        slice = (void *)&q[1];
        slice->parent = p;
        slice->start = start << p->is_wide_char; // chars -> bytes
        JS_REF_COUNT(p)++;
        return JS_MKPTR(JS_TAG_STRING, q);
    }
    if (p->is_wide_char) {
        JSString *str;
        int i;
        uint16_t c = 0;
        for (i = start; i < end; i++) {
            c |= str16(p)[i];
        }
        if (c > 0xFF)
            return js_new_string16_len(ctx, str16(p) + start, len);

        str = js_alloc_string(ctx, len, 0);
        if (!str)
            return JS_EXCEPTION;
        for (i = 0; i < len; i++) {
            str8(str)[i] = str16(p)[start + i];
        }
        str8(str)[len] = '\0';
        return JS_MKPTR(JS_TAG_STRING, str);
    } else {
        return js_new_string8_len(ctx, (const char *)(str8(p) + start), len);
    }
}

typedef struct StringBuffer {
    JSContext *ctx;
    JSString *str;
    int len;
    int size;
    int is_wide_char;
    int error_status;
} StringBuffer;

/* It is valid to call string_buffer_end() and all string_buffer functions even
   if string_buffer_init() or another string_buffer function returns an error.
   If the error_status is set, string_buffer_end() returns JS_EXCEPTION.
 */
static int string_buffer_init2(JSContext *ctx, StringBuffer *s, int size,
                               int is_wide)
{
    s->ctx = ctx;
    s->size = size;
    s->len = 0;
    s->is_wide_char = is_wide;
    s->error_status = 0;
    s->str = js_alloc_string(ctx, size, is_wide);
    if (unlikely(!s->str)) {
        s->size = 0;
        return s->error_status = -1;
    }
#ifdef ENABLE_DUMPS // JS_DUMP_LEAKS
    /* the StringBuffer may reallocate the JSString, only link it at the end */
    list_del(&s->str->link);
#endif
    return 0;
}

static inline int string_buffer_init(JSContext *ctx, StringBuffer *s, int size)
{
    return string_buffer_init2(ctx, s, size, 0);
}

static void string_buffer_free(StringBuffer *s)
{
    js_free(s->ctx, s->str);
    s->str = NULL;
}

static int string_buffer_set_error(StringBuffer *s)
{
    js_free(s->ctx, s->str);
    s->str = NULL;
    s->size = 0;
    s->len = 0;
    return s->error_status = -1;
}

static no_inline int string_buffer_widen(StringBuffer *s, int size)
{
    JSString *str;
    size_t slack;
    int i;

    if (s->error_status)
        return -1;

    str = js_realloc2(s->ctx, s->str, sizeof(JSString) + (size << 1), &slack);
    if (!str)
        return string_buffer_set_error(s);
    size += slack >> 1;
    for(i = s->len; i-- > 0;) {
        str16(str)[i] = str8(str)[i];
    }
    s->is_wide_char = 1;
    s->size = size;
    s->str = str;
    return 0;
}

static no_inline int string_buffer_realloc(StringBuffer *s, int new_len, int c)
{
    JSString *new_str;
    int new_size;
    size_t new_size_bytes, slack;

    if (s->error_status)
        return -1;

    if (new_len > JS_STRING_LEN_MAX) {
        JS_ThrowRangeError(s->ctx, "invalid string length");
        return string_buffer_set_error(s);
    }
    new_size = min_int(max_int(new_len, s->size * 3 / 2), JS_STRING_LEN_MAX);
    if (!s->is_wide_char && c >= 0x100) {
        return string_buffer_widen(s, new_size);
    }
    new_size_bytes = sizeof(JSString) + (new_size << s->is_wide_char) + 1 - s->is_wide_char;
    new_str = js_realloc2(s->ctx, s->str, new_size_bytes, &slack);
    if (!new_str)
        return string_buffer_set_error(s);
    new_size = min_int(new_size + (slack >> s->is_wide_char), JS_STRING_LEN_MAX);
    s->size = new_size;
    s->str = new_str;
    return 0;
}

static no_inline int string_buffer_putc16_slow(StringBuffer *s, uint32_t c)
{
    if (unlikely(s->len >= s->size)) {
        if (string_buffer_realloc(s, s->len + 1, c))
            return -1;
    }
    if (s->is_wide_char) {
        str16(s->str)[s->len++] = c;
    } else if (c < 0x100) {
        str8(s->str)[s->len++] = c;
    } else {
        if (string_buffer_widen(s, s->size))
            return -1;
        str16(s->str)[s->len++] = c;
    }
    return 0;
}

/* 0 <= c <= 0xff */
static int string_buffer_putc8(StringBuffer *s, uint32_t c)
{
    if (unlikely(s->len >= s->size)) {
        if (string_buffer_realloc(s, s->len + 1, c))
            return -1;
    }
    if (s->is_wide_char) {
        str16(s->str)[s->len++] = c;
    } else {
        str8(s->str)[s->len++] = c;
    }
    return 0;
}

/* 0 <= c <= 0xffff */
static int string_buffer_putc16(StringBuffer *s, uint32_t c)
{
    if (likely(s->len < s->size)) {
        if (s->is_wide_char) {
            str16(s->str)[s->len++] = c;
            return 0;
        } else if (c < 0x100) {
            str8(s->str)[s->len++] = c;
            return 0;
        }
    }
    return string_buffer_putc16_slow(s, c);
}

/* 0 <= c <= 0x10ffff */
static no_inline int string_buffer_putc_slow(StringBuffer *s, uint32_t c)
{
    if (c >= 0x10000) {
        /* surrogate pair */
        if (string_buffer_putc16(s, get_hi_surrogate(c)))
            return -1;
        c = get_lo_surrogate(c);
    }
    return string_buffer_putc16(s, c);
}

/* 0 <= c <= 0x10ffff */
static inline int string_buffer_putc(StringBuffer *s, uint32_t c)
{
    if (likely(s->len < s->size)) {
        if (s->is_wide_char) {
            if (c < 0x10000) {
                str16(s->str)[s->len++] = c;
                return 0;
            } else if (s->len + 1 < s->size) {
                /* surrogate pair */
                str16(s->str)[s->len++] = get_hi_surrogate(c);
                str16(s->str)[s->len++] = get_lo_surrogate(c);
                return 0;
            }
        } else if (c < 0x100) {
            str8(s->str)[s->len++] = c;
            return 0;
        }
    }
    return string_buffer_putc_slow(s, c);
}

static int string_getc(JSString *p, int *pidx)
{
    int idx, c, c1;
    idx = *pidx;
    if (p->is_wide_char) {
        c = str16(p)[idx++];
        if (is_hi_surrogate(c) && idx < p->len) {
            c1 = str16(p)[idx];
            if (is_lo_surrogate(c1)) {
                c = from_surrogate(c, c1);
                idx++;
            }
        }
    } else {
        c = str8(p)[idx++];
    }
    *pidx = idx;
    return c;
}

static int string_buffer_write8(StringBuffer *s, const uint8_t *p, int len)
{
    int i;

    if (s->len + len > s->size) {
        if (string_buffer_realloc(s, s->len + len, 0))
            return -1;
    }
    if (s->is_wide_char) {
        for (i = 0; i < len; i++) {
            str16(s->str)[s->len + i] = p[i];
        }
        s->len += len;
    } else {
        memcpy(&str8(s->str)[s->len], p, len);
        s->len += len;
    }
    return 0;
}

static int string_buffer_write16(StringBuffer *s, const uint16_t *p, int len)
{
    int c = 0, i;

    for (i = 0; i < len; i++) {
        c |= p[i];
    }
    if (s->len + len > s->size) {
        if (string_buffer_realloc(s, s->len + len, c))
            return -1;
    } else if (!s->is_wide_char && c >= 0x100) {
        if (string_buffer_widen(s, s->size))
            return -1;
    }
    if (s->is_wide_char) {
        memcpy(&str16(s->str)[s->len], p, len << 1);
        s->len += len;
    } else {
        for (i = 0; i < len; i++) {
            str8(s->str)[s->len + i] = p[i];
        }
        s->len += len;
    }
    return 0;
}

/* appending an ASCII string */
static int string_buffer_puts8(StringBuffer *s, const char *str)
{
    return string_buffer_write8(s, (const uint8_t *)str, strlen(str));
}

