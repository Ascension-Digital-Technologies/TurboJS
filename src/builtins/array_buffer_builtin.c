/* Engine domain source: builtins/bigint_typedarray.inc -> array_buffer_builtin.
 * Ownership: builtins subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static JSValue js_typed_array_at(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    JSObject *p;
    int64_t idx, len;

    p = get_typed_array(ctx, this_val);
    if (!p)
        return JS_EXCEPTION;
    if (typed_array_is_oob(p))
        return JS_ThrowTypeErrorArrayBufferOOB(ctx);
    len = p->u.array.count;

    // note: can change p->u.array.count
    if (JS_ToInt64Sat(ctx, &idx, argv[0]))
        return JS_EXCEPTION;

    if (idx < 0)
        idx = len + idx;

    if (idx < 0 || idx >= p->u.array.count)
        return JS_UNDEFINED;

    switch (p->class_id) {
    case JS_CLASS_INT8_ARRAY:
        return js_int32(p->u.array.u.int8_ptr[idx]);
    case JS_CLASS_UINT8C_ARRAY:
    case JS_CLASS_UINT8_ARRAY:
        return js_int32(p->u.array.u.uint8_ptr[idx]);
    case JS_CLASS_INT16_ARRAY:
        return js_int32(p->u.array.u.int16_ptr[idx]);
    case JS_CLASS_UINT16_ARRAY:
        return js_int32(p->u.array.u.uint16_ptr[idx]);
    case JS_CLASS_INT32_ARRAY:
        return js_int32(p->u.array.u.int32_ptr[idx]);
    case JS_CLASS_UINT32_ARRAY:
        return js_uint32(p->u.array.u.uint32_ptr[idx]);
    case JS_CLASS_FLOAT16_ARRAY:
        return js_float64(fromfp16(p->u.array.u.fp16_ptr[idx]));
    case JS_CLASS_FLOAT32_ARRAY:
        return js_float64(p->u.array.u.float_ptr[idx]);
    case JS_CLASS_FLOAT64_ARRAY:
        return js_float64(p->u.array.u.double_ptr[idx]);
    case JS_CLASS_BIG_INT64_ARRAY:
        return JS_NewBigInt64(ctx, p->u.array.u.int64_ptr[idx]);
    case JS_CLASS_BIG_UINT64_ARRAY:
        return JS_NewBigUint64(ctx, p->u.array.u.uint64_ptr[idx]);
    }

    abort(); /* unreachable */
    return JS_UNDEFINED;
}

static JSValue js_typed_array_with(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    JSValue arr, val, buffer;
    JSObject *p;
    JSTypedArray *ta;
    JSArrayBuffer *src_abuf, *abuf;
    int64_t idx;
    uint32_t len, newlen, copy_len;
    int size_log2;

    p = get_typed_array(ctx, this_val);
    if (!p)
        return JS_EXCEPTION;

    len = p->u.array.count;
    if (JS_ToInt64Sat(ctx, &idx, argv[0]))
        return JS_EXCEPTION;
    /* resolve negative index using original length (spec step 5-6) */
    if (idx < 0)
        idx = len + idx;

    val = JS_ToPrimitive(ctx, argv[1], HINT_NUMBER);
    if (JS_IsException(val))
        return JS_EXCEPTION;

    /* re-validate after user code (spec step 9: IsValidIntegerIndex) */
    if (typed_array_is_oob(p)) {
        JS_FreeValue(ctx, val);
        return JS_ThrowTypeErrorArrayBufferOOB(ctx);
    }
    newlen = p->u.array.count;
    if (idx < 0 || idx >= newlen) {
        JS_FreeValue(ctx, val);
        return JS_ThrowRangeError(ctx, "invalid typed array index");
    }

    copy_len = min_uint32(len, newlen);
    size_log2 = typed_array_size_log2(p->class_id);

    /* create new typed array with original length (zero-initialized) */
    arr = js_create_from_ctor(ctx, JS_UNDEFINED, p->class_id);
    if (JS_IsException(arr)) {
        JS_FreeValue(ctx, val);
        return JS_EXCEPTION;
    }
    buffer = js_array_buffer_constructor1(ctx, JS_UNDEFINED,
                                          (uint64_t)len << size_log2,
                                          NULL);
    if (JS_IsException(buffer)) {
        JS_FreeValue(ctx, val);
        JS_FreeValue(ctx, arr);
        return JS_EXCEPTION;
    }
    if (typed_array_is_oob(p)) {
        JS_FreeValue(ctx, val);
        JS_FreeValue(ctx, buffer);
        JS_FreeValue(ctx, arr);
        return JS_ThrowTypeErrorArrayBufferOOB(ctx);
    }
    abuf = JS_GetOpaque(buffer, JS_CLASS_ARRAY_BUFFER);
    if (typed_array_init(ctx, arr, buffer, 0, len, /*track_rab*/false)) {
        JS_FreeValue(ctx, val);
        JS_FreeValue(ctx, arr);
        return JS_EXCEPTION;
    }

    /* copy min(len, newlen) elements from source; rest stays zero */
    if (copy_len > 0) {
        ta = p->u.typed_array;
        src_abuf = ta->buffer->u.array_buffer;
        memcpy(abuf->data, src_abuf->data + ta->offset,
               (size_t)copy_len << size_log2);
    }

    if (JS_SetPropertyInt64(ctx, arr, idx, val) < 0) {
        JS_FreeValue(ctx, arr);
        return JS_EXCEPTION;
    }
    return arr;
}

static JSValue js_typed_array_set(JSContext *ctx,
                                  JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    JSValueConst offset = JS_UNDEFINED;
    if (argc > 1) {
        offset = argv[1];
    }
    return js_typed_array_set_internal(ctx, this_val, argv[0], offset);
}

static JSValue js_create_typed_array_iterator(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv, int magic)
{
    if (validate_typed_array(ctx, this_val))
        return JS_EXCEPTION;
    return js_create_array_iterator(ctx, this_val, argc, argv, magic);
}

static JSValue js_typed_array_create(JSContext *ctx, JSValueConst ctor,
                                     int argc, JSValueConst *argv,
                                     bool require_mutable)
{
    JSValue ret;
    int new_len;
    int64_t len;

    ret = JS_CallConstructor(ctx, ctor, argc, argv);
    if (JS_IsException(ret))
        return ret;
    /* validate the typed array */
    new_len = js_typed_array_get_length_unsafe(ctx, ret);
    if (new_len < 0)
        goto fail;
    if (require_mutable && typed_array_is_immutable(JS_VALUE_GET_OBJ(ret))) {
        JS_ThrowTypeErrorImmutableArrayBuffer(ctx);
        goto fail;
    }
    if (argc == 1) {
        /* ensure that it is large enough */
        if (JS_ToLengthFree(ctx, &len, js_dup(argv[0])))
            goto fail;
        if (new_len < len) {
            JS_ThrowTypeError(ctx, "TypedArray length is too small");
        fail:
            JS_FreeValue(ctx, ret);
            return JS_EXCEPTION;
        }
    }
    return ret;
}

