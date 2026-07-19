/* Engine domain source: runtime/atoms.inc -> atom_table.
 * Ownership: runtime subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

/* JSAtom support */

#define JS_ATOM_TAG_INT (1U << 31)
#define JS_ATOM_MAX_INT (JS_ATOM_TAG_INT - 1)
#define JS_ATOM_MAX     ((1U << 30) - 1)

/* return the max count from the hash size */
#define JS_ATOM_COUNT_RESIZE(n) ((n) * 2)

static inline bool __JS_AtomIsConst(JSAtom v)
{
    return (int32_t)v < JS_ATOM_END;
}

static inline bool __JS_AtomIsTaggedInt(JSAtom v)
{
    return (v & JS_ATOM_TAG_INT) != 0;
}

static inline JSAtom __JS_AtomFromUInt32(uint32_t v)
{
    return v | JS_ATOM_TAG_INT;
}

static inline uint32_t __JS_AtomToUInt32(JSAtom atom)
{
    return atom & ~JS_ATOM_TAG_INT;
}

static inline int is_num(int c)
{
    return c >= '0' && c <= '9';
}

/* return true if the string is a number n with 0 <= n <= 2^32-1 */
static inline bool is_num_string(uint32_t *pval, JSString *p)
{
    uint32_t n;
    uint64_t n64;
    int c, i, len;

    len = p->len;
    if (len == 0 || len > 10)
        return false;
    c = string_get(p, 0);
    if (is_num(c)) {
        if (c == '0') {
            if (len != 1)
                return false;
            n = 0;
        } else {
            n = c - '0';
            for(i = 1; i < len; i++) {
                c = string_get(p, i);
                if (!is_num(c))
                    return false;
                n64 = (uint64_t)n * 10 + (c - '0');
                if ((n64 >> 32) != 0)
                    return false;
                n = n64;
            }
        }
        *pval = n;
        return true;
    } else {
        return false;
    }
}

/* XXX: could use faster version ? */
static inline uint32_t hash_string8(const uint8_t *str, size_t len, uint32_t h)
{
    size_t i;

    for(i = 0; i < len; i++)
        h = h * 263 + str[i];
    return h;
}

static inline uint32_t hash_string16(const uint16_t *str,
                                     size_t len, uint32_t h)
{
    size_t i;

    for(i = 0; i < len; i++)
        h = h * 263 + str[i];
    return h;
}

static uint32_t hash_string(JSString *str, uint32_t h)
{
    if (str->is_wide_char)
        h = hash_string16(str16(str), str->len, h);
    else
        h = hash_string8(str8(str), str->len, h);
    return h;
}

static uint32_t hash_string_rope(JSValueConst val, uint32_t h)
{
    if (JS_VALUE_GET_TAG(val) == JS_TAG_STRING) {
        return hash_string(JS_VALUE_GET_STRING(val), h);
    } else {
        JSStringRope *r = JS_VALUE_GET_STRING_ROPE(val);
        h = hash_string_rope(r->left, h);
        return hash_string_rope(r->right, h);
    }
}

