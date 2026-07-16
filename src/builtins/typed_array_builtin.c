/* Engine domain source: builtins/bigint_typedarray.inc -> typed_array_builtin.
 * Ownership: builtins subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static JSValue js_typed_array_constructor_obj(JSContext *ctx,
                                              JSValueConst new_target,
                                              JSValueConst obj,
                                              int classid)
{
    JSValue iter, ret, arr = JS_UNDEFINED, val, buffer;
    uint32_t i;
    int size_log2;
    int64_t len;

    size_log2 = typed_array_size_log2(classid);
    ret = js_create_from_ctor(ctx, new_target, classid);
    if (JS_IsException(ret))
        return JS_EXCEPTION;

    iter = JS_GetProperty(ctx, obj, JS_ATOM_Symbol_iterator);
    if (JS_IsException(iter))
        goto fail;
    if (!JS_IsUndefined(iter) && !JS_IsNull(iter)) {
        uint32_t len1;
        arr = js_array_from_iterator(ctx, &len1, obj, iter);
        JS_FreeValue(ctx, iter);
        if (JS_IsException(arr))
            goto fail;
        len = len1;
    } else {
        if (js_get_length64(ctx, &len, obj))
            goto fail;
        arr = js_dup(obj);
    }

    buffer = js_array_buffer_constructor1(ctx, JS_UNDEFINED,
                                          len << size_log2,
                                          NULL);
    if (JS_IsException(buffer))
        goto fail;
    if (typed_array_init(ctx, ret, buffer, 0, len, /*track_rab*/false))
        goto fail;

    for(i = 0; i < len; i++) {
        val = JS_GetPropertyUint32(ctx, arr, i);
        if (JS_IsException(val))
            goto fail;
        if (JS_SetPropertyUint32(ctx, ret, i, val) < 0)
            goto fail;
    }
    JS_FreeValue(ctx, arr);
    return ret;
 fail:
    JS_FreeValue(ctx, arr);
    JS_FreeValue(ctx, ret);
    return JS_EXCEPTION;
}

static JSValue js_typed_array_constructor_ta(JSContext *ctx,
                                             JSValueConst new_target,
                                             JSValueConst src_obj,
                                             int classid, uint32_t len)
{
    JSObject *p, *src_buffer;
    JSTypedArray *ta;
    JSValue obj, buffer;
    uint32_t i;
    int size_log2;
    JSArrayBuffer *src_abuf, *abuf;

    obj = js_create_from_ctor(ctx, new_target, classid);
    if (JS_IsException(obj))
        return obj;
    p = JS_VALUE_GET_OBJ(src_obj);
    if (typed_array_is_oob(p)) {
        JS_ThrowTypeErrorArrayBufferOOB(ctx);
        goto fail;
    }
    if (len > p->u.array.count) {
        JS_ThrowRangeError(ctx, "length out of bounds");
        goto fail;
    }
    ta = p->u.typed_array;
    src_buffer = ta->buffer;
    src_abuf = src_buffer->u.array_buffer;
    size_log2 = typed_array_size_log2(classid);
    buffer = js_array_buffer_constructor1(ctx, JS_UNDEFINED,
                                          (uint64_t)len << size_log2,
                                          NULL);
    if (JS_IsException(buffer))
        goto fail;
    /* necessary because it could have been detached */
    if (typed_array_is_oob(p)) {
        JS_FreeValue(ctx, buffer);
        JS_ThrowTypeErrorArrayBufferOOB(ctx);
        goto fail;
    }
    abuf = JS_GetOpaque(buffer, JS_CLASS_ARRAY_BUFFER);
    if (typed_array_init(ctx, obj, buffer, 0, len, /*track_rab*/false))
        goto fail;
    if (p->class_id == classid) {
        /* same type: copy the content */
        memcpy(abuf->data, src_abuf->data + ta->offset, abuf->byte_length);
    } else {
        for(i = 0; i < len; i++) {
            JSValue val;
            val = JS_GetPropertyUint32(ctx, src_obj, i);
            if (JS_IsException(val))
                goto fail;
            if (JS_SetPropertyUint32(ctx, obj, i, val) < 0)
                goto fail;
        }
    }
    return obj;
 fail:
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue js_typed_array_constructor(JSContext *ctx,
                                          JSValueConst new_target,
                                          int argc, JSValueConst *argv,
                                          int classid)
{
    bool track_rab = false;
    JSValue buffer, obj;
    JSArrayBuffer *abuf;
    int size_log2;
    uint64_t len, offset;

    size_log2 = typed_array_size_log2(classid);
    if (JS_VALUE_GET_TAG(argv[0]) != JS_TAG_OBJECT) {
        if (JS_ToIndex(ctx, &len, argv[0]))
            return JS_EXCEPTION;
        buffer = js_array_buffer_constructor1(ctx, JS_UNDEFINED,
                                              len << size_log2,
                                              NULL);
        if (JS_IsException(buffer))
            return JS_EXCEPTION;
        offset = 0;
    } else {
        JSObject *p = JS_VALUE_GET_OBJ(argv[0]);
        if (p->class_id == JS_CLASS_ARRAY_BUFFER ||
            p->class_id == JS_CLASS_SHARED_ARRAY_BUFFER) {
            abuf = p->u.array_buffer;
            if (JS_ToIndex(ctx, &offset, argv[1]))
                return JS_EXCEPTION;
            if (abuf->detached)
                return JS_ThrowTypeErrorDetachedArrayBuffer(ctx);
            if ((offset & ((1 << size_log2) - 1)) != 0 ||
                offset > abuf->byte_length)
                return JS_ThrowRangeError(ctx, "invalid offset");
            if (JS_IsUndefined(argv[2])) {
                track_rab = array_buffer_is_resizable(abuf);
                if (!track_rab)
                    if ((abuf->byte_length & ((1 << size_log2) - 1)) != 0)
                        goto invalid_length;
                len = (abuf->byte_length - offset) >> size_log2;
            } else {
                if (JS_ToIndex(ctx, &len, argv[2]))
                    return JS_EXCEPTION;
                if (abuf->detached)
                    return JS_ThrowTypeErrorDetachedArrayBuffer(ctx);
                if ((offset + (len << size_log2)) > abuf->byte_length) {
                invalid_length:
                    return JS_ThrowRangeError(ctx, "invalid length");
                }
            }
            buffer = js_dup(argv[0]);
        } else {
            if (is_typed_array(p->class_id)) {
                return js_typed_array_constructor_ta(ctx, new_target, argv[0],
                                                     classid, p->u.array.count);
            } else {
                return js_typed_array_constructor_obj(ctx, new_target, argv[0], classid);
            }
        }
    }

    obj = js_create_from_ctor(ctx, new_target, classid);
    if (JS_IsException(obj)) {
        JS_FreeValue(ctx, buffer);
        return JS_EXCEPTION;
    }
    // Re-validate buffer after js_create_from_ctor which may have run JS code
    // that resized or detached the ArrayBuffer
    abuf = JS_VALUE_GET_OBJ(buffer)->u.array_buffer;
    if (abuf->detached) {
        JS_FreeValue(ctx, buffer);
        JS_FreeValue(ctx, obj);
        return JS_ThrowTypeErrorDetachedArrayBuffer(ctx);
    }
    if (offset > abuf->byte_length) {
        JS_FreeValue(ctx, buffer);
        JS_FreeValue(ctx, obj);
        return JS_ThrowRangeError(ctx, "invalid offset");
    }
    if (track_rab) {
        // Recalculate length for RAB-backed view
        len = (abuf->byte_length - offset) >> size_log2;
    } else if ((offset + (len << size_log2)) > abuf->byte_length) {
        JS_FreeValue(ctx, buffer);
        JS_FreeValue(ctx, obj);
        return JS_ThrowRangeError(ctx, "invalid length");
    }
    if (typed_array_init(ctx, obj, buffer, offset, len, track_rab)) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    return obj;
}

static void js_typed_array_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);
    JSTypedArray *ta = p->u.typed_array;
    if (ta) {
        /* during the GC the finalizers are called in an arbitrary
           order so the ArrayBuffer finalizer may have been called */
        if (ta->link.next) {
            list_del(&ta->link);
        }
        JS_FreeValueRT(rt, JS_MKPTR(JS_TAG_OBJECT, ta->buffer));
        js_free_rt(rt, ta);
    }
}