static JSValue js_typed_array___speciesCreate(JSContext *ctx,
                                              JSValueConst this_val,
                                              int argc, JSValueConst *argv,
                                              bool require_mutable)
{
    JSValueConst obj;
    JSObject *p;
    JSValue ctor, ret;
    int argc1;

    obj = argv[0];
    p = get_typed_array(ctx, obj);
    if (!p)
        return JS_EXCEPTION;
    ctor = JS_SpeciesConstructor(ctx, obj, JS_UNDEFINED);
    if (JS_IsException(ctor))
        return ctor;
    argc1 = max_int(argc - 1, 0);
    if (JS_IsUndefined(ctor)) {
        ret = js_typed_array_constructor(ctx, JS_UNDEFINED, argc1, argv + 1,
                                         p->class_id);
    } else {
        ret = js_typed_array_create(ctx, ctor, argc1, argv + 1, require_mutable);
        JS_FreeValue(ctx, ctor);
    }
    return ret;
}

static JSValue js_typed_array_from(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    // from(items, mapfn = void 0, this_arg = void 0)
    JSValueConst items = argv[0], mapfn, this_arg;
    JSValueConst args[2];
    JSValue stack[2];
    JSValue iter, arr, r, v, v2;
    int64_t k, len;
    int done, mapping;

    mapping = false;
    mapfn = JS_UNDEFINED;
    this_arg = JS_UNDEFINED;
    r = JS_UNDEFINED;
    arr = JS_UNDEFINED;
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
    if (!JS_IsUndefined(iter) && !JS_IsNull(iter)) {
        if (!JS_IsFunction(ctx, iter)) {
            JS_FreeValue(ctx, iter);
            JS_ThrowTypeError(ctx, "value is not iterable");
            goto exception;
        }
        arr = JS_NewArray(ctx);
        if (JS_IsException(arr)) {
            JS_FreeValue(ctx, iter);
            goto exception;
        }
        stack[0] = JS_GetIterator2(ctx, items, iter);
        JS_FreeValue(ctx, iter);
        if (JS_IsException(stack[0]))
            goto exception;
        stack[1] = JS_GetProperty(ctx, stack[0], JS_ATOM_next);
        if (JS_IsException(stack[1]))
            goto exception_close;
        for (k = 0;; k++) {
            v = JS_IteratorNext(ctx, stack[0], stack[1], 0, NULL, &done);
            if (JS_IsException(v))
                goto exception_close;
            if (done)
                break;
            if (JS_DefinePropertyValueInt64(ctx, arr, k, v, JS_PROP_C_W_E | JS_PROP_THROW) < 0)
                goto exception_close;
        }
    } else {
        arr = JS_ToObject(ctx, items);
        if (JS_IsException(arr))
            goto exception;
    }
    if (js_get_length64(ctx, &len, arr) < 0)
        goto exception;
    v = js_int64(len);
    r = js_typed_array_create(ctx, this_val, 1, vc(&v), true);
    JS_FreeValue(ctx, v);
    if (JS_IsException(r))
        goto exception;
    for(k = 0; k < len; k++) {
        v = JS_GetPropertyInt64(ctx, arr, k);
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
        if (JS_SetPropertyInt64(ctx, r, k, v) < 0)
            goto exception;
    }
    goto done;

 exception_close:
    if (!JS_IsUndefined(stack[0]))
        JS_IteratorClose(ctx, stack[0], true);
 exception:
    JS_FreeValue(ctx, r);
    r = JS_EXCEPTION;
 done:
    JS_FreeValue(ctx, arr);
    JS_FreeValue(ctx, stack[0]);
    JS_FreeValue(ctx, stack[1]);
    return r;
}

static JSValue js_typed_array_of(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    JSValue v, obj;
    int i;

    v = js_int32(argc);
    obj = js_typed_array_create(ctx, this_val, 1, vc(&v), true);
    if (JS_IsException(obj))
        return obj;

    for(i = 0; i < argc; i++) {
        if (JS_SetPropertyUint32(ctx, obj, i, js_dup(argv[i])) < 0) {
            JS_FreeValue(ctx, obj);
            return JS_EXCEPTION;
        }
    }
    return obj;
}

static JSValue js_typed_array_copyWithin(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    JSObject *p;
    int len, to, from, final, count, shift, space;

    p = get_typed_array(ctx, this_val);
    if (!p)
        return JS_EXCEPTION;
    if (typed_array_is_immutable(p))
        return JS_ThrowTypeErrorImmutableArrayBuffer(ctx);
    if (typed_array_is_oob(p))
        return JS_ThrowTypeErrorArrayBufferOOB(ctx);
    len = p->u.array.count;

    if (JS_ToInt32Clamp(ctx, &to, argv[0], 0, len, len))
        return JS_EXCEPTION;

    if (JS_ToInt32Clamp(ctx, &from, argv[1], 0, len, len))
        return JS_EXCEPTION;

    final = len;
    if (argc > 2 && !JS_IsUndefined(argv[2])) {
        if (JS_ToInt32Clamp(ctx, &final, argv[2], 0, len, len))
            return JS_EXCEPTION;
    }

    if (typed_array_is_oob(p))
        return JS_ThrowTypeErrorArrayBufferOOB(ctx);

    // RAB may have been resized by evil .valueOf method
    space = p->u.array.count - max_int(to, from);
    count = min_int(final - from, len - to);
    count = min_int(count, space);
    if (count > 0) {
        shift = typed_array_size_log2(p->class_id);
        memmove(p->u.array.u.uint8_ptr + (to << shift),
                p->u.array.u.uint8_ptr + (from << shift),
                count << shift);
    }
    return js_dup(this_val);
}

