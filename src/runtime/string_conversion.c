/* Engine domain source: runtime/classes_strings.inc -> string_conversion.
 * Ownership: runtime subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static int string_rope_get(JSValueConst val, uint32_t idx)
{
    if (JS_VALUE_GET_TAG(val) == JS_TAG_STRING) {
        return string_get(JS_VALUE_GET_STRING(val), idx);
    } else {
        JSStringRope *r = JS_VALUE_GET_STRING_ROPE(val);
        uint32_t len;
        if (JS_VALUE_GET_TAG(r->left) == JS_TAG_STRING)
            len = JS_VALUE_GET_STRING(r->left)->len;
        else
            len = JS_VALUE_GET_STRING_ROPE(r->left)->len;
        if (idx < len)
            return string_rope_get(r->left, idx);
        else
            return string_rope_get(r->right, idx - len);
    }
}

typedef struct {
    JSValueConst stack[JS_STRING_ROPE_MAX_DEPTH];
    int stack_len;
} JSStringRopeIter;

static void string_rope_iter_init(JSStringRopeIter *s, JSValueConst val)
{
    s->stack_len = 0;
    s->stack[s->stack_len++] = val;
}

/* iterate thru a rope and return the strings in order */
static JSString *string_rope_iter_next(JSStringRopeIter *s)
{
    JSValueConst val;
    JSStringRope *r;

    if (s->stack_len == 0)
        return NULL;
    val = s->stack[--s->stack_len];
    for(;;) {
        if (JS_VALUE_GET_TAG(val) == JS_TAG_STRING)
            return JS_VALUE_GET_STRING(val);
        r = JS_VALUE_GET_STRING_ROPE(val);
        assert(s->stack_len < JS_STRING_ROPE_MAX_DEPTH);
        s->stack[s->stack_len++] = r->right;
        val = r->left;
    }
}

/* compare two string values with position offsets */
static int js_string_memcmp_pos(JSString *p1, uint32_t pos1,
                                JSString *p2, uint32_t pos2, uint32_t len)
{
    int res;

    if (likely(!p1->is_wide_char)) {
        if (likely(!p2->is_wide_char))
            res = memcmp(str8(p1) + pos1, str8(p2) + pos2, len);
        else
            res = -memcmp16_8(str16(p2) + pos2, str8(p1) + pos1, len);
    } else {
        if (!p2->is_wide_char)
            res = memcmp16_8(str16(p1) + pos1, str8(p2) + pos2, len);
        else
            res = memcmp16(str16(p1) + pos1, str16(p2) + pos2, len);
    }
    return res;
}

static int js_string_rope_compare(JSValueConst op1,
                                  JSValueConst op2, bool eq_only)
{
    uint32_t len1, len2, len, pos1, pos2, l;
    int res;
    JSStringRopeIter it1, it2;
    JSString *p1, *p2;

    len1 = string_rope_get_len(op1);
    len2 = string_rope_get_len(op2);
    /* no need to go further for equality test if different length */
    if (eq_only && len1 != len2)
        return 1;
    len = min_uint32(len1, len2);
    string_rope_iter_init(&it1, op1);
    string_rope_iter_init(&it2, op2);
    p1 = string_rope_iter_next(&it1);
    p2 = string_rope_iter_next(&it2);
    pos1 = 0;
    pos2 = 0;
    while (len != 0) {
        l = min_uint32(p1->len - pos1, p2->len - pos2);
        l = min_uint32(l, len);
        res = js_string_memcmp_pos(p1, pos1, p2, pos2, l);
        if (res != 0)
            return res;
        len -= l;
        pos1 += l;
        if (pos1 >= p1->len) {
            p1 = string_rope_iter_next(&it1);
            pos1 = 0;
        }
        pos2 += l;
        if (pos2 >= p2->len) {
            p2 = string_rope_iter_next(&it2);
            pos2 = 0;
        }
    }

    if (len1 == len2)
        res = 0;
    else if (len1 < len2)
        res = -1;
    else
        res = 1;
    return res;
}

/* forward declaration */
static int string_buffer_concat_value(StringBuffer *s, JSValueConst v);
static JSValue js_rebalance_string_rope(JSContext *ctx, JSValueConst rope);