static __maybe_unused void JS_DumpString(JSRuntime *rt, JSString *p)
{
    int i, c, sep;

    if (p == NULL) {
        printf("<null>");
        return;
    }
    if (JS_REF_COUNT(p) != 1)
        printf("%d", JS_REF_COUNT(p));
    if (p->is_wide_char)
        putchar('L');
    sep = '\"';
    putchar(sep);
    for(i = 0; i < p->len; i++) {
        c = string_get(p, i);
        if (c == sep || c == '\\') {
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
    putchar(sep);
}

static __maybe_unused void JS_DumpAtoms(JSRuntime *rt)
{
    JSAtomStruct *p;
    int h, i;
    /* This only dumps hashed atoms, not JS_ATOM_TYPE_SYMBOL atoms */
    printf("JSAtom count=%d size=%d hash_size=%d:\n",
           rt->atom_count, rt->atom_size, rt->atom_hash_size);
    printf("JSAtom hash table: {\n");
    for(i = 0; i < rt->atom_hash_size; i++) {
        h = rt->atom_hash[i];
        if (h) {
            printf("  %d:", i);
            while (h) {
                p = rt->atom_array[h];
                printf(" ");
                JS_DumpString(rt, p);
                h = p->hash_next;
            }
            printf("\n");
        }
    }
    printf("}\n");
    printf("JSAtom table: {\n");
    for(i = 0; i < rt->atom_size; i++) {
        p = rt->atom_array[i];
        if (!atom_is_free(p)) {
            printf("  %d: { %d %08x ", i, p->atom_type, p->hash);
            if (!(p->len == 0 && p->is_wide_char != 0))
                JS_DumpString(rt, p);
            printf(" %d }\n", p->hash_next);
        }
    }
    printf("}\n");
}

static int JS_ResizeAtomHash(JSRuntime *rt, int new_hash_size)
{
    JSAtomStruct *p;
    uint32_t new_hash_mask, h, i, hash_next1, j, *new_hash;

    assert((new_hash_size & (new_hash_size - 1)) == 0); /* power of two */
    new_hash_mask = new_hash_size - 1;
    new_hash = js_mallocz_rt(rt, sizeof(rt->atom_hash[0]) * new_hash_size);
    if (!new_hash)
        return -1;
    for(i = 0; i < rt->atom_hash_size; i++) {
        h = rt->atom_hash[i];
        while (h != 0) {
            p = rt->atom_array[h];
            hash_next1 = p->hash_next;
            /* add in new hash table */
            j = p->hash & new_hash_mask;
            p->hash_next = new_hash[j];
            new_hash[j] = h;
            h = hash_next1;
        }
    }
    js_free_rt(rt, rt->atom_hash);
    rt->atom_hash = new_hash;
    rt->atom_hash_size = new_hash_size;
    rt->atom_count_resize = JS_ATOM_COUNT_RESIZE(new_hash_size);
    //    JS_DumpAtoms(rt);
    return 0;
}

static int JS_InitAtoms(JSRuntime *rt)
{
    int i, len, atom_type;
    const char *p;

    rt->atom_hash_size = 0;
    rt->atom_hash = NULL;
    rt->atom_count = 0;
    rt->atom_size = 0;
    rt->atom_free_index = 0;
    if (JS_ResizeAtomHash(rt, 512))     /* there are at least 504 predefined atoms */
        return -1;

    p = js_atom_init;
    for(i = 1; i < JS_ATOM_END; i++) {
        if (i == JS_ATOM_Private_brand)
            atom_type = JS_ATOM_TYPE_PRIVATE;
        else if (i >= JS_ATOM_Symbol_toPrimitive)
            atom_type = JS_ATOM_TYPE_SYMBOL;
        else
            atom_type = JS_ATOM_TYPE_STRING;
        len = strlen(p);
        if (__JS_NewAtomInit(rt, p, len, atom_type) == JS_ATOM_NULL)
            return -1;
        p = p + len + 1;
    }
    return 0;
}

JSAtom JS_DupAtomRT(JSRuntime *rt, JSAtom v)
{
    JSAtomStruct *p;

    if (!__JS_AtomIsConst(v)) {
        p = rt->atom_array[v];
        JS_REF_COUNT(p)++;
    }
    return v;
}

JSAtom JS_DupAtom(JSContext *ctx, JSAtom v)
{
    JSRuntime *rt;
    JSAtomStruct *p;

    if (!__JS_AtomIsConst(v)) {
        rt = ctx->rt;
        p = rt->atom_array[v];
        JS_REF_COUNT(p)++;
    }
    return v;
}

static JSAtomKindEnum JS_AtomGetKind(JSContext *ctx, JSAtom v)
{
    JSRuntime *rt;
    JSAtomStruct *p;

    rt = ctx->rt;
    if (__JS_AtomIsTaggedInt(v))
        return JS_ATOM_KIND_STRING;
    p = rt->atom_array[v];
    switch(p->atom_type) {
    case JS_ATOM_TYPE_STRING:
        return JS_ATOM_KIND_STRING;
    case JS_ATOM_TYPE_GLOBAL_SYMBOL:
        return JS_ATOM_KIND_SYMBOL;
    case JS_ATOM_TYPE_SYMBOL:
        switch(p->hash) {
        case JS_ATOM_HASH_SYMBOL:
            return JS_ATOM_KIND_SYMBOL;
        case JS_ATOM_HASH_PRIVATE:
            return JS_ATOM_KIND_PRIVATE;
        default:
            abort();
        }
    default:
        abort();
    }
    return (JSAtomKindEnum){-1}; // pacify compiler
}

static JSAtom js_get_atom_index(JSRuntime *rt, JSAtomStruct *p)
{
    uint32_t i = p->hash_next;  /* atom_index */
    if (p->atom_type != JS_ATOM_TYPE_SYMBOL) {
        JSAtomStruct *p1;

        i = rt->atom_hash[p->hash & (rt->atom_hash_size - 1)];
        p1 = rt->atom_array[i];
        while (p1 != p) {
            assert(i != 0);
            i = p1->hash_next;
            p1 = rt->atom_array[i];
        }
    }
    return i;
}

/* string case (internal). Return JS_ATOM_NULL if error. 'str' is
   freed. */
static JSAtom __JS_NewAtom(JSRuntime *rt, JSString *str, int atom_type)
{
    uint32_t h, h1, i;
    JSAtomStruct *p;
    int len;

    if (atom_type < JS_ATOM_TYPE_SYMBOL) {
        /* str is not NULL */
        if (str->atom_type == atom_type) {
            /* str is the atom, return its index */
            i = js_get_atom_index(rt, str);
            /* reduce string refcount and increase atom's unless constant */
            if (__JS_AtomIsConst(i))
                JS_REF_COUNT(str)--;
            return i;
        }
        /* try and locate an already registered atom */
        len = str->len;
        h = hash_string(str, atom_type);
        h &= JS_ATOM_HASH_MASK;
        h1 = h & (rt->atom_hash_size - 1);
        i = rt->atom_hash[h1];
        while (i != 0) {
            p = rt->atom_array[i];
            if (p->hash == h &&
                p->atom_type == atom_type &&
                p->len == len &&
                js_string_memcmp(p, str, len) == 0) {
                if (!__JS_AtomIsConst(i))
                    JS_REF_COUNT(p)++;
                goto done;
            }
            i = p->hash_next;
        }
    } else {
        h1 = 0; /* avoid warning */
        if (atom_type == JS_ATOM_TYPE_SYMBOL) {
            h = JS_ATOM_HASH_SYMBOL;
        } else {
            h = JS_ATOM_HASH_PRIVATE;
            atom_type = JS_ATOM_TYPE_SYMBOL;
        }
    }

    if (rt->atom_free_index == 0) {
        /* allow new atom entries */
        uint32_t new_size, start;
        JSAtomStruct **new_array;

        /* alloc new with size progression 3/2:
           4 6 9 13 19 28 42 63 94 141 211 316 474 711 1066 1599 2398 3597 5395 8092
           preallocating space for predefined atoms (at least 504).
         */
        new_size = max_int(711, rt->atom_size * 3 / 2);
        if (new_size > JS_ATOM_MAX)
            goto fail;
        /* XXX: should use realloc2 to use slack space */
        new_array = js_realloc_rt(rt, rt->atom_array, sizeof(*new_array) * new_size);
        if (!new_array)
            goto fail;
        /* Note: the atom 0 is not used */
        start = rt->atom_size;
        if (start == 0) {
            /* JS_ATOM_NULL entry */
            p = js_mallocz_rt(rt, sizeof(JSAtomStruct));
            if (!p) {
                js_free_rt(rt, new_array);
                goto fail;
            }
            JS_REF_COUNT(p) = 1;  /* not refcounted */
            p->atom_type = JS_ATOM_TYPE_SYMBOL;
#ifdef ENABLE_DUMPS // JS_DUMP_LEAKS
            list_add_tail(&p->link, &rt->string_list);
#endif
            new_array[0] = p;
            rt->atom_count++;
            start = 1;
        }
        rt->atom_size = new_size;
        rt->atom_array = new_array;
        rt->atom_free_index = start;
        for(i = start; i < new_size; i++) {
            uint32_t next;
            if (i == (new_size - 1))
                next = 0;
            else
                next = i + 1;
            rt->atom_array[i] = atom_set_free(next);
        }
    }

    if (str) {
        if (str->atom_type == 0) {
            p = str;
            p->atom_type = atom_type;
        } else {
            p = js_malloc_rt(rt, sizeof(JSString) +
                             (str->len << str->is_wide_char) +
                             1 - str->is_wide_char);
            if (unlikely(!p))
                goto fail;
            JS_REF_COUNT(p) = 1;
            p->is_wide_char = str->is_wide_char;
            p->len = str->len;
            p->kind = JS_STRING_KIND_NORMAL;
#ifdef ENABLE_DUMPS // JS_DUMP_LEAKS
            list_add_tail(&p->link, &rt->string_list);
#endif
            memcpy(str8(p), str8(str), (str->len << str->is_wide_char) +
                   1 - str->is_wide_char);
            js_free_string(rt, str);
        }
    } else {
        p = js_malloc_rt(rt, sizeof(JSAtomStruct)); /* empty wide string */
        if (!p)
            return JS_ATOM_NULL;
        JS_REF_COUNT(p) = 1;
        p->is_wide_char = 1;    /* Hack to represent NULL as a JSString */
        p->len = 0;
        p->kind = JS_STRING_KIND_NORMAL;
#ifdef ENABLE_DUMPS // JS_DUMP_LEAKS
        list_add_tail(&p->link, &rt->string_list);
#endif
    }

    /* use an already free entry */
    i = rt->atom_free_index;
    rt->atom_free_index = atom_get_free(rt->atom_array[i]);
    rt->atom_array[i] = p;

    p->hash = h;
    p->hash_next = i;   /* atom_index */
    p->atom_type = atom_type;
    p->first_weak_ref = NULL;

    rt->atom_count++;

    if (atom_type != JS_ATOM_TYPE_SYMBOL) {
        p->hash_next = rt->atom_hash[h1];
        rt->atom_hash[h1] = i;
        if (unlikely(rt->atom_count >= rt->atom_count_resize))
            JS_ResizeAtomHash(rt, rt->atom_hash_size * 2);
    }

    //    JS_DumpAtoms(rt);
    return i;

 fail:
    i = JS_ATOM_NULL;
 done:
    if (str)
        js_free_string(rt, str);
    return i;
}

// XXX: `str` must be pure ASCII. No UTF-8 encoded strings
// XXX: `str` must not be the string representation of a small integer
static JSAtom __JS_NewAtomInit(JSRuntime *rt, const char *str, int len,
                               int atom_type)
{
    JSString *p;
    p = js_alloc_string_rt(rt, len, 0);
    if (!p)
        return JS_ATOM_NULL;
    memcpy(str8(p), str, len);
    str8(p)[len] = '\0';
    return __JS_NewAtom(rt, p, atom_type);
}

// XXX: `str` must be raw 8-bit contents. No UTF-8 encoded strings
static JSAtom __JS_FindAtom(JSRuntime *rt, const char *str, size_t len,
                            int atom_type)
{
    uint32_t h, h1, i;
    JSAtomStruct *p;

    h = hash_string8((const uint8_t *)str, len, JS_ATOM_TYPE_STRING);
    h &= JS_ATOM_HASH_MASK;
    h1 = h & (rt->atom_hash_size - 1);
    i = rt->atom_hash[h1];
    while (i != 0) {
        p = rt->atom_array[i];
        if (p->hash == h &&
            p->atom_type == JS_ATOM_TYPE_STRING &&
            p->len == len &&
            p->is_wide_char == 0 &&
            memcmp(str8(p), str, len) == 0) {
            if (!__JS_AtomIsConst(i))
                JS_REF_COUNT(p)++;
            return i;
        }
        i = p->hash_next;
    }
    return JS_ATOM_NULL;
}