static JSValue js_typed_array_fill(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    JSObject *p;
    int len, k, final, shift;
    uint64_t v64;

    p = get_typed_array(ctx, this_val);
    if (!p)
        return JS_EXCEPTION;
    if (typed_array_is_immutable(p))
        return JS_ThrowTypeErrorImmutableArrayBuffer(ctx);
    if (typed_array_is_oob(p))
        return JS_ThrowTypeErrorArrayBufferOOB(ctx);
    len = p->u.array.count;

    if (p->class_id == JS_CLASS_UINT8C_ARRAY) {
        int32_t v;
        if (JS_ToUint8ClampFree(ctx, &v, js_dup(argv[0])))
            return JS_EXCEPTION;
        v64 = v;
    } else if (p->class_id <= JS_CLASS_UINT32_ARRAY) {
        uint32_t v;
        if (JS_ToUint32(ctx, &v, argv[0]))
            return JS_EXCEPTION;
        v64 = v;
    } else
    if (p->class_id <= JS_CLASS_BIG_UINT64_ARRAY) {
        if (JS_ToBigInt64(ctx, (int64_t *)&v64, argv[0]))
            return JS_EXCEPTION;
    } else {
        double d;
        if (JS_ToFloat64(ctx, &d, argv[0]))
            return JS_EXCEPTION;
        if (p->class_id == JS_CLASS_FLOAT16_ARRAY) {
            v64 = tofp16(d);
        } else if (p->class_id == JS_CLASS_FLOAT32_ARRAY) {
            union {
                float f;
                uint32_t u32;
            } u;
            u.f = d;
            v64 = u.u32;
        } else {
            JSFloat64Union u;
            u.d = d;
            v64 = u.u64;
        }
    }

    k = 0;
    if (argc > 1) {
        if (JS_ToInt32Clamp(ctx, &k, argv[1], 0, len, len))
            return JS_EXCEPTION;
    }

    final = len;
    if (argc > 2 && !JS_IsUndefined(argv[2])) {
        if (JS_ToInt32Clamp(ctx, &final, argv[2], 0, len, len))
            return JS_EXCEPTION;
    }

    if (typed_array_is_oob(p))
        return JS_ThrowTypeErrorArrayBufferOOB(ctx);

    // RAB may have been resized by evil .valueOf method
    final = min_int(final, p->u.array.count);
    shift = typed_array_size_log2(p->class_id);
    switch(shift) {
    case 0:
        if (k < final) {
            memset(p->u.array.u.uint8_ptr + k, v64, final - k);
        }
        break;
    case 1:
        for(; k < final; k++) {
            p->u.array.u.uint16_ptr[k] = v64;
        }
        break;
    case 2:
        for(; k < final; k++) {
            p->u.array.u.uint32_ptr[k] = v64;
        }
        break;
    case 3:
        for(; k < final; k++) {
            p->u.array.u.uint64_ptr[k] = v64;
        }
        break;
    default:
        abort();
    }
    return js_dup(this_val);
}

static JSValue js_typed_array_find(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv, int mode)
{
    JSValueConst func, this_arg, args[3];
    JSValue val, index_val, res;
    int len, k, end;
    int dir;

    val = JS_UNDEFINED;
    len = js_typed_array_get_length_unsafe(ctx, this_val);
    if (len < 0)
        goto exception;

    func = argv[0];
    if (check_function(ctx, func))
        goto exception;

    this_arg = JS_UNDEFINED;
    if (argc > 1)
        this_arg = argv[1];

    k = 0;
    dir = 1;
    end = len;
    if (mode == ArrayFindLast || mode == ArrayFindLastIndex) {
        k = len - 1;
        dir = -1;
        end = -1;
    }

    for(; k != end; k += dir) {
        index_val = js_int32(k);
        val = JS_GetPropertyValue(ctx, this_val, index_val);
        if (JS_IsException(val))
            goto exception;
        args[0] = val;
        args[1] = index_val;
        args[2] = this_val;
        res = JS_Call(ctx, func, this_arg, 3, args);
        if (JS_IsException(res))
            goto exception;
        if (JS_ToBoolFree(ctx, res)) {
            if (mode == ArrayFindIndex || mode == ArrayFindLastIndex) {
                JS_FreeValue(ctx, val);
                return index_val;
            } else {
                return val;
            }
        }
        JS_FreeValue(ctx, val);
    }
    if (mode == ArrayFindIndex || mode == ArrayFindLastIndex)
        return js_int32(-1);
    else
        return JS_UNDEFINED;

exception:
    JS_FreeValue(ctx, val);
    return JS_EXCEPTION;
}

#define special_indexOf 0
#define special_lastIndexOf 1
#define special_includes -1