/* op1 and op2 must be strings or string ropes */
static JSValue js_new_string_rope(JSContext *ctx, JSValue op1, JSValue op2)
{
    uint32_t len;
    int is_wide_char, depth;
    JSStringRope *r;
    JSValue res;

    if (JS_VALUE_GET_TAG(op1) == JS_TAG_STRING) {
        JSString *p1 = JS_VALUE_GET_STRING(op1);
        len = p1->len;
        is_wide_char = p1->is_wide_char;
        depth = 0;
    } else {
        JSStringRope *r1 = JS_VALUE_GET_STRING_ROPE(op1);
        len = r1->len;
        is_wide_char = r1->is_wide_char;
        depth = r1->depth;
    }

    if (JS_VALUE_GET_TAG(op2) == JS_TAG_STRING) {
        JSString *p2 = JS_VALUE_GET_STRING(op2);
        len += p2->len;
        is_wide_char |= p2->is_wide_char;
    } else {
        JSStringRope *r2 = JS_VALUE_GET_STRING_ROPE(op2);
        len += r2->len;
        is_wide_char |= r2->is_wide_char;
        depth = max_int(depth, r2->depth);
    }
    if (len > JS_STRING_LEN_MAX) {
        JS_ThrowInternalError(ctx, "string too long");
        goto fail;
    }
    r = js_malloc(ctx, sizeof(*r));
    if (!r)
        goto fail;
    JS_REF_COUNT(r) = 1;
    r->len = len;
    r->is_wide_char = is_wide_char;
    r->depth = depth + 1;
    r->left = op1;
    r->right = op2;
    res = JS_MKPTR(JS_TAG_STRING_ROPE, r);
    if (r->depth > JS_STRING_ROPE_MAX_DEPTH) {
        JSValue res2;
#ifdef DUMP_ROPE_REBALANCE
        printf("rebalance: initial depth=%d\n", r->depth);
#endif
        res2 = js_rebalance_string_rope(ctx, res);
#ifdef DUMP_ROPE_REBALANCE
        if (JS_VALUE_GET_TAG(res2) == JS_TAG_STRING_ROPE)
            printf("rebalance: final depth=%d\n", JS_VALUE_GET_STRING_ROPE(res2)->depth);
#endif
        JS_FreeValue(ctx, res);
        return res2;
    } else {
        return res;
    }
 fail:
    JS_FreeValue(ctx, op1);
    JS_FreeValue(ctx, op2);
    return JS_EXCEPTION;
}

#define ROPE_N_BUCKETS 44

/* Fibonacci numbers starting from F_2 */
static const uint32_t rope_bucket_len[ROPE_N_BUCKETS] = {
          1,          2,          3,          5,
          8,         13,         21,         34,
         55,         89,        144,        233,
        377,        610,        987,       1597,
       2584,       4181,       6765,      10946,
      17711,      28657,      46368,      75025,
     121393,     196418,     317811,     514229,
     832040,    1346269,    2178309,    3524578,
    5702887,    9227465,   14930352,   24157817,
   39088169,   63245986,  102334155,  165580141,
  267914296,  433494437,  701408733, 1134903170, /* > JS_STRING_LEN_MAX */
};

static int js_rebalance_string_rope_rec(JSContext *ctx, JSValue *buckets,
                                        JSValueConst val)
{
    if (JS_VALUE_GET_TAG(val) == JS_TAG_STRING) {
        JSString *p = JS_VALUE_GET_STRING(val);
        uint32_t len, i;
        JSValue a, b;

        len = p->len;
        if (len == 0)
            return 0; /* nothing to do */
        /* find the bucket i so that rope_bucket_len[i] <= len <
           rope_bucket_len[i + 1] and concatenate the ropes in the
           buckets before */
        a = JS_NULL;
        i = 0;
        while (len >= rope_bucket_len[i + 1]) {
            b = buckets[i];
            if (!JS_IsNull(b)) {
                buckets[i] = JS_NULL;
                if (JS_IsNull(a)) {
                    a = b;
                } else {
                    a = js_new_string_rope(ctx, b, a);
                    if (JS_IsException(a))
                        return -1;
                }
            }
            i++;
        }
        if (!JS_IsNull(a)) {
            a = js_new_string_rope(ctx, a, js_dup(val));
            if (JS_IsException(a))
                return -1;
        } else {
            a = js_dup(val);
        }
        while (!JS_IsNull(buckets[i])) {
            a = js_new_string_rope(ctx, buckets[i], a);
            buckets[i] = JS_NULL;
            if (JS_IsException(a))
                return -1;
            i++;
        }
        buckets[i] = a;
    } else {
        JSStringRope *r = JS_VALUE_GET_STRING_ROPE(val);
        if (js_rebalance_string_rope_rec(ctx, buckets, r->left))
            return -1;
        if (js_rebalance_string_rope_rec(ctx, buckets, r->right))
            return -1;
    }
    return 0;
}