static void js_typed_array_mark(JSRuntime *rt, JSValueConst val,
                                JS_MarkFunc *mark_func)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);
    JSTypedArray *ta = p->u.typed_array;
    if (ta) {
        JS_MarkValue(rt, JS_MKPTR(JS_TAG_OBJECT, ta->buffer), mark_func);
    }
}

static JSValue js_dataview_constructor(JSContext *ctx,
                                       JSValueConst new_target,
                                       int argc, JSValueConst *argv)
{
    bool recompute_len = false;
    bool track_rab = false;
    JSArrayBuffer *abuf;
    uint64_t offset;
    uint32_t len;
    JSValueConst buffer;
    JSValue obj;
    JSTypedArray *ta;
    JSObject *p;

    buffer = argv[0];
    abuf = js_get_array_buffer(ctx, buffer);
    if (!abuf)
        return JS_EXCEPTION;
    offset = 0;
    if (argc > 1) {
        if (JS_ToIndex(ctx, &offset, argv[1]))
            return JS_EXCEPTION;
    }
    if (abuf->detached)
        return JS_ThrowTypeErrorDetachedArrayBuffer(ctx);
    if (offset > abuf->byte_length)
        return JS_ThrowRangeError(ctx, "invalid byteOffset");
    len = abuf->byte_length - offset;
    if (argc > 2 && !JS_IsUndefined(argv[2])) {
        uint64_t l;
        if (JS_ToIndex(ctx, &l, argv[2]))
            return JS_EXCEPTION;
        if (l > len)
            return JS_ThrowRangeError(ctx, "invalid byteLength");
        len = l;
    } else {
        recompute_len = true;
        track_rab = array_buffer_is_resizable(abuf);
    }

    obj = js_create_from_ctor(ctx, new_target, JS_CLASS_DATAVIEW);
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    if (abuf->detached) {
        /* could have been detached in js_create_from_ctor() */
        JS_ThrowTypeErrorDetachedArrayBuffer(ctx);
        goto fail;
    }
    // RAB could have been resized in js_create_from_ctor()
    if (offset > abuf->byte_length) {
        goto out_of_bound;
    } else if (recompute_len) {
        len = abuf->byte_length - offset;
    } else if (offset + len > abuf->byte_length) {
    out_of_bound:
        JS_ThrowRangeError(ctx, "invalid byteOffset or byteLength");
        goto fail;
    }
    ta = js_malloc(ctx, sizeof(*ta));
    if (!ta) {
    fail:
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    p = JS_VALUE_GET_OBJ(obj);
    ta->obj = p;
    ta->buffer = JS_VALUE_GET_OBJ(js_dup(buffer));
    ta->offset = offset;
    ta->length = len;
    ta->track_rab = track_rab;
    list_add_tail(&ta->link, &abuf->array_list);
    p->u.typed_array = ta;
    return obj;
}

// is the DataView out of bounds relative to its parent arraybuffer?
static bool dataview_is_oob(JSObject *p)
{
    JSArrayBuffer *abuf;
    JSTypedArray *ta;

    assert(p->class_id == JS_CLASS_DATAVIEW);
    ta = p->u.typed_array;
    abuf = ta->buffer->u.array_buffer;
    if (abuf->detached)
        return true;
    if (ta->offset > abuf->byte_length)
        return true;
    if (ta->track_rab)
        return false;
    return (int64_t)ta->offset + ta->length > abuf->byte_length;
}

static JSObject *get_dataview(JSContext *ctx, JSValueConst this_val)
{
    JSObject *p;
    if (JS_VALUE_GET_TAG(this_val) != JS_TAG_OBJECT)
        goto fail;
    p = JS_VALUE_GET_OBJ(this_val);
    if (p->class_id != JS_CLASS_DATAVIEW) {
    fail:
        JS_ThrowTypeError(ctx, "not a DataView");
        return NULL;
    }
    return p;
}

static JSValue js_dataview_get_buffer(JSContext *ctx, JSValueConst this_val)
{
    JSObject *p;
    JSTypedArray *ta;
    p = get_dataview(ctx, this_val);
    if (!p)
        return JS_EXCEPTION;
    ta = p->u.typed_array;
    return js_dup(JS_MKPTR(JS_TAG_OBJECT, ta->buffer));
}

static JSValue js_dataview_get_byteLength(JSContext *ctx, JSValueConst this_val)
{
    JSArrayBuffer *abuf;
    JSTypedArray *ta;
    JSObject *p;

    p = get_dataview(ctx, this_val);
    if (!p)
        return JS_EXCEPTION;
    if (dataview_is_oob(p))
        return JS_ThrowTypeErrorArrayBufferOOB(ctx);
    ta = p->u.typed_array;
    if (ta->track_rab) {
        abuf = ta->buffer->u.array_buffer;
        return js_uint32(abuf->byte_length - ta->offset);
    }
    return js_uint32(ta->length);
}

static JSValue js_dataview_get_byteOffset(JSContext *ctx, JSValueConst this_val)
{
    JSTypedArray *ta;
    JSObject *p;

    p = get_dataview(ctx, this_val);
    if (!p)
        return JS_EXCEPTION;
    if (dataview_is_oob(p))
        return JS_ThrowTypeErrorArrayBufferOOB(ctx);
    ta = p->u.typed_array;
    return js_uint32(ta->offset);
}

static JSValue js_dataview_getValue(JSContext *ctx,
                                    JSValueConst this_obj,
                                    int argc, JSValueConst *argv, int class_id)
{
    JSTypedArray *ta;
    JSArrayBuffer *abuf;
    bool littleEndian, is_swap;
    int size;
    uint8_t *ptr;
    uint32_t v;
    uint64_t pos;

    ta = JS_GetOpaque2(ctx, this_obj, JS_CLASS_DATAVIEW);
    if (!ta)
        return JS_EXCEPTION;
    size = 1 << typed_array_size_log2(class_id);
    if (JS_ToIndex(ctx, &pos, argv[0]))
        return JS_EXCEPTION;
    littleEndian = argc > 1 && JS_ToBool(ctx, argv[1]);
    is_swap = littleEndian ^ !is_be();
    abuf = ta->buffer->u.array_buffer;
    if (abuf->detached)
        return JS_ThrowTypeErrorDetachedArrayBuffer(ctx);
    // order matters: this check should come before the next one
    if ((pos + size) > ta->length)
        return JS_ThrowRangeError(ctx, "out of bound");
    // test262 expects a TypeError for this and V8, in its infinite wisdom,
    // throws a "detached array buffer" exception, but IMO that doesn't make
    // sense because the buffer is not in fact detached, it's still there
    if ((int64_t)ta->offset + ta->length > abuf->byte_length)
        return JS_ThrowTypeError(ctx, "out of bound");
    ptr = abuf->data + ta->offset + pos;

    switch(class_id) {
    case JS_CLASS_INT8_ARRAY:
        return js_int32(*(int8_t *)ptr);
    case JS_CLASS_UINT8_ARRAY:
        return js_int32(*(uint8_t *)ptr);
    case JS_CLASS_INT16_ARRAY:
        v = get_u16(ptr);
        if (is_swap)
            v = bswap16(v);
        return js_int32((int16_t)v);
    case JS_CLASS_UINT16_ARRAY:
        v = get_u16(ptr);
        if (is_swap)
            v = bswap16(v);
        return js_int32(v);
    case JS_CLASS_INT32_ARRAY:
        v = get_u32(ptr);
        if (is_swap)
            v = bswap32(v);
        return js_int32(v);
    case JS_CLASS_UINT32_ARRAY:
        v = get_u32(ptr);
        if (is_swap)
            v = bswap32(v);
        return js_uint32(v);
    case JS_CLASS_BIG_INT64_ARRAY:
        {
            uint64_t v;
            v = get_u64(ptr);
            if (is_swap)
                v = bswap64(v);
            return JS_NewBigInt64(ctx, v);
        }
        break;
    case JS_CLASS_BIG_UINT64_ARRAY:
        {
            uint64_t v;
            v = get_u64(ptr);
            if (is_swap)
                v = bswap64(v);
            return JS_NewBigUint64(ctx, v);
        }
        break;
    case JS_CLASS_FLOAT16_ARRAY:
        {
            uint16_t v;
            v = get_u16(ptr);
            if (is_swap)
                v = bswap16(v);
            return js_float64(fromfp16(v));
        }
    case JS_CLASS_FLOAT32_ARRAY:
        {
            union {
                float f;
                uint32_t i;
            } u;
            v = get_u32(ptr);
            if (is_swap)
                v = bswap32(v);
            u.i = v;
            return js_float64(u.f);
        }
    case JS_CLASS_FLOAT64_ARRAY:
        {
            union {
                double f;
                uint64_t i;
            } u;
            u.i = get_u64(ptr);
            if (is_swap)
                u.i = bswap64(u.i);
            return js_float64(u.f);
        }
    default:
        abort();
    }
    return JS_EXCEPTION; // pacify compiler
}

static JSValue js_dataview_setValue(JSContext *ctx,
                                    JSValueConst this_obj,
                                    int argc, JSValueConst *argv, int class_id)
{
    JSTypedArray *ta;
    JSArrayBuffer *abuf;
    bool littleEndian, is_swap;
    int size;
    uint8_t *ptr;
    uint64_t v64;
    uint32_t v;
    uint64_t pos;
    JSValueConst val;

    ta = JS_GetOpaque2(ctx, this_obj, JS_CLASS_DATAVIEW);
    if (!ta)
        return JS_EXCEPTION;
    abuf = ta->buffer->u.array_buffer;
    if (abuf->immutable)
        return JS_ThrowTypeErrorImmutableArrayBuffer(ctx);
    size = 1 << typed_array_size_log2(class_id);
    if (JS_ToIndex(ctx, &pos, argv[0]))
        return JS_EXCEPTION;
    val = argv[1];
    v = 0; /* avoid warning */
    v64 = 0; /* avoid warning */
    if (class_id <= JS_CLASS_UINT32_ARRAY) {
        if (JS_ToUint32(ctx, &v, val))
            return JS_EXCEPTION;
    } else if (class_id <= JS_CLASS_BIG_UINT64_ARRAY) {
        if (JS_ToBigInt64(ctx, (int64_t *)&v64, val))
            return JS_EXCEPTION;
    } else {
        double d;
        if (JS_ToFloat64(ctx, &d, val))
            return JS_EXCEPTION;
        if (class_id == JS_CLASS_FLOAT16_ARRAY) {
            v = tofp16(d);
        } else if (class_id == JS_CLASS_FLOAT32_ARRAY) {
            union {
                float f;
                uint32_t i;
            } u;
            u.f = d;
            v = u.i;
        } else {
            JSFloat64Union u;
            u.d = d;
            v64 = u.u64;
        }
    }
    littleEndian = argc > 2 && JS_ToBool(ctx, argv[2]);
    is_swap = littleEndian ^ !is_be();
    if (abuf->detached)
        return JS_ThrowTypeErrorDetachedArrayBuffer(ctx);
    // order matters: this check should come before the next one
    if ((pos + size) > ta->length)
        return JS_ThrowRangeError(ctx, "out of bound");
    // test262 expects a TypeError for this and V8, in its infinite wisdom,
    // throws a "detached array buffer" exception, but IMO that doesn't make
    // sense because the buffer is not in fact detached, it's still there
    if ((int64_t)ta->offset + ta->length > abuf->byte_length)
        return JS_ThrowTypeError(ctx, "out of bound");
    ptr = abuf->data + ta->offset + pos;

    switch(class_id) {
    case JS_CLASS_INT8_ARRAY:
    case JS_CLASS_UINT8_ARRAY:
        *ptr = v;
        break;
    case JS_CLASS_INT16_ARRAY:
    case JS_CLASS_UINT16_ARRAY:
    case JS_CLASS_FLOAT16_ARRAY:
        if (is_swap)
            v = bswap16(v);
        put_u16(ptr, v);
        break;
    case JS_CLASS_INT32_ARRAY:
    case JS_CLASS_UINT32_ARRAY:
    case JS_CLASS_FLOAT32_ARRAY:
        if (is_swap)
            v = bswap32(v);
        put_u32(ptr, v);
        break;
    case JS_CLASS_BIG_INT64_ARRAY:
    case JS_CLASS_BIG_UINT64_ARRAY:
    case JS_CLASS_FLOAT64_ARRAY:
        if (is_swap)
            v64 = bswap64(v64);
        put_u64(ptr, v64);
        break;
    default:
        abort();
    }
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_dataview_proto_funcs[] = {
    JS_CGETSET_DEF("buffer", js_dataview_get_buffer, NULL ),
    JS_CGETSET_DEF("byteLength", js_dataview_get_byteLength, NULL ),
    JS_CGETSET_DEF("byteOffset", js_dataview_get_byteOffset, NULL ),
    JS_CFUNC_MAGIC_DEF("getInt8", 1, js_dataview_getValue, JS_CLASS_INT8_ARRAY ),
    JS_CFUNC_MAGIC_DEF("getUint8", 1, js_dataview_getValue, JS_CLASS_UINT8_ARRAY ),
    JS_CFUNC_MAGIC_DEF("getInt16", 1, js_dataview_getValue, JS_CLASS_INT16_ARRAY ),
    JS_CFUNC_MAGIC_DEF("getUint16", 1, js_dataview_getValue, JS_CLASS_UINT16_ARRAY ),
    JS_CFUNC_MAGIC_DEF("getInt32", 1, js_dataview_getValue, JS_CLASS_INT32_ARRAY ),
    JS_CFUNC_MAGIC_DEF("getUint32", 1, js_dataview_getValue, JS_CLASS_UINT32_ARRAY ),
    JS_CFUNC_MAGIC_DEF("getBigInt64", 1, js_dataview_getValue, JS_CLASS_BIG_INT64_ARRAY ),
    JS_CFUNC_MAGIC_DEF("getBigUint64", 1, js_dataview_getValue, JS_CLASS_BIG_UINT64_ARRAY ),
    JS_CFUNC_MAGIC_DEF("getFloat16", 1, js_dataview_getValue, JS_CLASS_FLOAT16_ARRAY ),
    JS_CFUNC_MAGIC_DEF("getFloat32", 1, js_dataview_getValue, JS_CLASS_FLOAT32_ARRAY ),
    JS_CFUNC_MAGIC_DEF("getFloat64", 1, js_dataview_getValue, JS_CLASS_FLOAT64_ARRAY ),
    JS_CFUNC_MAGIC_DEF("setInt8", 2, js_dataview_setValue, JS_CLASS_INT8_ARRAY ),
    JS_CFUNC_MAGIC_DEF("setUint8", 2, js_dataview_setValue, JS_CLASS_UINT8_ARRAY ),
    JS_CFUNC_MAGIC_DEF("setInt16", 2, js_dataview_setValue, JS_CLASS_INT16_ARRAY ),
    JS_CFUNC_MAGIC_DEF("setUint16", 2, js_dataview_setValue, JS_CLASS_UINT16_ARRAY ),
    JS_CFUNC_MAGIC_DEF("setInt32", 2, js_dataview_setValue, JS_CLASS_INT32_ARRAY ),
    JS_CFUNC_MAGIC_DEF("setUint32", 2, js_dataview_setValue, JS_CLASS_UINT32_ARRAY ),
    JS_CFUNC_MAGIC_DEF("setBigInt64", 2, js_dataview_setValue, JS_CLASS_BIG_INT64_ARRAY ),
    JS_CFUNC_MAGIC_DEF("setBigUint64", 2, js_dataview_setValue, JS_CLASS_BIG_UINT64_ARRAY ),
    JS_CFUNC_MAGIC_DEF("setFloat16", 2, js_dataview_setValue, JS_CLASS_FLOAT16_ARRAY ),
    JS_CFUNC_MAGIC_DEF("setFloat32", 2, js_dataview_setValue, JS_CLASS_FLOAT32_ARRAY ),
    JS_CFUNC_MAGIC_DEF("setFloat64", 2, js_dataview_setValue, JS_CLASS_FLOAT64_ARRAY ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "DataView", JS_PROP_CONFIGURABLE ),
};

static JSValue js_new_uint8array(JSContext *ctx, JSValue buffer)
{
    if (JS_IsException(buffer))
        return JS_EXCEPTION;
    JSValue obj = js_create_from_ctor(ctx, JS_UNDEFINED, JS_CLASS_UINT8_ARRAY);
    if (JS_IsException(obj)) {
        JS_FreeValue(ctx, buffer);
        return JS_EXCEPTION;
    }
    JSArrayBuffer *abuf = js_get_array_buffer(ctx, buffer);
    assert(abuf != NULL);
    if (typed_array_init(ctx, obj, buffer, 0, abuf->byte_length, /*track_rab*/false)) {
        // 'buffer' is freed on error above.
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    return obj;
}

JSValue JS_NewUint8Array(JSContext *ctx, uint8_t *buf, size_t len,
                         JSFreeArrayBufferDataFunc *free_func, void *opaque,
                         bool is_shared)
{
    JSClassID class_id =
        is_shared ? JS_CLASS_SHARED_ARRAY_BUFFER : JS_CLASS_ARRAY_BUFFER;
    JSValue buffer = js_array_buffer_constructor3(ctx, JS_UNDEFINED, len, NULL,
                                                  class_id, buf, free_func,
                                                  opaque, false);
    return js_new_uint8array(ctx, buffer);
}

JSValue JS_NewUint8ArrayCopy(JSContext *ctx, const uint8_t *buf, size_t len)
{
    JSValue buffer = js_array_buffer_constructor3(ctx, JS_UNDEFINED, len, NULL,
                                                  JS_CLASS_ARRAY_BUFFER,
                                                  (uint8_t *)buf,
                                                  js_array_buffer_free, NULL,
                                                  true);
    return js_new_uint8array(ctx, buffer);
}

int JS_GetTypedArrayType(JSValueConst obj)
{
    JSClassID class_id = JS_GetClassID(obj);
    if (is_typed_array(class_id))
        return class_id - JS_CLASS_UINT8C_ARRAY;
    else
        return -1;
}

/* Atomics */
#ifdef CONFIG_ATOMICS

typedef enum AtomicsOpEnum {
    ATOMICS_OP_ADD,
    ATOMICS_OP_AND,
    ATOMICS_OP_OR,
    ATOMICS_OP_SUB,
    ATOMICS_OP_XOR,
    ATOMICS_OP_EXCHANGE,
    ATOMICS_OP_COMPARE_EXCHANGE,
    ATOMICS_OP_LOAD,
} AtomicsOpEnum;

static JSObject *js_atomics_get_buf(JSContext *ctx,
                                    JSValueConst obj, JSValueConst idx_val,
                                    JSArrayBuffer **pabuf,
                                    uint64_t *pindex, int is_waitable)
{
    JSObject *p;
    JSTypedArray *ta;
    JSArrayBuffer *abuf;
    uint64_t idx;
    bool err;

    if (JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT)
        goto fail;
    p = JS_VALUE_GET_OBJ(obj);
    if (is_waitable)
        err = (p->class_id != JS_CLASS_INT32_ARRAY &&
               p->class_id != JS_CLASS_BIG_INT64_ARRAY);
    else
        err = !(p->class_id >= JS_CLASS_INT8_ARRAY &&
                p->class_id <= JS_CLASS_BIG_UINT64_ARRAY);
    if (err) {
    fail:
        JS_ThrowTypeError(ctx, "integer TypedArray expected");
        return NULL;
    }
    ta = p->u.typed_array;
    abuf = ta->buffer->u.array_buffer;
    if (!abuf->shared) {
        if (is_waitable == 2) {
            JS_ThrowTypeError(ctx, "not a SharedArrayBuffer TypedArray");
            return NULL;
        }
        if (abuf->detached) {
            JS_ThrowTypeErrorDetachedArrayBuffer(ctx);
            return NULL;
        }
    }
    if (JS_ToIndex(ctx, &idx, idx_val)) {
        return NULL;
    }
    /* if the array buffer is detached, p->u.array.count = 0 */
    if (idx >= p->u.array.count) {
        JS_ThrowRangeError(ctx, "out-of-bound access");
        return NULL;
    }
    if (pabuf)
        *pabuf = abuf;
    *pindex = idx;
    return p;
}

static JSValue js_atomics_op(JSContext *ctx,
                             JSValueConst this_obj,
                             int argc, JSValueConst *argv, int op)
{
    int size_log2;
    uint64_t v, a, rep_val, idx;
    void *ptr;
    JSObject *p;
    JSValue ret;

    p = js_atomics_get_buf(ctx, argv[0], argv[1], NULL, &idx, 0);
    if (!p)
        return JS_EXCEPTION;
    size_log2 = typed_array_size_log2(p->class_id);
    rep_val = 0;
    if (op == ATOMICS_OP_LOAD) {
        v = 0;
    } else {
        if (size_log2 == 3) {
            int64_t v64;
            if (JS_ToBigInt64(ctx, &v64, argv[2]))
                return JS_EXCEPTION;
            v = v64;
            if (op == ATOMICS_OP_COMPARE_EXCHANGE) {
                if (JS_ToBigInt64(ctx, &v64, argv[3]))
                    return JS_EXCEPTION;
                rep_val = v64;
            }
        } else {
			uint32_t v32;
			if (JS_ToUint32(ctx, &v32, argv[2]))
				return JS_EXCEPTION;
			v = v32;
			if (op == ATOMICS_OP_COMPARE_EXCHANGE) {
				if (JS_ToUint32(ctx, &v32, argv[3]))
					return JS_EXCEPTION;
				rep_val = v32;
			}
        }
   }

    /* check if an evil .valueOf has resized or detached the array */
    if (idx >= p->u.array.count)
        return JS_ThrowRangeError(ctx, "out-of-bound access");

    ptr = p->u.array.u.uint8_ptr + ((uintptr_t)idx << size_log2);

   switch(op | (size_log2 << 3)) {

#define OP(op_name, func_name)                          \
    case ATOMICS_OP_ ## op_name | (0 << 3):             \
       a = func_name((_Atomic uint8_t *)ptr, v);        \
       break;                                           \
    case ATOMICS_OP_ ## op_name | (1 << 3):             \
        a = func_name((_Atomic uint16_t *)ptr, v);      \
        break;                                          \
    case ATOMICS_OP_ ## op_name | (2 << 3):             \
        a = func_name((_Atomic uint32_t *)ptr, v);      \
        break;                                          \
    case ATOMICS_OP_ ## op_name | (3 << 3):             \
        a = func_name((_Atomic uint64_t *)ptr, v);      \
        break;
        OP(ADD, atomic_fetch_add)
        OP(AND, atomic_fetch_and)
        OP(OR, atomic_fetch_or)
        OP(SUB, atomic_fetch_sub)
        OP(XOR, atomic_fetch_xor)
        OP(EXCHANGE, atomic_exchange)
#undef OP

    case ATOMICS_OP_LOAD | (0 << 3):
        a = atomic_load((_Atomic uint8_t *)ptr);
        break;
    case ATOMICS_OP_LOAD | (1 << 3):
        a = atomic_load((_Atomic uint16_t *)ptr);
        break;
    case ATOMICS_OP_LOAD | (2 << 3):
        a = atomic_load((_Atomic uint32_t *)ptr);
        break;
    case ATOMICS_OP_LOAD | (3 << 3):
        a = atomic_load((_Atomic uint64_t *)ptr);
        break;
    case ATOMICS_OP_COMPARE_EXCHANGE | (0 << 3):
        {
            uint8_t v1 = v;
            atomic_compare_exchange_strong((_Atomic uint8_t *)ptr, &v1, rep_val);
            a = v1;
        }
        break;
    case ATOMICS_OP_COMPARE_EXCHANGE | (1 << 3):
        {
            uint16_t v1 = v;
            atomic_compare_exchange_strong((_Atomic uint16_t *)ptr, &v1, rep_val);
            a = v1;
        }
        break;
    case ATOMICS_OP_COMPARE_EXCHANGE | (2 << 3):
        {
            uint32_t v1 = v;
            atomic_compare_exchange_strong((_Atomic uint32_t *)ptr, &v1, rep_val);
            a = v1;
        }
        break;
    case ATOMICS_OP_COMPARE_EXCHANGE | (3 << 3):
        {
            uint64_t v1 = v;
            atomic_compare_exchange_strong((_Atomic uint64_t *)ptr, &v1, rep_val);
            a = v1;
        }
        break;
    default:
        abort();
    }

    switch(p->class_id) {
    case JS_CLASS_INT8_ARRAY:
        a = (int8_t)a;
        goto done;
    case JS_CLASS_UINT8_ARRAY:
        a = (uint8_t)a;
        goto done;
    case JS_CLASS_INT16_ARRAY:
        a = (int16_t)a;
        goto done;
    case JS_CLASS_UINT16_ARRAY:
        a = (uint16_t)a;
        goto done;
    case JS_CLASS_INT32_ARRAY:
    done:
        ret = js_int32(a);
        break;
    case JS_CLASS_UINT32_ARRAY:
        ret = js_uint32(a);
        break;
    case JS_CLASS_BIG_INT64_ARRAY:
        ret = JS_NewBigInt64(ctx, a);
        break;
    case JS_CLASS_BIG_UINT64_ARRAY:
        ret = JS_NewBigUint64(ctx, a);
        break;
    default:
        abort();
    }
    return ret;
}

static JSValue js_atomics_store(JSContext *ctx,
                                JSValueConst this_obj,
                                int argc, JSValueConst *argv)
{
    int size_log2;
    uint64_t idx;
    void *ptr;
    JSObject *p;
    JSValue ret;

    p = js_atomics_get_buf(ctx, argv[0], argv[1], NULL, &idx, 0);
    if (!p)
        return JS_EXCEPTION;
    size_log2 = typed_array_size_log2(p->class_id);
    if (size_log2 == 3) {
        int64_t v64;
        ret = JS_ToBigIntFree(ctx, js_dup(argv[2]));
        if (JS_IsException(ret))
            return ret;
        if (JS_ToBigInt64(ctx, &v64, ret)) {
            JS_FreeValue(ctx, ret);
            return JS_EXCEPTION;
        }
        /* check if an evil .valueOf has resized or detached the array */
        if (idx >= p->u.array.count) {
            return JS_ThrowRangeError(ctx, "out-of-bound access");
        }
        ptr = p->u.array.u.uint8_ptr + ((uintptr_t)idx << size_log2);
        atomic_store((_Atomic uint64_t *)ptr, v64);
    } else {
        uint32_t v;
        /* XXX: spec, would be simpler to return the written value */
        ret = JS_ToIntegerFree(ctx, js_dup(argv[2]));
        if (JS_IsException(ret))
            return ret;
        if (JS_ToUint32(ctx, &v, ret)) {
            JS_FreeValue(ctx, ret);
            return JS_EXCEPTION;
        }
        /* check if an evil .valueOf has resized or detached the array */
        if (idx >= p->u.array.count) {
            return JS_ThrowRangeError(ctx, "out-of-bound access");
        }
        ptr = p->u.array.u.uint8_ptr + ((uintptr_t)idx << size_log2);
        switch(size_log2) {
        case 0:
            atomic_store((_Atomic uint8_t *)ptr, v);
            break;
        case 1:
            atomic_store((_Atomic uint16_t *)ptr, v);
            break;
        case 2:
            atomic_store((_Atomic uint32_t *)ptr, v);
            break;
        default:
            abort();
        }
    }
    return ret;
}

static JSValue js_atomics_isLockFree(JSContext *ctx,
                                     JSValueConst this_obj,
                                     int argc, JSValueConst *argv)
{
    int v, ret;
    if (JS_ToInt32Sat(ctx, &v, argv[0]))
        return JS_EXCEPTION;
    ret = (v == 1 || v == 2 || v == 4 || v == 8);
    return js_bool(ret);
}

typedef struct JSAtomicsWaiter {
    struct list_head link;
    bool linked;
    js_cond_t cond;
    int32_t *ptr;
} JSAtomicsWaiter;

static js_once_t js_atomics_once = JS_ONCE_INIT;
static js_mutex_t js_atomics_mutex;
static struct list_head js_atomics_waiter_list =
    LIST_HEAD_INIT(js_atomics_waiter_list);

// no-op: Atomics.pause() is not allowed to block or yield to another
// thread, only to hint the CPU that it should back off for a bit;
// the amount of work we do here is a good enough substitute
static JSValue js_atomics_pause(JSContext *ctx, JSValueConst this_obj,
                                int argc, JSValueConst *argv)
{
    double d;

    if (argc > 0) {
        switch (JS_VALUE_GET_NORM_TAG(argv[0])) {
        case JS_TAG_FLOAT64: // accepted if and only if fraction == 0.0
            d = JS_VALUE_GET_FLOAT64(argv[0]);
            if (isfinite(d))
                if (0 == modf(d, &d))
                    break;
            // fallthru
        default:
            return JS_ThrowTypeError(ctx, "not an integral number");
        case JS_TAG_UNDEFINED:
        case JS_TAG_INT:
            break;
        }
    }
    return JS_UNDEFINED;
}

static JSValue js_atomics_wait(JSContext *ctx,
                               JSValueConst this_obj,
                               int argc, JSValueConst *argv)
{
    int64_t v;
    int32_t v32;
    void *ptr;
    int64_t timeout;
    uint64_t idx;
    JSObject *p;
    JSAtomicsWaiter waiter_s, *waiter;
    int ret, size_log2, res;
    double d;

    p = js_atomics_get_buf(ctx, argv[0], argv[1], NULL, &idx, 2);
    if (!p)
        return JS_EXCEPTION;
    size_log2 = typed_array_size_log2(p->class_id);
    if (size_log2 == 3) {
        if (JS_ToBigInt64(ctx, &v, argv[2]))
            return JS_EXCEPTION;
    } else {
        if (JS_ToInt32(ctx, &v32, argv[2]))
            return JS_EXCEPTION;
        v = v32;
    }
    if (JS_ToFloat64(ctx, &d, argv[3]))
        return JS_EXCEPTION;
    if (isnan(d) || d >= 0x1p63)
        timeout = INT64_MAX;
    else if (d < 0)
        timeout = 0;
    else
        timeout = (int64_t)d;
    if (!ctx->rt->can_block)
        return JS_ThrowTypeError(ctx, "cannot block in this thread");

    /* check if an evil .valueOf has resized or detached the array */
    if (idx >= p->u.array.count)
        return JS_ThrowRangeError(ctx, "out-of-bound access");

    ptr = p->u.array.u.uint8_ptr + ((uintptr_t)idx << size_log2);

    /* XXX: inefficient if large number of waiters, should hash on
       'ptr' value */
    js_mutex_lock(&js_atomics_mutex);
    if (size_log2 == 3) {
        res = *(int64_t *)ptr != v;
    } else {
        res = *(int32_t *)ptr != v;
    }
    if (res) {
        js_mutex_unlock(&js_atomics_mutex);
        return JS_AtomToString(ctx, JS_ATOM_not_equal);
    }

    waiter = &waiter_s;
    waiter->ptr = ptr;
    js_cond_init(&waiter->cond);
    waiter->linked = true;
    list_add_tail(&waiter->link, &js_atomics_waiter_list);

    if (timeout == INT64_MAX) {
        js_cond_wait(&waiter->cond, &js_atomics_mutex);
        ret = 0;
    } else {
        ret = js_cond_timedwait(&waiter->cond, &js_atomics_mutex, timeout * 1e6 /* to ns */);
    }
    if (waiter->linked)
        list_del(&waiter->link);
    js_mutex_unlock(&js_atomics_mutex);
    js_cond_destroy(&waiter->cond);
    if (ret == -1) {
        return JS_AtomToString(ctx, JS_ATOM_timed_out);
    } else {
        return JS_AtomToString(ctx, JS_ATOM_ok);
    }
}

static JSValue js_atomics_notify(JSContext *ctx,
                                 JSValueConst this_obj,
                                 int argc, JSValueConst *argv)
{
    struct list_head *el, *el1, waiter_list;
    int size_log2;
    int32_t count, n;
    void *ptr;
    uint64_t idx;
    JSObject *p;
    JSArrayBuffer *abuf;
    JSAtomicsWaiter *waiter;

    p = js_atomics_get_buf(ctx, argv[0], argv[1], &abuf, &idx, 1);
    if (!p)
        return JS_EXCEPTION;
    size_log2 = typed_array_size_log2(p->class_id);

    if (JS_IsUndefined(argv[2])) {
        count = INT32_MAX;
    } else {
        if (JS_ToInt32Clamp(ctx, &count, argv[2], 0, INT32_MAX, 0))
            return JS_EXCEPTION;
    }

    /* check if an evil .valueOf has resized or detached the array */
    if (idx >= p->u.array.count)
        return JS_ThrowRangeError(ctx, "out-of-bound access");

    n = 0;
    if (abuf->shared && count > 0) {
        ptr = p->u.array.u.uint8_ptr + ((uintptr_t)idx << size_log2);

        js_mutex_lock(&js_atomics_mutex);
        init_list_head(&waiter_list);
        list_for_each_safe(el, el1, &js_atomics_waiter_list) {
            waiter = list_entry(el, JSAtomicsWaiter, link);
            if (waiter->ptr == ptr) {
                list_del(&waiter->link);
                waiter->linked = false;
                list_add_tail(&waiter->link, &waiter_list);
                n++;
                if (n >= count)
                    break;
            }
        }
        list_for_each(el, &waiter_list) {
            waiter = list_entry(el, JSAtomicsWaiter, link);
            js_cond_signal(&waiter->cond);
        }
        js_mutex_unlock(&js_atomics_mutex);
    }
    return js_int32(n);
}

static const JSCFunctionListEntry js_atomics_funcs[] = {
    JS_CFUNC_MAGIC_DEF("add", 3, js_atomics_op, ATOMICS_OP_ADD ),
    JS_CFUNC_MAGIC_DEF("and", 3, js_atomics_op, ATOMICS_OP_AND ),
    JS_CFUNC_MAGIC_DEF("or", 3, js_atomics_op, ATOMICS_OP_OR ),
    JS_CFUNC_MAGIC_DEF("sub", 3, js_atomics_op, ATOMICS_OP_SUB ),
    JS_CFUNC_MAGIC_DEF("xor", 3, js_atomics_op, ATOMICS_OP_XOR ),
    JS_CFUNC_MAGIC_DEF("exchange", 3, js_atomics_op, ATOMICS_OP_EXCHANGE ),
    JS_CFUNC_MAGIC_DEF("compareExchange", 4, js_atomics_op, ATOMICS_OP_COMPARE_EXCHANGE ),
    JS_CFUNC_MAGIC_DEF("load", 2, js_atomics_op, ATOMICS_OP_LOAD ),
    JS_CFUNC_DEF("store", 3, js_atomics_store ),
    JS_CFUNC_DEF("isLockFree", 1, js_atomics_isLockFree ),
    JS_CFUNC_DEF("pause", 0, js_atomics_pause ),
    JS_CFUNC_DEF("wait", 4, js_atomics_wait ),
    JS_CFUNC_DEF("notify", 3, js_atomics_notify ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Atomics", JS_PROP_CONFIGURABLE ),
};

static const JSCFunctionListEntry js_atomics_obj[] = {
    JS_OBJECT_DEF("Atomics", js_atomics_funcs, countof(js_atomics_funcs), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE ),
};

static void js__atomics_init(void) {
    js_mutex_init(&js_atomics_mutex);
}

/* TODO(saghul) make this public and not dependent on typed arrays? */
static int JS_AddIntrinsicAtomics(JSContext *ctx)
{
    js_once(&js_atomics_once, js__atomics_init);

    /* add Atomics as autoinit object */
    return JS_SetPropertyFunctionList(ctx, ctx->global_obj, js_atomics_obj, countof(js_atomics_obj));
}

#endif /* CONFIG_ATOMICS */

static int js_uint8array_funcs_init(JSContext *ctx);

int JS_AddIntrinsicTypedArrays(JSContext *ctx)
{
    JSValue typed_array_base_func, typed_array_base_proto, obj;
    int i, ret;

    obj = JS_NewCConstructor(ctx, JS_CLASS_ARRAY_BUFFER, "ArrayBuffer",
                             js_array_buffer_constructor, 1, JS_CFUNC_constructor, 0,
                             JS_UNDEFINED,
                             js_array_buffer_funcs, countof(js_array_buffer_funcs),
                             js_array_buffer_proto_funcs, countof(js_array_buffer_proto_funcs),
                             0);
    if (JS_IsException(obj))
        return -1;
    JS_FreeValue(ctx, obj);

    obj = JS_NewCConstructor(ctx, JS_CLASS_SHARED_ARRAY_BUFFER, "SharedArrayBuffer",
                             js_shared_array_buffer_constructor, 1, JS_CFUNC_constructor, 0,
                             JS_UNDEFINED,
                             js_shared_array_buffer_funcs, countof(js_shared_array_buffer_funcs),
                             js_shared_array_buffer_proto_funcs, countof(js_shared_array_buffer_proto_funcs),
                             0);
    if (JS_IsException(obj))
        return -1;
    JS_FreeValue(ctx, obj);

    typed_array_base_func =
        JS_NewCConstructor(ctx, -1, "TypedArray",
                           js_typed_array_base_constructor, 0, JS_CFUNC_constructor_or_func, 0,
                           JS_UNDEFINED,
                           js_typed_array_base_funcs, countof(js_typed_array_base_funcs),
                           js_typed_array_base_proto_funcs, countof(js_typed_array_base_proto_funcs),
                           JS_NEW_CTOR_NO_GLOBAL);
    if (JS_IsException(typed_array_base_func))
        return -1;

    /* TypedArray.prototype.toString must be the same object as Array.prototype.toString */
    obj = JS_GetProperty(ctx, ctx->class_proto[JS_CLASS_ARRAY], JS_ATOM_toString);
    if (JS_IsException(obj))
        goto fail;
    /* XXX: should use alias method in JSCFunctionListEntry */ //@@@
    typed_array_base_proto = JS_GetProperty(ctx, typed_array_base_func, JS_ATOM_prototype);
    if (JS_IsException(typed_array_base_proto))
        goto fail;
    ret = JS_DefinePropertyValue(ctx, typed_array_base_proto, JS_ATOM_toString, obj,
                                 JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_FreeValue(ctx, typed_array_base_proto);
    if (ret < 0)
        goto fail;

    /* Used to squelch a -Wcast-function-type warning. */
    JSCFunctionType ft = { .generic_magic = js_typed_array_constructor };
    for(i = JS_CLASS_UINT8C_ARRAY; i < JS_CLASS_UINT8C_ARRAY + JS_TYPED_ARRAY_COUNT; i++) {
        char buf[ATOM_GET_STR_BUF_SIZE];
        const char *name;
        const JSCFunctionListEntry *bpe;

        name = JS_AtomGetStr(ctx, buf, sizeof(buf),
                             JS_ATOM_Uint8ClampedArray + i - JS_CLASS_UINT8C_ARRAY);
        bpe = js_typed_array_funcs + typed_array_size_log2(i);
        obj = JS_NewCConstructor(ctx, i, name,
                                 ft.generic, 3, JS_CFUNC_constructor_magic, i,
                                 typed_array_base_func,
                                 bpe, 1,
                                 bpe, 1,
                                 0);
        if (JS_IsException(obj)) {
        fail:
            JS_FreeValue(ctx, typed_array_base_func);
            return -1;
        }
        JS_FreeValue(ctx, obj);
    }
    JS_FreeValue(ctx, typed_array_base_func);

    /* Uint8Array base64/hex methods */
    if (js_uint8array_funcs_init(ctx))
        return -1;

    /* DataView */
    obj = JS_NewCConstructor(ctx, JS_CLASS_DATAVIEW, "DataView",
                             js_dataview_constructor, 1, JS_CFUNC_constructor, 0,
                             JS_UNDEFINED,
                             NULL, 0,
                             js_dataview_proto_funcs, countof(js_dataview_proto_funcs),
                             0);
    if (JS_IsException(obj))
        return -1;
    JS_FreeValue(ctx, obj);

    /* Atomics */
#ifdef CONFIG_ATOMICS
    if (JS_AddIntrinsicAtomics(ctx))
        return -1;
#endif
    return 0;
}

/* Performance */

static double js__now_ms(void)
{
    return js__hrtime_ns() / 1e6;
}

static JSValue js_perf_now(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    return js_float64(js__now_ms() - ctx->time_origin);
}

static const JSCFunctionListEntry js_perf_proto_funcs[] = {
    JS_CFUNC_DEF2("now", 0, js_perf_now, JS_PROP_ENUMERABLE),
};

int JS_AddPerformance(JSContext *ctx)
{
    ctx->time_origin = js__now_ms();

    JSValue performance = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, performance, js_perf_proto_funcs, countof(js_perf_proto_funcs));
    JS_DefinePropertyValueStr(ctx, performance, "timeOrigin",
                           js_float64(ctx->time_origin),
                           JS_PROP_ENUMERABLE);
    JS_DefinePropertyValueStr(ctx, ctx->global_obj, "performance",
                           js_dup(performance),
                           JS_PROP_WRITABLE | JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE);
    JS_FreeValue(ctx, performance);
    return 0;
}

/* Equality comparisons and sameness */
int JS_IsEqual(JSContext *ctx, JSValueConst op1, JSValueConst op2)
{
    JSValue sp[2] = { js_dup(op1), js_dup(op2) };
    if (js_eq_slow(ctx, endof(sp), 0))
        return -1;
    return JS_VALUE_GET_BOOL(sp[0]);
}

bool JS_IsStrictEqual(JSContext *ctx, JSValueConst op1, JSValueConst op2)
{
    return js_strict_eq2(ctx, op1, op2, JS_EQ_STRICT);
}

bool JS_IsSameValue(JSContext *ctx, JSValueConst op1, JSValueConst op2)
{
    return js_same_value(ctx, op1, op2);
}

bool JS_IsSameValueZero(JSContext *ctx, JSValueConst op1, JSValueConst op2)
{
    return js_same_value_zero(ctx, op1, op2);
}

/* WeakRef */

typedef struct JSWeakRefData {
    JSValueConst target;
    JSValue obj;
} JSWeakRefData;

static JSWeakRefData js_weakref_sentinel;

static void js_weakref_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSWeakRefData *wrd = JS_GetOpaque(val, JS_CLASS_WEAK_REF);
    if (!wrd || wrd == &js_weakref_sentinel)
        return;

    /* Delete weak ref */
    JSWeakRefRecord **pwr, *wr;

    pwr = get_first_weak_ref(wrd->target);
    for(;;) {
        wr = *pwr;
        assert(wr != NULL);
        if (wr->kind == JS_WEAK_REF_KIND_WEAK_REF && wr->u.weak_ref_data == wrd)
            break;
        pwr = &wr->next_weak_ref;
    }
    *pwr = wr->next_weak_ref;
    js_free_rt(rt, wrd);
    js_free_rt(rt, wr);
}

static JSValue js_weakref_constructor(JSContext *ctx, JSValueConst new_target,
                                      int argc, JSValueConst *argv)
{
    if (JS_IsUndefined(new_target))
        return JS_ThrowTypeError(ctx, "constructor requires 'new'");
    JSValueConst arg = argv[0];
    if (!is_valid_weakref_target(arg))
        return JS_ThrowTypeError(ctx, "invalid target");
    // TODO(saghul): short-circuit if the refcount is 1?
    JSValue obj = js_create_from_ctor(ctx, new_target, JS_CLASS_WEAK_REF);
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    JSWeakRefData *wrd = js_malloc(ctx, sizeof(*wrd));
    if (!wrd) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    JSWeakRefRecord *wr = js_malloc(ctx, sizeof(*wr));
    if (!wr) {
        JS_FreeValue(ctx, obj);
        js_free(ctx, wrd);
        return JS_EXCEPTION;
    }
    wrd->target = arg;
    wrd->obj = obj;
    wr->kind = JS_WEAK_REF_KIND_WEAK_REF;
    wr->u.weak_ref_data = wrd;
    insert_weakref_record(arg, wr);

    JS_SetOpaqueInternal(obj, wrd);
    return obj;
}

static JSValue js_weakref_deref(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    JSWeakRefData *wrd = JS_GetOpaque2(ctx, this_val, JS_CLASS_WEAK_REF);
    if (!wrd)
        return JS_EXCEPTION;
    if (wrd == &js_weakref_sentinel)
        return JS_UNDEFINED;
    return js_dup(wrd->target);
}

static const JSCFunctionListEntry js_weakref_proto_funcs[] = {
    JS_CFUNC_DEF("deref", 0, js_weakref_deref ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WeakRef", JS_PROP_CONFIGURABLE ),
};

static const JSClassShortDef js_weakref_class_def[] = {
    { JS_ATOM_WeakRef, js_weakref_finalizer, NULL }, /* JS_CLASS_WEAK_REF */
};

typedef struct JSFinRecEntry {
    struct list_head link;
    JSValueConst target;
    JSValue held_val;
    JSValue token;
    JSValue cb;
    JSContext *ctx;
} JSFinRecEntry;

typedef struct JSFinalizationRegistryData {
    struct list_head entries;
    JSContext *ctx;
    JSValue cb;
} JSFinalizationRegistryData;

static void delete_finrec_weakref(JSRuntime *rt, JSFinRecEntry *fre)
{
    JSWeakRefRecord **pwr, *wr;

    pwr = get_first_weak_ref(fre->target);
    for(;;) {
        wr = *pwr;
        assert(wr != NULL);
        if (wr->kind == JS_WEAK_REF_KIND_FINALIZATION_REGISTRY_ENTRY && wr->u.fin_rec_entry == fre)
            break;
        pwr = &wr->next_weak_ref;
    }
    *pwr = wr->next_weak_ref;
    js_free_rt(rt, wr);
}

static void js_finrec_free(JSRuntime *rt, JSFinRecEntry *fre)
{
    JS_FreeValueRT(rt, fre->held_val);
    JS_FreeValueRT(rt, fre->token);
    JS_FreeValueRT(rt, fre->cb);
    JS_FreeContext(fre->ctx);
    js_free_rt(rt, fre);
}

static void js_finrec_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSFinalizationRegistryData *frd = JS_GetOpaque(val, JS_CLASS_FINALIZATION_REGISTRY);
    if (frd) {
        struct list_head *el, *el1;
        /* first pass to remove the weak ref entries and avoid having them modified
           by freeing a token / held value. */
        list_for_each_safe(el, el1, &frd->entries) {
            JSFinRecEntry *fre = list_entry(el, JSFinRecEntry, link);
            delete_finrec_weakref(rt, fre);
        }
        /* second pass to actually free all objects. */
        list_for_each_safe(el, el1, &frd->entries) {
            JSFinRecEntry *fre = list_entry(el, JSFinRecEntry, link);
            list_del(&fre->link);
            js_finrec_free(rt, fre);
        }
        JS_FreeValueRT(rt, frd->cb);
        js_free_rt(rt, frd);
    }
}

static void js_finrec_mark(JSRuntime *rt, JSValueConst val,
                           JS_MarkFunc *mark_func)
{
    JSFinalizationRegistryData *frd = JS_GetOpaque(val, JS_CLASS_FINALIZATION_REGISTRY);
    if (frd) {
        JS_MarkValue(rt, frd->cb, mark_func);
        struct list_head *el;
        list_for_each(el, &frd->entries) {
            JSFinRecEntry *fre = list_entry(el, JSFinRecEntry, link);
            JS_MarkValue(rt, fre->held_val, mark_func);
            JS_MarkValue(rt, fre->token, mark_func);
            JS_MarkValue(rt, fre->cb, mark_func);
            mark_func(rt, &fre->ctx->header);
        }
    }
}

static JSValue js_finrec_constructor(JSContext *ctx, JSValueConst new_target,
                                     int argc, JSValueConst *argv)
{
    if (JS_IsUndefined(new_target))
        return JS_ThrowTypeError(ctx, "constructor requires 'new'");
    JSValueConst cb = argv[0];
    if (check_function(ctx, cb))
        return JS_EXCEPTION;
    JSValue obj = js_create_from_ctor(ctx, new_target, JS_CLASS_FINALIZATION_REGISTRY);
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    JSFinalizationRegistryData *frd = js_malloc(ctx, sizeof(*frd));
    if (!frd) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    init_list_head(&frd->entries);
    frd->ctx = ctx;
    frd->cb = js_dup(cb);
    JS_SetOpaqueInternal(obj, frd);
    return obj;
}

static JSValue js_finrec_register(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    JSFinalizationRegistryData *frd = JS_GetOpaque2(ctx, this_val, JS_CLASS_FINALIZATION_REGISTRY);
    if (!frd)
        return JS_EXCEPTION;

    JSValueConst target = argv[0];
    JSValueConst held_val = argv[1];
    // The function length needs to return 2, so the 3rd argument won't be initialized.
    JSValueConst token = argc > 2 ? argv[2] : JS_UNDEFINED;

    if (!is_valid_weakref_target(target))
        return JS_ThrowTypeError(ctx, "invalid target");
    if (js_same_value(ctx, target, this_val))
        return JS_UNDEFINED;
    if (!JS_IsUndefined(held_val) && js_same_value(ctx, target, held_val))
        return JS_ThrowTypeError(ctx, "held value cannot be the target");
    if (!JS_IsUndefined(token) && !is_valid_weakref_target(token))
        return JS_ThrowTypeError(ctx, "invalid unregister token");

    JSFinRecEntry *fre = js_malloc(ctx, sizeof(*fre));
    if (!fre)
        return JS_EXCEPTION;
    JSWeakRefRecord *wr = js_malloc(ctx, sizeof(*wr));
    if (!wr) {
        js_free(ctx, fre);
        return JS_EXCEPTION;
    }
    fre->cb = js_dup(frd->cb);
    fre->ctx = JS_DupContext(frd->ctx);
    fre->target = target;
    fre->held_val = js_dup(held_val);
    fre->token = js_dup(token);
    list_add_tail(&fre->link, &frd->entries);
    wr->kind = JS_WEAK_REF_KIND_FINALIZATION_REGISTRY_ENTRY;
    wr->u.fin_rec_entry = fre;
    insert_weakref_record(target, wr);

    return JS_UNDEFINED;
}

static JSValue js_finrec_unregister(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    JSFinalizationRegistryData *frd = JS_GetOpaque2(ctx, this_val, JS_CLASS_FINALIZATION_REGISTRY);
    if (!frd)
        return JS_EXCEPTION;

    JSValueConst token = argv[0];
    if (!is_valid_weakref_target(token))
        return JS_ThrowTypeError(ctx, "invalid unregister token");

    struct list_head *el, *el1;
    bool removed = false;
    list_for_each_safe(el, el1, &frd->entries) {
        JSFinRecEntry *fre = list_entry(el, JSFinRecEntry, link);
        if (js_same_value(ctx, fre->token, token)) {
            list_del(&fre->link);
            delete_finrec_weakref(ctx->rt, fre);
            js_finrec_free(ctx->rt, fre);
            removed = true;
        }
    }

    return js_bool(removed);
}

static const JSCFunctionListEntry js_finrec_proto_funcs[] = {
    JS_CFUNC_DEF("register", 2, js_finrec_register ),
    JS_CFUNC_DEF("unregister", 1, js_finrec_unregister ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "FinalizationRegistry", JS_PROP_CONFIGURABLE ),
};

static const JSClassShortDef js_finrec_class_def[] = {
    { JS_ATOM_FinalizationRegistry, js_finrec_finalizer, js_finrec_mark }, /* JS_CLASS_FINALIZATION_REGISTRY */
};

static JSValue js_finrec_job(JSContext *ctx, int argc, JSValueConst *argv)
{
    return JS_Call(ctx, argv[0], JS_UNDEFINED, 1, &argv[1]);
}