static JSValue js_typed_array_indexOf(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv, int special)
{
    JSObject *p;
    int len, tag, is_int, is_bigint, k, stop, inc, res = -1;
    int64_t v64;
    double d;
    float f;
    uint16_t hf;

    p = get_typed_array(ctx, this_val);
    if (!p)
        return JS_EXCEPTION;
    if (typed_array_is_oob(p))
        return JS_ThrowTypeErrorArrayBufferOOB(ctx);
    len = p->u.array.count;

    if (len == 0)
        goto done;

    if (special == special_lastIndexOf) {
        k = len - 1;
        if (argc > 1) {
            int64_t k1;
            if (JS_ToInt64Clamp(ctx, &k1, argv[1], -1, len - 1, len))
                goto exception;
            k = k1;
            if (k < 0)
                goto done;
        }
        stop = -1;
        inc = -1;
    } else {
        k = 0;
        if (argc > 1) {
            if (JS_ToInt32Sat(ctx, &k, argv[1]))
                goto exception;
            if (k < 0) {
                k += len;
                if (k < 0)
                    k = 0;
            } else if (k > len) {
                k = len;
            }
        }
        stop = len;
        inc = 1;
    }

    /* if the array was detached, no need to go further (but no
       exception is raised) */
    if (typed_array_is_oob(p) || len > p->u.array.count) {
        /* "includes" scans all the properties, so "undefined" can match */
        if (special == special_includes)
            if (JS_IsUndefined(argv[0]))
                if (k < typed_array_length(p))
                    res = 0;
        goto done;
    }

    // RAB may have been resized by evil .valueOf method
    len = min_int(len, p->u.array.count);
    if (len == 0)
        goto done;
    k = min_int(k, len);
    stop = min_int(stop, len);

    is_bigint = 0;
    is_int = 0; /* avoid warning */
    v64 = 0; /* avoid warning */
    tag = JS_VALUE_GET_NORM_TAG(argv[0]);
    if (tag == JS_TAG_INT) {
        is_int = 1;
        v64 = JS_VALUE_GET_INT(argv[0]);
        d = v64;
    } else
    if (tag == JS_TAG_FLOAT64) {
        d = JS_VALUE_GET_FLOAT64(argv[0]);
        if (d >= INT64_MIN && d < 0x1p63) {
            v64 = d;
            is_int = (v64 == d);
        }
    } else if (tag == JS_TAG_BIG_INT || tag == JS_TAG_SHORT_BIG_INT) {
        JSBigIntBuf buf1;
        JSBigInt *p1;
        int sz = (64 / JS_LIMB_BITS);
        if (tag == JS_TAG_SHORT_BIG_INT)
            p1 = js_bigint_set_short(&buf1, argv[0]);
        else
            p1 = JS_VALUE_GET_PTR(argv[0]);

        if (p->class_id == JS_CLASS_BIG_INT64_ARRAY) {
            if (p1->len > sz)
                goto done; /* does not fit an int64 : cannot be found */
        } else if (p->class_id == JS_CLASS_BIG_UINT64_ARRAY) {
            if (js_bigint_sign(p1))
                goto done; /* v < 0 */
            if (p1->len <= sz) {
                /* OK */
            } else if (p1->len == sz + 1 && p1->tab[sz] == 0) {
                /* 2^63 <= v <= 2^64-1 */
            } else {
                goto done;
            }
        } else {
            goto done;
        }
        if (JS_ToBigInt64(ctx, &v64, argv[0]))
            goto exception;
        d = 0;
        is_bigint = 1;
    } else {
        goto done;
    }

    switch (p->class_id) {
    case JS_CLASS_INT8_ARRAY:
        if (is_int && (int8_t)v64 == v64)
            goto scan8;
        break;
    case JS_CLASS_UINT8C_ARRAY:
    case JS_CLASS_UINT8_ARRAY:
        if (is_int && (uint8_t)v64 == v64) {
            const uint8_t *pv, *pp;
            uint16_t v;
        scan8:
            pv = p->u.array.u.uint8_ptr;
            v = v64;
            if (inc > 0) {
                pp = NULL;
                if (pv)
                    pp = memchr(pv + k, v, len - k);
                if (pp)
                    res = pp - pv;
            } else {
                for (; k != stop; k += inc) {
                    if (pv[k] == v) {
                        res = k;
                        break;
                    }
                }
            }
        }
        break;
    case JS_CLASS_INT16_ARRAY:
        if (is_int && (int16_t)v64 == v64)
            goto scan16;
        break;
    case JS_CLASS_UINT16_ARRAY:
        if (is_int && (uint16_t)v64 == v64) {
            const uint16_t *pv;
            uint16_t v;
        scan16:
            pv = p->u.array.u.uint16_ptr;
            v = v64;
            for (; k != stop; k += inc) {
                if (pv[k] == v) {
                    res = k;
                    break;
                }
            }
        }
        break;
    case JS_CLASS_INT32_ARRAY:
        if (is_int && (int32_t)v64 == v64)
            goto scan32;
        break;
    case JS_CLASS_UINT32_ARRAY:
        if (is_int && (uint32_t)v64 == v64) {
            const uint32_t *pv;
            uint32_t v;
        scan32:
            pv = p->u.array.u.uint32_ptr;
            v = v64;
            for (; k != stop; k += inc) {
                if (pv[k] == v) {
                    res = k;
                    break;
                }
            }
        }
        break;
    case JS_CLASS_FLOAT16_ARRAY:
        if (is_bigint)
            break;
        if (isnan(d)) {
            const uint16_t *pv = p->u.array.u.fp16_ptr;
            /* special case: indexOf returns -1, includes finds NaN */
            if (special != special_includes)
                goto done;
            for (; k != stop; k += inc) {
                if (isfp16nan(pv[k])) {
                    res = k;
                    break;
                }
            }
        } else if (d == 0) {
            // special case: includes also finds negative zero
            const uint16_t *pv = p->u.array.u.fp16_ptr;
            for (; k != stop; k += inc) {
                if (isfp16zero(pv[k])) {
                    res = k;
                    break;
                }
            }
        } else if (hf = tofp16(d), d == fromfp16(hf)) {
            const uint16_t *pv = p->u.array.u.fp16_ptr;
            for (; k != stop; k += inc) {
                if (pv[k] == hf) {
                    res = k;
                    break;
                }
            }
        }
        break;
    case JS_CLASS_FLOAT32_ARRAY:
        if (is_bigint)
            break;
        if (isnan(d)) {
            const float *pv = p->u.array.u.float_ptr;
            /* special case: indexOf returns -1, includes finds NaN */
            if (special != special_includes)
                goto done;
            for (; k != stop; k += inc) {
                if (isnan(pv[k])) {
                    res = k;
                    break;
                }
            }
        } else if ((f = (float)d) == d) {
            const float *pv = p->u.array.u.float_ptr;
            for (; k != stop; k += inc) {
                if (pv[k] == f) {
                    res = k;
                    break;
                }
            }
        }
        break;
    case JS_CLASS_FLOAT64_ARRAY:
        if (is_bigint)
            break;
        if (isnan(d)) {
            const double *pv = p->u.array.u.double_ptr;
            /* special case: indexOf returns -1, includes finds NaN */
            if (special != special_includes)
                goto done;
            for (; k != stop; k += inc) {
                if (isnan(pv[k])) {
                    res = k;
                    break;
                }
            }
        } else {
            const double *pv = p->u.array.u.double_ptr;
            for (; k != stop; k += inc) {
                if (pv[k] == d) {
                    res = k;
                    break;
                }
            }
        }
        break;
    case JS_CLASS_BIG_INT64_ARRAY:
        if (is_bigint) {
            goto scan64;
        }
        break;
    case JS_CLASS_BIG_UINT64_ARRAY:
        if (is_bigint) {
            const uint64_t *pv;
            uint64_t v;
        scan64:
            pv = p->u.array.u.uint64_ptr;
            v = v64;
            for (; k != stop; k += inc) {
                if (pv[k] == v) {
                    res = k;
                    break;
                }
            }
        }
        break;
    }

done:
    if (special == special_includes)
        return js_bool(res >= 0);
    else
        return js_int32(res);

exception:
    return JS_EXCEPTION;
}