/* Return a new rope which is balanced. Algorithm from "Ropes: an
   Alternative to Strings", Hans-J. Boehm, Russ Atkinson and Michael
   Plass. */
static JSValue js_rebalance_string_rope(JSContext *ctx, JSValueConst rope)
{
    JSValue buckets[ROPE_N_BUCKETS], a, b;
    int i;

    for(i = 0; i < ROPE_N_BUCKETS; i++)
        buckets[i] = JS_NULL;
    if (js_rebalance_string_rope_rec(ctx, buckets, rope))
        goto fail;
    a = JS_NULL;
    for(i = 0; i < ROPE_N_BUCKETS; i++) {
        b = buckets[i];
        if (!JS_IsNull(b)) {
            buckets[i] = JS_NULL;
            if (JS_IsNull(a)) {
                a = b;
            } else {
                a = js_new_string_rope(ctx, b, a);
                if (JS_IsException(a))
                    goto fail;
            }
        }
    }
    /* fail safe */
    if (JS_IsNull(a))
        return JS_AtomToString(ctx, JS_ATOM_empty_string);
    else
        return a;
 fail:
    for(i = 0; i < ROPE_N_BUCKETS; i++) {
        JS_FreeValue(ctx, buckets[i]);
    }
    return JS_EXCEPTION;
}

/* 'rope' must be a rope. return a string and modify the rope so that
   it won't need to be linearized again. */
static JSValue js_linearize_string_rope(JSContext *ctx, JSValueConst rope)
{
    StringBuffer b_s, *b = &b_s;
    JSStringRope *r;
    JSValue ret;

    r = JS_VALUE_GET_STRING_ROPE(rope);

    /* check whether it is already linearized */
    if (JS_VALUE_GET_TAG(r->right) == JS_TAG_STRING &&
        JS_VALUE_GET_STRING(r->right)->len == 0) {
        ret = js_dup(r->left);
        return ret;
    }
    if (string_buffer_init2(ctx, b, r->len, r->is_wide_char))
        goto fail;
    if (string_buffer_concat_value(b, rope))
        goto fail;
    ret = string_buffer_end(b);
    if (JS_REF_COUNT(r) > 1) {
        /* update the rope so that it won't need to be linearized again */
        JS_FreeValue(ctx, r->left);
        JS_FreeValue(ctx, r->right);
        r->left = js_dup(ret);
        r->right = JS_AtomToString(ctx, JS_ATOM_empty_string);
    }
    return ret;
 fail:
    return JS_EXCEPTION;
}

/* flat string concatenation - used by rope when concatenating short strings */
static JSValue JS_ConcatString2(JSContext *ctx, JSValue op1, JSValue op2);

static void copy_str16(uint16_t *dst, JSString *p, int offset, int len)
{
    if (p->is_wide_char) {
        memcpy(dst, str16(p) + offset, len * 2);
    } else {
        const uint8_t *src1 = str8(p) + offset;
        int i;

        for(i = 0; i < len; i++)
            dst[i] = src1[i];
    }
}

static JSValue JS_ConcatString1(JSContext *ctx, JSString *p1, JSString *p2)
{
    JSString *p;
    uint32_t len;
    int is_wide_char;

    len = p1->len + p2->len;
    if (len > JS_STRING_LEN_MAX)
        return JS_ThrowRangeError(ctx, "invalid string length");
    is_wide_char = p1->is_wide_char | p2->is_wide_char;
    p = js_alloc_string(ctx, len, is_wide_char);
    if (!p)
        return JS_EXCEPTION;
    if (!is_wide_char) {
        memcpy(str8(p), str8(p1), p1->len);
        memcpy(str8(p) + p1->len, str8(p2), p2->len);
        str8(p)[len] = '\0';
    } else {
        copy_str16(str16(p), p1, 0, p1->len);
        copy_str16(str16(p) + p1->len, p2, 0, p2->len);
    }
    return JS_MKPTR(JS_TAG_STRING, p);
}