static JSValue js_typed_array_join(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv, int toLocaleString)
{
    JSValue sep = JS_UNDEFINED, el;
    StringBuffer b_s, *b = &b_s;
    JSString *s = NULL;
    JSObject *p;
    int i, len, oldlen, newlen;
    int c;

    p = get_typed_array(ctx, this_val);
    if (!p)
        return JS_EXCEPTION;
    if (typed_array_is_oob(p))
        return JS_ThrowTypeErrorArrayBufferOOB(ctx);
    len = oldlen = newlen = p->u.array.count;

    c = ',';    /* default separator */
    if (!toLocaleString && argc > 0 && !JS_IsUndefined(argv[0])) {
        sep = JS_ToString(ctx, argv[0]);
        if (JS_IsException(sep))
            goto exception;
        s = JS_VALUE_GET_STRING(sep);
        if (s->len == 1 && !s->is_wide_char)
            c = str8(s)[0];
        else
            c = -1;
        // ToString(sep) can detach or resize the arraybuffer as a side effect
        newlen = p->u.array.count;
        len = min_int(len, newlen);
    }
    string_buffer_init(ctx, b, 0);

    /* XXX: optimize with direct access */
    for(i = 0; i < len; i++) {
        if (i > 0) {
            if (c >= 0) {
                if (string_buffer_putc8(b, c))
                    goto fail;
            } else {
                if (string_buffer_concat(b, s, 0, s->len))
                    goto fail;
            }
        }
        el = JS_GetPropertyUint32(ctx, this_val, i);
        /* Can return undefined for example if the typed array is detached */
        if (!JS_IsNull(el) && !JS_IsUndefined(el)) {
            if (JS_IsException(el))
                goto fail;
            if (toLocaleString) {
                el = JS_ToLocaleStringFree(ctx, el);
            }
            if (string_buffer_concat_value_free(b, el))
                goto fail;
        }
    }

    // add extra separators in case RAB was resized by evil .valueOf method
    i = max_int(1, newlen);
    for(/*empty*/; i < oldlen; i++) {
        if (c >= 0) {
            if (string_buffer_putc8(b, c))
                goto fail;
        } else {
            if (string_buffer_concat(b, s, 0, s->len))
                goto fail;
        }
    }

    JS_FreeValue(ctx, sep);
    return string_buffer_end(b);

fail:
    string_buffer_free(b);
    JS_FreeValue(ctx, sep);
exception:
    return JS_EXCEPTION;
}

static JSValue js_typed_array_reverse(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    JSObject *p;
    int len;

    len = js_typed_array_get_length_unsafe(ctx, this_val);
    if (len < 0)
        return JS_EXCEPTION;
    p = JS_VALUE_GET_OBJ(this_val);
    if (typed_array_is_immutable(p))
        return JS_ThrowTypeErrorImmutableArrayBuffer(ctx);
    if (len > 0) {
        switch (typed_array_size_log2(p->class_id)) {
        case 0:
            {
                uint8_t *p1 = p->u.array.u.uint8_ptr;
                uint8_t *p2 = p1 + len - 1;
                while (p1 < p2) {
                    uint8_t v = *p1;
                    *p1++ = *p2;
                    *p2-- = v;
                }
            }
            break;
        case 1:
            {
                uint16_t *p1 = p->u.array.u.uint16_ptr;
                uint16_t *p2 = p1 + len - 1;
                while (p1 < p2) {
                    uint16_t v = *p1;
                    *p1++ = *p2;
                    *p2-- = v;
                }
            }
            break;
        case 2:
            {
                uint32_t *p1 = p->u.array.u.uint32_ptr;
                uint32_t *p2 = p1 + len - 1;
                while (p1 < p2) {
                    uint32_t v = *p1;
                    *p1++ = *p2;
                    *p2-- = v;
                }
            }
            break;
        case 3:
            {
                uint64_t *p1 = p->u.array.u.uint64_ptr;
                uint64_t *p2 = p1 + len - 1;
                while (p1 < p2) {
                    uint64_t v = *p1;
                    *p1++ = *p2;
                    *p2-- = v;
                }
            }
            break;
        default:
            abort();
        }
    }
    return js_dup(this_val);
}

static JSValue js_typed_array_toReversed(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    JSValue arr, ret;
    JSObject *p;

    p = get_typed_array(ctx, this_val);
    if (!p)
        return JS_EXCEPTION;
    arr = js_typed_array_constructor_ta(ctx, JS_UNDEFINED, this_val,
                                        p->class_id, p->u.array.count);
    if (JS_IsException(arr))
        return JS_EXCEPTION;
    ret = js_typed_array_reverse(ctx, arr, argc, argv);
    JS_FreeValue(ctx, arr);
    return ret;
}

static JSValue js_typed_array_slice(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    JSValueConst args[2];
    JSValue arr, val;
    JSObject *p, *p1;
    int n, len, start, final, count, shift, space;

    arr = JS_UNDEFINED;
    p = get_typed_array(ctx, this_val);
    if (!p)
        return JS_EXCEPTION;
    if (typed_array_is_oob(p))
        return JS_ThrowTypeErrorArrayBufferOOB(ctx);
    len = p->u.array.count;

    if (JS_ToInt32Clamp(ctx, &start, argv[0], 0, len, len))
        goto exception;
    final = len;
    if (!JS_IsUndefined(argv[1])) {
        if (JS_ToInt32Clamp(ctx, &final, argv[1], 0, len, len))
            goto exception;
    }
    count = max_int(final - start, 0);

    args[0] = this_val;
    args[1] = js_int32(count);
    arr = js_typed_array___speciesCreate(ctx, JS_UNDEFINED, 2, args, true);
    if (JS_IsException(arr))
        goto exception;

    if (count > 0) {
        if (validate_typed_array(ctx, this_val)
        ||  validate_typed_array(ctx, arr))
            goto exception;

        if (len != p->u.array.count)
            goto slow_path;

        p1 = get_typed_array(ctx, arr);
        if (p1 != NULL && p->class_id == p1->class_id &&
            typed_array_length(p1) >= count &&
            typed_array_length(p) >= start + count) {
            shift = typed_array_size_log2(p->class_id);
            memmove(p1->u.array.u.uint8_ptr,
                    p->u.array.u.uint8_ptr + (start << shift),
                    count << shift);
        } else {
        slow_path:
            space = max_int(0, p->u.array.count - start);
            count = min_int(count, space);
            for (n = 0; n < count; n++) {
                val = JS_GetPropertyValue(ctx, this_val, js_int32(start + n));
                if (JS_IsException(val))
                    goto exception;
                if (JS_SetPropertyValue(ctx, arr, js_int32(n), val,
                                        JS_PROP_THROW) < 0)
                    goto exception;
            }
        }
    }
    return arr;

 exception:
    JS_FreeValue(ctx, arr);
    return JS_EXCEPTION;
}

static JSValue js_typed_array_subarray(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    JSArrayBuffer *abuf;
    JSTypedArray *ta;
    JSValueConst args[4];
    JSValue arr, byteOffset, ta_buffer;
    JSObject *p;
    int len, start, final, count, shift, offset;

    p = get_typed_array(ctx, this_val);
    if (!p)
        goto exception;
    len = p->u.array.count;
    if (JS_ToInt32Clamp(ctx, &start, argv[0], 0, len, len))
        goto exception;
    final = len;
    if (!JS_IsUndefined(argv[1])) {
        if (JS_ToInt32Clamp(ctx, &final, argv[1], 0, len, len))
            goto exception;
    }
    count = max_int(final - start, 0);
    byteOffset = js_typed_array_get_byteOffset(ctx, this_val);
    if (JS_IsException(byteOffset))
        goto exception;
    ta = p->u.typed_array;
    abuf = ta->buffer->u.array_buffer;
    if (ta->offset > abuf->byte_length)
        goto range_error;
    if (ta->offset == abuf->byte_length && count > 0) {
    range_error:
        JS_ThrowRangeError(ctx, "invalid offset");
        goto exception;
    }
    shift = typed_array_size_log2(p->class_id);
    offset = JS_VALUE_GET_INT(byteOffset) + (start << shift);
    JS_FreeValue(ctx, byteOffset);
    ta_buffer = js_typed_array_get_buffer(ctx, this_val);
    if (JS_IsException(ta_buffer))
        goto exception;
    args[0] = this_val;
    args[1] = safe_const(ta_buffer);
    args[2] = safe_const(js_int32(offset));
    args[3] = safe_const(js_int32(count));
    // result is length-tracking if source TA is and no explicit count is given
    if (ta->track_rab && JS_IsUndefined(argv[1]))
        args[3] = JS_UNDEFINED;
    arr = js_typed_array___speciesCreate(ctx, JS_UNDEFINED, 4, args, false);
    JS_FreeValue(ctx, ta_buffer);
    return arr;
 exception:
    return JS_EXCEPTION;
}

/* TypedArray.prototype.sort */

static int js_cmp_doubles(double x, double y)
{
    if (isnan(x))    return isnan(y) ? 0 : +1;
    if (isnan(y))    return -1;
    if (x < y)       return -1;
    if (x > y)       return 1;
    if (x != 0)      return 0;
    if (signbit(x))  return signbit(y) ? 0 : -1;
    else             return signbit(y) ? 1 : 0;
}

static int js_TA_cmp_int8(const void *a, const void *b, void *opaque) {
    return *(const int8_t *)a - *(const int8_t *)b;
}

static int js_TA_cmp_uint8(const void *a, const void *b, void *opaque) {
    return *(const uint8_t *)a - *(const uint8_t *)b;
}

static int js_TA_cmp_int16(const void *a, const void *b, void *opaque) {
    return *(const int16_t *)a - *(const int16_t *)b;
}

static int js_TA_cmp_uint16(const void *a, const void *b, void *opaque) {
    return *(const uint16_t *)a - *(const uint16_t *)b;
}

static int js_TA_cmp_int32(const void *a, const void *b, void *opaque) {
    int32_t x = *(const int32_t *)a;
    int32_t y = *(const int32_t *)b;
    return (y < x) - (y > x);
}

static int js_TA_cmp_uint32(const void *a, const void *b, void *opaque) {
    uint32_t x = *(const uint32_t *)a;
    uint32_t y = *(const uint32_t *)b;
    return (y < x) - (y > x);
}

static int js_TA_cmp_int64(const void *a, const void *b, void *opaque) {
    int64_t x = *(const int64_t *)a;
    int64_t y = *(const int64_t *)b;
    return (y < x) - (y > x);
}

static int js_TA_cmp_uint64(const void *a, const void *b, void *opaque) {
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    return (y < x) - (y > x);
}

static int js_TA_cmp_float16(const void *a, const void *b, void *opaque) {
    return js_cmp_doubles(fromfp16(*(const uint16_t *)a),
                          fromfp16(*(const uint16_t *)b));
}

static int js_TA_cmp_float32(const void *a, const void *b, void *opaque) {
    return js_cmp_doubles(*(const float *)a, *(const float *)b);
}

static int js_TA_cmp_float64(const void *a, const void *b, void *opaque) {
    return js_cmp_doubles(*(const double *)a, *(const double *)b);
}

static JSValue js_TA_get_int8(JSContext *ctx, const void *a) {
    return js_int32(*(const int8_t *)a);
}

static JSValue js_TA_get_uint8(JSContext *ctx, const void *a) {
    return js_int32(*(const uint8_t *)a);
}

static JSValue js_TA_get_int16(JSContext *ctx, const void *a) {
    return js_int32(*(const int16_t *)a);
}

static JSValue js_TA_get_uint16(JSContext *ctx, const void *a) {
    return js_int32(*(const uint16_t *)a);
}

static JSValue js_TA_get_int32(JSContext *ctx, const void *a) {
    return js_int32(*(const int32_t *)a);
}

static JSValue js_TA_get_uint32(JSContext *ctx, const void *a) {
    return js_uint32(*(const uint32_t *)a);
}

static JSValue js_TA_get_int64(JSContext *ctx, const void *a) {
    return JS_NewBigInt64(ctx, *(int64_t *)a);
}

static JSValue js_TA_get_uint64(JSContext *ctx, const void *a) {
    return JS_NewBigUint64(ctx, *(uint64_t *)a);
}

static JSValue js_TA_get_float16(JSContext *ctx, const void *a) {
    return js_float64(fromfp16(*(const uint16_t *)a));
}

static JSValue js_TA_get_float32(JSContext *ctx, const void *a) {
    return js_float64(*(const float *)a);
}

static JSValue js_TA_get_float64(JSContext *ctx, const void *a) {
    return js_float64(*(const double *)a);
}

struct TA_sort_context {
    JSContext *ctx;
    int exception;
    JSValueConst arr;
    JSValueConst cmp;
    JSValue (*getfun)(JSContext *ctx, const void *a);
    int elt_size;
};

static int js_TA_cmp_generic(const void *a, const void *b, void *opaque) {
    struct TA_sort_context *psc = opaque;
    JSContext *ctx = psc->ctx;
    uint32_t a_idx, b_idx;
    JSValue argv[2];
    JSValue res;
    JSObject *p;
    int cmp;

    p = JS_VALUE_GET_OBJ(psc->arr);
    if (typed_array_is_oob(p))
        return 0;

    cmp = 0;
    if (!psc->exception) {
        a_idx = *(uint32_t *)a;
        b_idx = *(uint32_t *)b;
        if (a_idx >= p->u.array.count || b_idx >= p->u.array.count)
            return 0;
        argv[0] = psc->getfun(ctx, (char *)p->u.array.u.ptr +
                              a_idx * (size_t)psc->elt_size);
        argv[1] = psc->getfun(ctx, (char *)p->u.array.u.ptr +
                              b_idx * (size_t)(psc->elt_size));
        res = JS_Call(ctx, psc->cmp, JS_UNDEFINED, 2, vc(argv));
        if (JS_IsException(res)) {
            psc->exception = 1;
            goto done;
        }
        if (JS_VALUE_GET_TAG(res) == JS_TAG_INT) {
            int val = JS_VALUE_GET_INT(res);
            cmp = (val > 0) - (val < 0);
        } else {
            double val;
            if (JS_ToFloat64Free(ctx, &val, res) < 0) {
                psc->exception = 1;
                goto done;
            } else {
                cmp = (val > 0) - (val < 0);
            }
        }
        if (cmp == 0) {
            /* make sort stable: compare array offsets */
            cmp = (a_idx > b_idx) - (a_idx < b_idx);
        }
    done:
        JS_FreeValue(ctx, argv[0]);
        JS_FreeValue(ctx, argv[1]);
    }
    return cmp;
}