/* flat string concatenation - op1 and op2 must be JS_TAG_STRING */
static JSValue JS_ConcatString2(JSContext *ctx, JSValue op1, JSValue op2)
{
    JSValue ret;
    JSString *p1, *p2;

    p1 = JS_VALUE_GET_STRING(op1);
    p2 = JS_VALUE_GET_STRING(op2);

    /* XXX: could also check if p1 is empty */
    if (p2->len == 0) {
        goto ret_op1;
    }
    if (JS_REF_COUNT(p1) == 1 && p1->is_wide_char == p2->is_wide_char
    &&  js_malloc_usable_size(ctx, p1) >= sizeof(*p1) + ((p1->len + p2->len) << p2->is_wide_char) + 1 - p1->is_wide_char) {
        /* Concatenate in place in available space at the end of p1 */
        if (p1->is_wide_char) {
            memcpy(str16(p1) + p1->len, str16(p2), p2->len << 1);
            p1->len += p2->len;
        } else {
            memcpy(str8(p1) + p1->len, str8(p2), p2->len);
            p1->len += p2->len;
            str8(p1)[p1->len] = '\0';
        }
    ret_op1:
        JS_FreeValue(ctx, op2);
        return op1;
    }
    ret = JS_ConcatString1(ctx, p1, p2);
    JS_FreeValue(ctx, op1);
    JS_FreeValue(ctx, op2);
    return ret;
}

/* op1 and op2 are converted to strings. For convenience, op1 or op2 =
   JS_EXCEPTION are accepted and return JS_EXCEPTION.  */
static JSValue JS_ConcatString(JSContext *ctx, JSValue op1, JSValue op2)
{
    JSString *p1, *p2;

    if (unlikely(!tag_is_string(JS_VALUE_GET_TAG(op1)))) {
        op1 = JS_ToStringFree(ctx, op1);
        if (JS_IsException(op1)) {
            JS_FreeValue(ctx, op2);
            return JS_EXCEPTION;
        }
    }
    if (unlikely(!tag_is_string(JS_VALUE_GET_TAG(op2)))) {
        op2 = JS_ToStringFree(ctx, op2);
        if (JS_IsException(op2)) {
            JS_FreeValue(ctx, op1);
            return JS_EXCEPTION;
        }
    }

    /* normal concatenation for short strings */
    if (JS_VALUE_GET_TAG(op2) == JS_TAG_STRING) {
        p2 = JS_VALUE_GET_STRING(op2);
        if (p2->len == 0) {
            JS_FreeValue(ctx, op2);
            return op1;
        }
        if (p2->len <= JS_STRING_ROPE_SHORT_LEN) {
            if (JS_VALUE_GET_TAG(op1) == JS_TAG_STRING) {
                p1 = JS_VALUE_GET_STRING(op1);
                if (p1->len <= JS_STRING_ROPE_SHORT2_LEN) {
                    return JS_ConcatString2(ctx, op1, op2);
                } else {
                    return js_new_string_rope(ctx, op1, op2);
                }
            } else {
                JSStringRope *r1;
                r1 = JS_VALUE_GET_STRING_ROPE(op1);
                if (JS_VALUE_GET_TAG(r1->right) == JS_TAG_STRING &&
                    JS_VALUE_GET_STRING(r1->right)->len <= JS_STRING_ROPE_SHORT_LEN) {
                    JSValue val, ret;
                    val = JS_ConcatString2(ctx, js_dup(r1->right), op2);
                    if (JS_IsException(val)) {
                        JS_FreeValue(ctx, op1);
                        return JS_EXCEPTION;
                    }
                    ret = js_new_string_rope(ctx, js_dup(r1->left), val);
                    JS_FreeValue(ctx, op1);
                    return ret;
                }
            }
        }
    } else if (JS_VALUE_GET_TAG(op1) == JS_TAG_STRING) {
        JSStringRope *r2;
        p1 = JS_VALUE_GET_STRING(op1);
        if (p1->len == 0) {
            JS_FreeValue(ctx, op1);
            return op2;
        }
        r2 = JS_VALUE_GET_STRING_ROPE(op2);
        if (JS_VALUE_GET_TAG(r2->left) == JS_TAG_STRING &&
            JS_VALUE_GET_STRING(r2->left)->len <= JS_STRING_ROPE_SHORT_LEN) {
            JSValue val, ret;
            val = JS_ConcatString2(ctx, op1, js_dup(r2->left));
            if (JS_IsException(val)) {
                JS_FreeValue(ctx, op2);
                return JS_EXCEPTION;
            }
            ret = js_new_string_rope(ctx, val, js_dup(r2->right));
            JS_FreeValue(ctx, op2);
            return ret;
        }
    }
    return js_new_string_rope(ctx, op1, op2);
}