static JSValue js_typed_array_sort(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    JSObject *p;
    int len;
    size_t elt_size;
    struct TA_sort_context tsc;
    int (*cmpfun)(const void *a, const void *b, void *opaque);

    p = get_typed_array(ctx, this_val);
    if (!p)
        return JS_EXCEPTION;
    if (typed_array_is_immutable(p))
        return JS_ThrowTypeErrorImmutableArrayBuffer(ctx);
    if (typed_array_is_oob(p))
        return JS_ThrowTypeErrorArrayBufferOOB(ctx);

    tsc.ctx = ctx;
    tsc.exception = 0;
    tsc.arr = this_val;
    tsc.cmp = argv[0];

    if (!JS_IsUndefined(tsc.cmp) && check_function(ctx, tsc.cmp))
        return JS_EXCEPTION;

    len = p->u.array.count;
    if (len > 1) {
        switch (p->class_id) {
        case JS_CLASS_INT8_ARRAY:
            tsc.getfun = js_TA_get_int8;
            cmpfun = js_TA_cmp_int8;
            break;
        case JS_CLASS_UINT8C_ARRAY:
        case JS_CLASS_UINT8_ARRAY:
            tsc.getfun = js_TA_get_uint8;
            cmpfun = js_TA_cmp_uint8;
            break;
        case JS_CLASS_INT16_ARRAY:
            tsc.getfun = js_TA_get_int16;
            cmpfun = js_TA_cmp_int16;
            break;
        case JS_CLASS_UINT16_ARRAY:
            tsc.getfun = js_TA_get_uint16;
            cmpfun = js_TA_cmp_uint16;
            break;
        case JS_CLASS_INT32_ARRAY:
            tsc.getfun = js_TA_get_int32;
            cmpfun = js_TA_cmp_int32;
            break;
        case JS_CLASS_UINT32_ARRAY:
            tsc.getfun = js_TA_get_uint32;
            cmpfun = js_TA_cmp_uint32;
            break;
        case JS_CLASS_BIG_INT64_ARRAY:
            tsc.getfun = js_TA_get_int64;
            cmpfun = js_TA_cmp_int64;
            break;
        case JS_CLASS_BIG_UINT64_ARRAY:
            tsc.getfun = js_TA_get_uint64;
            cmpfun = js_TA_cmp_uint64;
            break;
        case JS_CLASS_FLOAT16_ARRAY:
            tsc.getfun = js_TA_get_float16;
            cmpfun = js_TA_cmp_float16;
            break;
        case JS_CLASS_FLOAT32_ARRAY:
            tsc.getfun = js_TA_get_float32;
            cmpfun = js_TA_cmp_float32;
            break;
        case JS_CLASS_FLOAT64_ARRAY:
            tsc.getfun = js_TA_get_float64;
            cmpfun = js_TA_cmp_float64;
            break;
        default:
            abort();
        }
        elt_size = 1 << typed_array_size_log2(p->class_id);
        if (!JS_IsUndefined(tsc.cmp)) {
            uint32_t *array_idx;
            void *array_tmp;
            size_t i, j;

            /* XXX: a stable sort would use less memory */
            array_idx = js_malloc(ctx, len * sizeof(array_idx[0]));
            if (!array_idx)
                return JS_EXCEPTION;
            for(i = 0; i < len; i++)
                array_idx[i] = i;
            tsc.elt_size = elt_size;
            rqsort(array_idx, len, sizeof(array_idx[0]),
                   js_TA_cmp_generic, &tsc);
            if (tsc.exception)
                goto fail;
            // per spec: typed array can be detached mid-iteration
            if (typed_array_is_oob(p))
                goto done;
            len = min_int(len, p->u.array.count);
            if (len == 0)
                goto done;
            array_tmp = js_malloc(ctx, len * elt_size);
            if (!array_tmp) {
            fail:
                js_free(ctx, array_idx);
                return JS_EXCEPTION;
            }
            memcpy(array_tmp, p->u.array.u.ptr, len * elt_size);
            switch(elt_size) {
            case 1:
                for(i = 0; i < len; i++) {
                    j = array_idx[i];
                    if (j < len)
                        p->u.array.u.uint8_ptr[i] = ((uint8_t *)array_tmp)[j];
                }
                break;
            case 2:
                for(i = 0; i < len; i++) {
                    j = array_idx[i];
                    if (j < len)
                        p->u.array.u.uint16_ptr[i] = ((uint16_t *)array_tmp)[j];
                }
                break;
            case 4:
                for(i = 0; i < len; i++) {
                    j = array_idx[i];
                    if (j < len)
                        p->u.array.u.uint32_ptr[i] = ((uint32_t *)array_tmp)[j];
                }
                break;
            case 8:
                for(i = 0; i < len; i++) {
                    j = array_idx[i];
                    if (j < len)
                        p->u.array.u.uint64_ptr[i] = ((uint64_t *)array_tmp)[j];
                }
                break;
            default:
                abort();
            }
            js_free(ctx, array_tmp);
        done:
            js_free(ctx, array_idx);
        } else {
            rqsort(p->u.array.u.ptr, len, elt_size, cmpfun, &tsc);
            if (tsc.exception)
                return JS_EXCEPTION;
        }
    }
    return js_dup(this_val);
}

static JSValue js_typed_array_toSorted(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    JSValue arr, ret;
    JSObject *p;

    p = get_typed_array(ctx, this_val);
    if (!p)
        return JS_EXCEPTION;
    arr = js_typed_array_constructor_ta(ctx, JS_UNDEFINED, this_val,
                                        p->class_id, p->u.array.count);
    if (JS_IsException(arr))
        return JS_EXCEPTION;
    ret = js_typed_array_sort(ctx, arr, argc, argv);
    JS_FreeValue(ctx, arr);
    return ret;
}

static const JSCFunctionListEntry js_typed_array_base_funcs[] = {
    JS_CFUNC_DEF("from", 1, js_typed_array_from ),
    JS_CFUNC_DEF("of", 0, js_typed_array_of ),
    JS_CGETSET_DEF("[Symbol.species]", js_get_this, NULL ),
};

static const JSCFunctionListEntry js_typed_array_base_proto_funcs[] = {
    JS_CGETSET_DEF("length", js_typed_array_get_length, NULL ),
    JS_CFUNC_DEF("at", 1, js_typed_array_at ),
    JS_CFUNC_DEF("with", 2, js_typed_array_with ),
    JS_CGETSET_DEF("buffer", js_typed_array_get_buffer, NULL ),
    JS_CGETSET_DEF("byteLength", js_typed_array_get_byteLength, NULL ),
    JS_CGETSET_DEF("byteOffset", js_typed_array_get_byteOffset, NULL ),
    JS_CFUNC_DEF("set", 1, js_typed_array_set ),
    JS_CFUNC_MAGIC_DEF("values", 0, js_create_typed_array_iterator, JS_ITERATOR_KIND_VALUE ),
    JS_ALIAS_DEF("[Symbol.iterator]", "values" ),
    JS_CFUNC_MAGIC_DEF("keys", 0, js_create_typed_array_iterator, JS_ITERATOR_KIND_KEY ),
    JS_CFUNC_MAGIC_DEF("entries", 0, js_create_typed_array_iterator, JS_ITERATOR_KIND_KEY_AND_VALUE ),
    JS_CGETSET_DEF("[Symbol.toStringTag]", js_typed_array_get_toStringTag, NULL ),
    JS_CFUNC_DEF("copyWithin", 2, js_typed_array_copyWithin ),
    JS_CFUNC_MAGIC_DEF("every", 1, js_array_every, special_every | special_TA ),
    JS_CFUNC_MAGIC_DEF("some", 1, js_array_every, special_some | special_TA ),
    JS_CFUNC_MAGIC_DEF("forEach", 1, js_array_every, special_forEach | special_TA ),
    JS_CFUNC_MAGIC_DEF("map", 1, js_array_every, special_map | special_TA ),
    JS_CFUNC_MAGIC_DEF("filter", 1, js_array_every, special_filter | special_TA ),
    JS_CFUNC_MAGIC_DEF("reduce", 1, js_array_reduce, special_reduce | special_TA ),
    JS_CFUNC_MAGIC_DEF("reduceRight", 1, js_array_reduce, special_reduceRight | special_TA ),
    JS_CFUNC_DEF("fill", 1, js_typed_array_fill ),
    JS_CFUNC_MAGIC_DEF("find", 1, js_typed_array_find, ArrayFind ),
    JS_CFUNC_MAGIC_DEF("findIndex", 1, js_typed_array_find, ArrayFindIndex ),
    JS_CFUNC_MAGIC_DEF("findLast", 1, js_typed_array_find, ArrayFindLast ),
    JS_CFUNC_MAGIC_DEF("findLastIndex", 1, js_typed_array_find, ArrayFindLastIndex ),
    JS_CFUNC_DEF("reverse", 0, js_typed_array_reverse ),
    JS_CFUNC_DEF("toReversed", 0, js_typed_array_toReversed ),
    JS_CFUNC_DEF("slice", 2, js_typed_array_slice ),
    JS_CFUNC_DEF("subarray", 2, js_typed_array_subarray ),
    JS_CFUNC_DEF("sort", 1, js_typed_array_sort ),
    JS_CFUNC_DEF("toSorted", 1, js_typed_array_toSorted ),
    JS_CFUNC_MAGIC_DEF("join", 1, js_typed_array_join, 0 ),
    JS_CFUNC_MAGIC_DEF("toLocaleString", 0, js_typed_array_join, 1 ),
    JS_CFUNC_MAGIC_DEF("indexOf", 1, js_typed_array_indexOf, special_indexOf ),
    JS_CFUNC_MAGIC_DEF("lastIndexOf", 1, js_typed_array_indexOf, special_lastIndexOf ),
    JS_CFUNC_MAGIC_DEF("includes", 1, js_typed_array_indexOf, special_includes ),
    //JS_ALIAS_BASE_DEF("toString", "toString", 2 /* Array.prototype. */), @@@
};

static const JSCFunctionListEntry js_typed_array_funcs[] = {
    JS_PROP_INT32_DEF("BYTES_PER_ELEMENT", 1, 0),
    JS_PROP_INT32_DEF("BYTES_PER_ELEMENT", 2, 0),
    JS_PROP_INT32_DEF("BYTES_PER_ELEMENT", 4, 0),
    JS_PROP_INT32_DEF("BYTES_PER_ELEMENT", 8, 0),
};

static JSValue js_typed_array_base_constructor(JSContext *ctx,
                                               JSValueConst this_val,
                                               int argc, JSValueConst *argv)
{
    return JS_ThrowTypeError(ctx, "cannot be called");
}

/* 'obj' must be an allocated typed array object */
static int typed_array_init(JSContext *ctx, JSValue obj, JSValue buffer,
                            uint64_t offset, uint64_t len, bool track_rab)
{
    JSTypedArray *ta;
    JSObject *p, *pbuffer;
    JSArrayBuffer *abuf;
    int size_log2;

    p = JS_VALUE_GET_OBJ(obj);
    size_log2 = typed_array_size_log2(p->class_id);
    ta = js_malloc(ctx, sizeof(*ta));
    if (!ta) {
        JS_FreeValue(ctx, buffer);
        return -1;
    }
    pbuffer = JS_VALUE_GET_OBJ(buffer);
    abuf = pbuffer->u.array_buffer;
    ta->obj = p;
    ta->buffer = pbuffer;
    ta->offset = offset;
    ta->length = len << size_log2;
    ta->track_rab = track_rab;
    list_add_tail(&ta->link, &abuf->array_list);
    p->u.typed_array = ta;
    p->u.array.count = len;
    p->u.array.u.ptr = abuf->data + offset;
    return 0;
}


static JSValue js_array_from_iterator(JSContext *ctx, uint32_t *plen,
                                      JSValueConst obj, JSValueConst method)
{
    JSValue arr, iter, next_method = JS_UNDEFINED, val;
    int done;
    uint32_t k;

    *plen = 0;
    arr = JS_NewArray(ctx);
    if (JS_IsException(arr))
        return arr;
    iter = JS_GetIterator2(ctx, obj, method);
    if (JS_IsException(iter))
        goto fail;
    next_method = JS_GetProperty(ctx, iter, JS_ATOM_next);
    if (JS_IsException(next_method))
        goto fail;
    k = 0;
    for(;;) {
        val = JS_IteratorNext(ctx, iter, next_method, 0, NULL, &done);
        if (JS_IsException(val))
            goto fail;
        if (done)
            break;
        if (JS_CreateDataPropertyUint32(ctx, arr, k, val, JS_PROP_THROW) < 0)
            goto fail;
        k++;
    }
    JS_FreeValue(ctx, next_method);
    JS_FreeValue(ctx, iter);
    *plen = k;
    return arr;
 fail:
    JS_FreeValue(ctx, next_method);
    JS_FreeValue(ctx, iter);
    JS_FreeValue(ctx, arr);
    return JS_EXCEPTION;
}

