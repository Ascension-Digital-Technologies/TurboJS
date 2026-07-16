/* Engine domain source: builtins/collections_promises_date.inc -> collection_builtin.
 * Ownership: builtins subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

/* Set/Map/WeakSet/WeakMap */

#define MAGIC_SET (1 << 0)
#define MAGIC_WEAK (1 << 1)

static JSValue js_map_constructor(JSContext *ctx, JSValueConst new_target,
                                  int argc, JSValueConst *argv, int magic)
{
    JSMapState *s;
    JSValue obj, adder = JS_UNDEFINED, iter = JS_UNDEFINED, next_method = JS_UNDEFINED;
    JSValueConst arr;
    bool is_set, is_weak;

    is_set = magic & MAGIC_SET;
    is_weak = ((magic & MAGIC_WEAK) != 0);
    obj = js_create_from_ctor(ctx, new_target, JS_CLASS_MAP + magic);
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    s = js_mallocz(ctx, sizeof(*s));
    if (!s)
        goto fail;
    init_list_head(&s->records);
    s->is_weak = is_weak;
    JS_SetOpaqueInternal(obj, s);
    s->hash_size = 1;
    s->hash_table = js_malloc(ctx, sizeof(s->hash_table[0]) * s->hash_size);
    if (!s->hash_table)
        goto fail;
    init_list_head(&s->hash_table[0]);
    s->record_count_threshold = 4;

    arr = JS_UNDEFINED;
    if (argc > 0)
        arr = argv[0];
    if (!JS_IsUndefined(arr) && !JS_IsNull(arr)) {
        JSValue item, ret;
        int done;

        adder = JS_GetProperty(ctx, obj, is_set ? JS_ATOM_add : JS_ATOM_set);
        if (JS_IsException(adder))
            goto fail;
        if (!JS_IsFunction(ctx, adder)) {
            JS_ThrowTypeError(ctx, "%s is not a function", is_set ? "set" : "add");
            goto fail;
        }

        iter = JS_GetIterator(ctx, arr, false);
        if (JS_IsException(iter))
            goto fail;
        next_method = JS_GetProperty(ctx, iter, JS_ATOM_next);
        if (JS_IsException(next_method))
            goto fail;

        for(;;) {
            item = JS_IteratorNext(ctx, iter, next_method, 0, NULL, &done);
            if (JS_IsException(item))
                goto fail;
            if (done)
                break;
            if (is_set) {
                ret = JS_Call(ctx, adder, obj, 1, vc(&item));
                if (JS_IsException(ret)) {
                    JS_FreeValue(ctx, item);
                    goto fail;
                }
            } else {
                JSValue key, value;
                JSValueConst args[2];
                key = JS_UNDEFINED;
                value = JS_UNDEFINED;
                if (!JS_IsObject(item)) {
                    JS_ThrowTypeErrorNotAnObject(ctx);
                    goto fail1;
                }
                key = JS_GetPropertyUint32(ctx, item, 0);
                if (JS_IsException(key))
                    goto fail1;
                value = JS_GetPropertyUint32(ctx, item, 1);
                if (JS_IsException(value))
                    goto fail1;
                args[0] = key;
                args[1] = value;
                ret = JS_Call(ctx, adder, obj, 2, args);
                if (JS_IsException(ret)) {
                fail1:
                    JS_FreeValue(ctx, item);
                    JS_FreeValue(ctx, key);
                    JS_FreeValue(ctx, value);
                    goto fail;
                }
                JS_FreeValue(ctx, key);
                JS_FreeValue(ctx, value);
            }
            JS_FreeValue(ctx, ret);
            JS_FreeValue(ctx, item);
        }
        JS_FreeValue(ctx, next_method);
        JS_FreeValue(ctx, iter);
        JS_FreeValue(ctx, adder);
    }
    return obj;
 fail:
    if (JS_IsObject(iter)) {
        /* close the iterator object, preserving pending exception */
        JS_IteratorClose(ctx, iter, true);
    }
    JS_FreeValue(ctx, next_method);
    JS_FreeValue(ctx, iter);
    JS_FreeValue(ctx, adder);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

/* XXX: could normalize strings to speed up comparison */
static JSValue map_normalize_key(JSContext *ctx, JSValue key)
{
    uint32_t tag = JS_VALUE_GET_TAG(key);
    // convert -0.0 to +0.0
    // not a leak; |key| and return value are not heap-allocated
    if (JS_TAG_IS_FLOAT64(tag) && JS_VALUE_GET_FLOAT64(key) == 0.0)
        return js_int32(0);
    return key;
}

static JSValueConst map_normalize_key_const(JSContext *ctx, JSValueConst key)
{
    return safe_const(map_normalize_key(ctx, unsafe_unconst(key)));
}

/* XXX: better hash ? */
static uint32_t map_hash_key(JSContext *ctx, JSValueConst key)
{
    uint32_t tag = JS_VALUE_GET_NORM_TAG(key);
    uint32_t h;
    double d;
    JSFloat64Union u;
    JSBigInt *r;

    switch(tag) {
    case JS_TAG_BOOL:
        h = JS_VALUE_GET_INT(key);
        break;
    case JS_TAG_STRING:
        h = hash_string(JS_VALUE_GET_STRING(key), 0);
        break;
    case JS_TAG_STRING_ROPE:
        h = hash_string_rope(key, 0);
        break;
    case JS_TAG_OBJECT:
    case JS_TAG_SYMBOL:
        h = (uintptr_t)JS_VALUE_GET_PTR(key) * 3163;
        break;
    case JS_TAG_INT:
        d = JS_VALUE_GET_INT(key);
        goto hash_float64;
    case JS_TAG_SHORT_BIG_INT:
        d = JS_VALUE_GET_SHORT_BIG_INT(key);
        goto hash_float64;
    case JS_TAG_BIG_INT:
        r = JS_VALUE_GET_PTR(key);
        h = hash_string8((void *)r->tab, r->len * sizeof(*r->tab), 0);
        break;
    case JS_TAG_FLOAT64:
        d = JS_VALUE_GET_FLOAT64(key);
        /* normalize the NaN */
        if (isnan(d))
            d = NAN;
    hash_float64:
        u.d = d;
        h = (u.u32[0] ^ u.u32[1]) * 3163;
        return h ^= JS_TAG_FLOAT64;
    default:
        h = 0;
        break;
    }
    h ^= tag;
    return h;
}

static JSMapRecord *map_find_record(JSContext *ctx, JSMapState *s,
                                    JSValueConst key)
{
    struct list_head *el;
    JSMapRecord *mr;
    uint32_t h;
    h = map_hash_key(ctx, key) & (s->hash_size - 1);
    list_for_each(el, &s->hash_table[h]) {
        mr = list_entry(el, JSMapRecord, hash_link);
        if (js_same_value_zero(ctx, mr->key, key))
            return mr;
    }
    return NULL;
}

static void map_hash_resize(JSContext *ctx, JSMapState *s)
{
    uint32_t new_hash_size, i, h;
    struct list_head *new_hash_table, *el;
    JSMapRecord *mr;

    /* XXX: no reporting of memory allocation failure */
    if (s->hash_size == 1)
        new_hash_size = 4;
    else
        new_hash_size = s->hash_size * 2;
    new_hash_table = js_realloc(ctx, s->hash_table,
                                sizeof(new_hash_table[0]) * new_hash_size);
    if (!new_hash_table)
        return;

    for(i = 0; i < new_hash_size; i++)
        init_list_head(&new_hash_table[i]);

    list_for_each(el, &s->records) {
        mr = list_entry(el, JSMapRecord, link);
        if (!mr->empty) {
            h = map_hash_key(ctx, mr->key) & (new_hash_size - 1);
            list_add_tail(&mr->hash_link, &new_hash_table[h]);
        }
    }
    s->hash_table = new_hash_table;
    s->hash_size = new_hash_size;
    s->record_count_threshold = new_hash_size * 2;
}

static JSWeakRefRecord **get_first_weak_ref(JSValueConst key)
{
        switch (JS_VALUE_GET_TAG(key)) {
        case JS_TAG_OBJECT:
            {
                JSObject *p = JS_VALUE_GET_OBJ(key);
                return &p->first_weak_ref;
            }
            break;
        case JS_TAG_SYMBOL:
            {
                JSAtomStruct *p = JS_VALUE_GET_PTR(key);
                return &p->first_weak_ref;
            }
            break;
        default:
            abort();
        }
        return NULL; // pacify compiler
}

static JSMapRecord *map_add_record(JSContext *ctx, JSMapState *s,
                                   JSValueConst key)
{
    uint32_t h;
    JSMapRecord *mr;

    mr = js_malloc(ctx, sizeof(*mr));
    if (!mr)
        return NULL;
    mr->ref_count = 1;
    mr->map = s;
    mr->empty = false;
    if (s->is_weak) {
        JSWeakRefRecord *wr = js_malloc(ctx, sizeof(*wr));
        if (!wr) {
            js_free(ctx, mr);
            return NULL;
        }
        wr->kind = JS_WEAK_REF_KIND_MAP;
        wr->u.map_record = mr;
        insert_weakref_record(key, wr);
        mr->key = unsafe_unconst(key);
    } else {
        mr->key = js_dup(key);
    }
    h = map_hash_key(ctx, key) & (s->hash_size - 1);
    list_add_tail(&mr->hash_link, &s->hash_table[h]);
    list_add_tail(&mr->link, &s->records);
    s->record_count++;
    if (s->record_count >= s->record_count_threshold) {
        map_hash_resize(ctx, s);
    }
    return mr;
}

/* Remove the weak reference from the object weak
   reference list. we don't use a doubly linked list to
   save space, assuming a given object has few weak
       references to it */
static void delete_map_weak_ref(JSRuntime *rt, JSMapRecord *mr)
{
    JSWeakRefRecord **pwr, *wr;

    pwr = get_first_weak_ref(mr->key);
    for(;;) {
        wr = *pwr;
        assert(wr != NULL);
        if (wr->kind == JS_WEAK_REF_KIND_MAP && wr->u.map_record == mr)
            break;
        pwr = &wr->next_weak_ref;
    }
    *pwr = wr->next_weak_ref;
    js_free_rt(rt, wr);
}

static void map_delete_record(JSRuntime *rt, JSMapState *s, JSMapRecord *mr)
{
    if (mr->empty)
        return;
    list_del(&mr->hash_link);
    if (s->is_weak) {
        delete_map_weak_ref(rt, mr);
    } else {
        JS_FreeValueRT(rt, mr->key);
    }
    JS_FreeValueRT(rt, mr->value);
    if (--mr->ref_count == 0) {
        list_del(&mr->link);
        js_free_rt(rt, mr);
    } else {
        /* keep a zombie record for iterators */
        mr->empty = true;
        mr->key = JS_UNDEFINED;
        mr->value = JS_UNDEFINED;
    }
    s->record_count--;
}

static void map_decref_record(JSRuntime *rt, JSMapRecord *mr)
{
    if (--mr->ref_count == 0) {
        /* the record can be safely removed */
        assert(mr->empty);
        list_del(&mr->link);
        js_free_rt(rt, mr);
    }
}

static JSValue js_map_set(JSContext *ctx, JSValueConst this_val,
                          int argc, JSValueConst *argv, int magic)
{
    JSMapState *s = JS_GetOpaque2(ctx, this_val, JS_CLASS_MAP + magic);
    JSMapRecord *mr;
    JSValueConst key, value;
    int is_set;

    if (!s)
        return JS_EXCEPTION;
    is_set = (magic & MAGIC_SET);
    key = map_normalize_key_const(ctx, argv[0]);
    if (s->is_weak && !is_valid_weakref_target(key))
        return JS_ThrowTypeError(ctx, "invalid value used as %s key", is_set ? "WeakSet" : "WeakMap");
    if (is_set)
        value = JS_UNDEFINED;
    else
        value = argv[1];
    mr = map_find_record(ctx, s, key);
    if (mr) {
        JS_FreeValue(ctx, mr->value);
    } else {
        mr = map_add_record(ctx, s, key);
        if (!mr)
            return JS_EXCEPTION;
    }
    mr->value = js_dup(value);
    return js_dup(this_val);
}

static JSValue js_map_get(JSContext *ctx, JSValueConst this_val,
                          int argc, JSValueConst *argv, int magic)
{
    JSMapState *s = JS_GetOpaque2(ctx, this_val, JS_CLASS_MAP + magic);
    JSMapRecord *mr;
    JSValueConst key;

    if (!s)
        return JS_EXCEPTION;
    key = map_normalize_key_const(ctx, argv[0]);
    mr = map_find_record(ctx, s, key);
    if (!mr)
        return JS_UNDEFINED;
    else
        return js_dup(mr->value);
}

static JSValue js_map_getOrInsert(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv, int magic)
{
    bool computed = magic & 1;
    JSClassID class_id = magic >> 1;
    JSMapState *s = JS_GetOpaque2(ctx, this_val, class_id);
    JSMapRecord *mr;
    JSValueConst key;
    JSValue value;

    if (!s)
        return JS_EXCEPTION;
    if (computed && check_function(ctx, argv[1]))
        return JS_EXCEPTION;
    key = map_normalize_key_const(ctx, argv[0]);
    if (s->is_weak && !is_valid_weakref_target(key))
        return JS_ThrowTypeError(ctx, "invalid value used as WeakMap key");
    mr = map_find_record(ctx, s, key);
    if (!mr) {
        if (computed) {
            value = JS_Call(ctx, argv[1], JS_UNDEFINED, 1, &key);
            if (JS_IsException(value))
                return JS_EXCEPTION;
            mr = map_find_record(ctx, s, key);
            if (mr)
                map_delete_record(ctx->rt, s, mr);
        } else {
            value = js_dup(argv[1]);
        }
        mr = map_add_record(ctx, s, key);
        if (!mr) {
            JS_FreeValue(ctx, value);
            return JS_EXCEPTION;
        }
        mr->value = value;
    }
    return js_dup(mr->value);
}

static JSValue js_map_has(JSContext *ctx, JSValueConst this_val,
                          int argc, JSValueConst *argv, int magic)
{
    JSMapState *s = JS_GetOpaque2(ctx, this_val, JS_CLASS_MAP + magic);
    JSMapRecord *mr;
    JSValueConst key;

    if (!s)
        return JS_EXCEPTION;
    key = map_normalize_key_const(ctx, argv[0]);
    mr = map_find_record(ctx, s, key);
    return js_bool(mr != NULL);
}

static JSValue js_map_delete(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv, int magic)
{
    JSMapState *s = JS_GetOpaque2(ctx, this_val, JS_CLASS_MAP + magic);
    JSMapRecord *mr;
    JSValueConst key;

    if (!s)
        return JS_EXCEPTION;
    key = map_normalize_key_const(ctx, argv[0]);
    mr = map_find_record(ctx, s, key);
    if (!mr)
        return JS_FALSE;
    map_delete_record(ctx->rt, s, mr);
    return JS_TRUE;
}

static JSValue js_map_clear(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv, int magic)
{
    JSMapState *s = JS_GetOpaque2(ctx, this_val, JS_CLASS_MAP + magic);
    struct list_head *el, *el1;
    JSMapRecord *mr;

    if (!s)
        return JS_EXCEPTION;
    list_for_each_safe(el, el1, &s->records) {
        mr = list_entry(el, JSMapRecord, link);
        map_delete_record(ctx->rt, s, mr);
    }
    return JS_UNDEFINED;
}

static JSValue js_map_get_size(JSContext *ctx, JSValueConst this_val, int magic)
{
    JSMapState *s = JS_GetOpaque2(ctx, this_val, JS_CLASS_MAP + magic);
    if (!s)
        return JS_EXCEPTION;
    return js_uint32(s->record_count);
}

static JSValue js_map_forEach(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv, int magic)
{
    JSMapState *s = JS_GetOpaque2(ctx, this_val, JS_CLASS_MAP + magic);
    JSValueConst func, this_arg;
    JSValue ret, args[3];
    struct list_head *el;
    JSMapRecord *mr;

    if (!s)
        return JS_EXCEPTION;
    func = argv[0];
    if (argc > 1)
        this_arg = argv[1];
    else
        this_arg = JS_UNDEFINED;
    if (check_function(ctx, func))
        return JS_EXCEPTION;
    /* Note: the list can be modified while traversing it, but the
       current element is locked */
    el = s->records.next;
    while (el != &s->records) {
        mr = list_entry(el, JSMapRecord, link);
        if (!mr->empty) {
            mr->ref_count++;
            /* must duplicate in case the record is deleted */
            args[1] = js_dup(mr->key);
            if (magic)
                args[0] = args[1];
            else
                args[0] = js_dup(mr->value);
            args[2] = unsafe_unconst(this_val);
            ret = JS_Call(ctx, func, this_arg, 3, vc(args));
            JS_FreeValue(ctx, args[0]);
            if (!magic)
                JS_FreeValue(ctx, args[1]);
            el = el->next;
            map_decref_record(ctx->rt, mr);
            if (JS_IsException(ret))
                return ret;
            JS_FreeValue(ctx, ret);
        } else {
            el = el->next;
        }
    }
    return JS_UNDEFINED;
}

static JSValue js_map_groupBy(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    JSValue res, iter, next, groups, k, v, prop;
    JSValueConst cb, args[2];
    int64_t idx;
    int done;

    // "is function?" check must be observed before argv[0] is accessed
    cb = argv[1];
    if (check_function(ctx, cb))
        return JS_EXCEPTION;

    iter = JS_GetIterator(ctx, argv[0], /*is_async*/false);
    if (JS_IsException(iter))
        return JS_EXCEPTION;

    k = JS_UNDEFINED;
    v = JS_UNDEFINED;
    prop = JS_UNDEFINED;
    groups = JS_UNDEFINED;

    next = JS_GetProperty(ctx, iter, JS_ATOM_next);
    if (JS_IsException(next))
        goto exception;

    groups = js_map_constructor(ctx, JS_UNDEFINED, 0, NULL, 0);
    if (JS_IsException(groups))
        goto exception;

    for (idx = 0; ; idx++) {
        v = JS_IteratorNext(ctx, iter, next, 0, NULL, &done);
        if (JS_IsException(v))
            goto exception;
        if (done)
            break; // v is JS_UNDEFINED

        args[0] = v;
        args[1] = js_int64(idx);
        k = JS_Call(ctx, cb, ctx->global_obj, 2, args);
        if (JS_IsException(k))
            goto exception;

        prop = js_map_get(ctx, groups, 1, vc(&k), 0);
        if (JS_IsException(prop))
            goto exception;

        if (JS_IsUndefined(prop)) {
            prop = JS_NewArray(ctx);
            if (JS_IsException(prop))
                goto exception;
            args[0] = k;
            args[1] = prop;
            res = js_map_set(ctx, groups, 2, args, 0);
            if (JS_IsException(res))
                goto exception;
            JS_FreeValue(ctx, res);
        }

        res = js_array_push(ctx, prop, 1, vc(&v), /*unshift*/0);
        if (JS_IsException(res))
            goto exception;
        // res is an int64

        JS_FreeValue(ctx, prop);
        JS_FreeValue(ctx, k);
        JS_FreeValue(ctx, v);
        prop = JS_UNDEFINED;
        k = JS_UNDEFINED;
        v = JS_UNDEFINED;
    }

    JS_FreeValue(ctx, iter);
    JS_FreeValue(ctx, next);
    return groups;

exception:
    JS_FreeValue(ctx, prop);
    JS_FreeValue(ctx, k);
    JS_FreeValue(ctx, v);
    JS_FreeValue(ctx, groups);
    JS_FreeValue(ctx, iter);
    JS_FreeValue(ctx, next);
    return JS_EXCEPTION;
}

static void js_map_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSObject *p;
    JSMapState *s;
    struct list_head *el, *el1;
    JSMapRecord *mr;

    p = JS_VALUE_GET_OBJ(val);
    s = p->u.map_state;
    if (s) {
        /* if the object is deleted we are sure that no iterator is
           using it */
        list_for_each_safe(el, el1, &s->records) {
            mr = list_entry(el, JSMapRecord, link);
            if (!mr->empty) {
                if (s->is_weak)
                    delete_map_weak_ref(rt, mr);
                else
                    JS_FreeValueRT(rt, mr->key);
                JS_FreeValueRT(rt, mr->value);
            }
            js_free_rt(rt, mr);
        }
        js_free_rt(rt, s->hash_table);
        js_free_rt(rt, s);
    }
}

static void js_map_mark(JSRuntime *rt, JSValueConst val,
                        JS_MarkFunc *mark_func)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);
    JSMapState *s;
    struct list_head *el;
    JSMapRecord *mr;

    s = p->u.map_state;
    if (s) {
        assert(!s->is_weak);
        list_for_each(el, &s->records) {
            mr = list_entry(el, JSMapRecord, link);
            JS_MarkValue(rt, mr->key, mark_func);
            JS_MarkValue(rt, mr->value, mark_func);
        }
    }
}

/* Map Iterator */

typedef struct JSMapIteratorData {
    JSValue obj;
    JSIteratorKindEnum kind;
    JSMapRecord *cur_record;
} JSMapIteratorData;

static void js_map_iterator_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSObject *p;
    JSMapIteratorData *it;

    p = JS_VALUE_GET_OBJ(val);
    it = p->u.map_iterator_data;
    if (it) {
        /* During the GC sweep phase the Map finalizer may be
           called before the Map iterator finalizer */
        if (JS_IsLiveObject(rt, it->obj) && it->cur_record) {
            map_decref_record(rt, it->cur_record);
        }
        JS_FreeValueRT(rt, it->obj);
        js_free_rt(rt, it);
    }
}

static void js_map_iterator_mark(JSRuntime *rt, JSValueConst val,
                                 JS_MarkFunc *mark_func)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);
    JSMapIteratorData *it;
    it = p->u.map_iterator_data;
    if (it) {
        /* the record is already marked by the object */
        JS_MarkValue(rt, it->obj, mark_func);
    }
}

static JSValue js_create_map_iterator(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv, int magic)
{
    JSIteratorKindEnum kind;
    JSMapState *s;
    JSMapIteratorData *it;
    JSValue enum_obj;

    kind = magic >> 2;
    magic &= 3;
    s = JS_GetOpaque2(ctx, this_val, JS_CLASS_MAP + magic);
    if (!s)
        return JS_EXCEPTION;
    enum_obj = JS_NewObjectClass(ctx, JS_CLASS_MAP_ITERATOR + magic);
    if (JS_IsException(enum_obj))
        goto fail;
    it = js_malloc(ctx, sizeof(*it));
    if (!it) {
        JS_FreeValue(ctx, enum_obj);
        goto fail;
    }
    it->obj = js_dup(this_val);
    it->kind = kind;
    it->cur_record = NULL;
    JS_SetOpaqueInternal(enum_obj, it);
    return enum_obj;
 fail:
    return JS_EXCEPTION;
}

static JSValue js_map_iterator_next(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv,
                                    int *pdone, int magic)
{
    JSMapIteratorData *it;
    JSMapState *s;
    JSMapRecord *mr;
    struct list_head *el;

    it = JS_GetOpaque2(ctx, this_val, JS_CLASS_MAP_ITERATOR + magic);
    if (!it) {
        *pdone = false;
        return JS_EXCEPTION;
    }
    if (JS_IsUndefined(it->obj))
        goto done;
    s = JS_GetOpaque(it->obj, JS_CLASS_MAP + magic);
    assert(s != NULL);
    if (!it->cur_record) {
        el = s->records.next;
    } else {
        mr = it->cur_record;
        el = mr->link.next;
        map_decref_record(ctx->rt, mr); /* the record can be freed here */
    }
    for(;;) {
        if (el == &s->records) {
            /* no more record  */
            it->cur_record = NULL;
            JS_FreeValue(ctx, it->obj);
            it->obj = JS_UNDEFINED;
        done:
            /* end of enumeration */
            *pdone = true;
            return JS_UNDEFINED;
        }
        mr = list_entry(el, JSMapRecord, link);
        if (!mr->empty)
            break;
        /* get the next record */
        el = mr->link.next;
    }

    /* lock the record so that it won't be freed */
    mr->ref_count++;
    it->cur_record = mr;
    *pdone = false;

    if (it->kind == JS_ITERATOR_KIND_KEY) {
        return js_dup(mr->key);
    } else {
        JSValueConst args[2];
        args[0] = mr->key;
        if (magic)
            args[1] = mr->key;
        else
            args[1] = mr->value;
        if (it->kind == JS_ITERATOR_KIND_VALUE) {
            return js_dup(args[1]);
        } else {
            return js_create_array(ctx, 2, args);
        }
    }
}

static JSValue js_map_read(BCReaderState *s, int magic)
{
    JSContext *ctx = s->ctx;
    JSValue obj, rv, argv[2];
    uint32_t i, prop_count;

    argv[0] = JS_UNDEFINED;
    argv[1] = JS_UNDEFINED;
    obj = js_map_constructor(ctx, JS_UNDEFINED, 0, NULL, magic);
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    if (BC_add_object_ref(s, obj))
        goto fail;
    if (bc_get_leb128(s, &prop_count))
        goto fail;
    for(i = 0; i < prop_count; i++) {
        argv[0] = JS_ReadObjectRec(s);
        if (JS_IsException(argv[0]))
            goto fail;
        if (!(magic & MAGIC_SET)) {
            argv[1] = JS_ReadObjectRec(s);
            if (JS_IsException(argv[1]))
                goto fail;
        }
        rv = js_map_set(ctx, obj, countof(argv), vc(argv), magic);
        if (JS_IsException(rv))
            goto fail;
        JS_FreeValue(ctx, rv);
        JS_FreeValue(ctx, argv[0]);
        JS_FreeValue(ctx, argv[1]);
        argv[0] = JS_UNDEFINED;
        argv[1] = JS_UNDEFINED;
    }
    return obj;
 fail:
    JS_FreeValue(ctx, obj);
    JS_FreeValue(ctx, argv[0]);
    JS_FreeValue(ctx, argv[1]);
    return JS_EXCEPTION;
}

static int js_map_write(BCWriterState *s, struct JSMapState *map_state,
                        int magic)
{
    struct list_head *el;
    JSMapRecord *mr;

    bc_put_leb128(s, map_state ? map_state->record_count : 0);
    if (map_state) {
        list_for_each(el, &map_state->records) {
            mr = list_entry(el, JSMapRecord, link);
            if (JS_WriteObjectRec(s, mr->key))
                return -1;
            // mr->value is always JS_UNDEFINED for sets
            if (!(magic & MAGIC_SET))
                if (JS_WriteObjectRec(s, mr->value))
                    return -1;
        }
    }

    return 0;
}

static JSValue JS_ReadMap(BCReaderState *s)
{
    return js_map_read(s, 0);
}

static JSValue JS_ReadSet(BCReaderState *s)
{
    return js_map_read(s, MAGIC_SET);
}

static int JS_WriteMap(BCWriterState *s, struct JSMapState *map_state)
{
    return js_map_write(s, map_state, 0);
}

static int JS_WriteSet(BCWriterState *s, struct JSMapState *map_state)
{
    return js_map_write(s, map_state, MAGIC_SET);
}

static int js_setlike_get_props(JSContext *ctx, JSValueConst setlike,
                                uint64_t *psize, JSValue *phas, JSValue *pkeys)
{
    JSValue has, keys, v;
    JSMapState *s;
    uint64_t size;
    double d;

    keys = JS_UNDEFINED;
    has = JS_UNDEFINED;
    s = JS_GetOpaque(setlike, JS_CLASS_SET);
    if (s) {
        size = s->record_count;
    } else {
        v = JS_GetProperty(ctx, setlike, JS_ATOM_size);
        if (JS_IsException(v))
            return -1;
        if (JS_IsUndefined(v)) {
            JS_ThrowTypeError(ctx, ".size is undefined");
            return -1;
        }
        if (JS_ToFloat64Free(ctx, &d, v) < 0)
            return -1;
        if (d < 0) {
            JS_ThrowRangeError(ctx, ".size is not a legal size");
            return -1;
        }
        if (isnan(d)) {
            JS_ThrowTypeError(ctx, ".size is not a legal size");
            return -1;
        }
        if (isinf(d) || d > (double)MAX_SAFE_INTEGER) {
            size = UINT64_MAX;
        } else {
            size = (uint64_t)d; // cast for expository reasons
        }
    }
    has = JS_GetProperty(ctx, setlike, JS_ATOM_has);
    if (JS_IsException(has))
        return -1;
    if (!JS_IsFunction(ctx, has)) {
        JS_ThrowTypeError(ctx, ".has is not a function");
        goto fail;
    }
    keys = JS_GetProperty(ctx, setlike, JS_ATOM_keys);
    if (JS_IsException(keys))
        goto fail;
    if (!JS_IsFunction(ctx, keys)) {
        JS_ThrowTypeError(ctx, ".keys is not a function");
        goto fail;
    }
    *psize = size;
    *phas = has;
    *pkeys = keys;
    return 0;
fail:
    JS_FreeValue(ctx, has);
    JS_FreeValue(ctx, keys);
    return -1;
}

static JSValue js_set_isDisjointFrom(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    JSValue has, item, iter, keys, next, rv, rval;
    JSValueConst setlike;
    int done;
    bool found;
    JSMapState *s;
    uint64_t size;
    int ok;

    s = JS_GetOpaque2(ctx, this_val, JS_CLASS_SET);
    if (!s)
        return JS_EXCEPTION;
    setlike = argv[0];
    if (js_setlike_get_props(ctx, setlike, &size, &has, &keys) < 0)
        return JS_EXCEPTION;
    iter = JS_UNDEFINED;
    next = JS_UNDEFINED;
    rval = JS_EXCEPTION;
    if (s->record_count > size) {
        iter = JS_Call(ctx, keys, setlike, 0, NULL);
        if (JS_IsException(iter))
            goto exception;
        next = JS_GetProperty(ctx, iter, JS_ATOM_next);
        if (JS_IsException(next))
            goto exception;
        found = false;
        for (;;) {
            item = JS_IteratorNext(ctx, iter, next, 0, NULL, &done);
            if (JS_IsException(item))
                goto exception;
            if (done) // item is JS_UNDEFINED
                break;
            item = map_normalize_key(ctx, item);
            found = (NULL != map_find_record(ctx, s, item));
            JS_FreeValue(ctx, item);
            if (!found)
                continue;
            if (JS_IteratorClose(ctx, iter, /*is_exception_pending*/false) < 0)
                goto exception;
            break;
        }
    } else {
        iter = js_create_map_iterator(ctx, this_val, 0, NULL, MAGIC_SET);
        if (JS_IsException(iter))
            goto exception;
        found = false;
        do {
            item = js_map_iterator_next(ctx, iter, 0, NULL, &done, MAGIC_SET);
            if (JS_IsException(item))
                goto exception;
            if (done) // item is JS_UNDEFINED
                break;
            rv = JS_Call(ctx, has, setlike, 1, vc(&item));
            JS_FreeValue(ctx, item);
            ok = JS_ToBoolFree(ctx, rv); // returns -1 if rv is JS_EXCEPTION
            if (ok < 0)
                goto exception;
            found = (ok > 0);
        } while (!found);
    }
    rval = !found ? JS_TRUE : JS_FALSE;
exception:
    JS_FreeValue(ctx, has);
    JS_FreeValue(ctx, keys);
    JS_FreeValue(ctx, iter);
    JS_FreeValue(ctx, next);
    return rval;
}

static JSValue js_set_isSubsetOf(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    JSValue has, item, iter, keys, next, rv, rval;
    JSValueConst setlike;
    bool found;
    JSMapState *s;
    uint64_t size;
    int done, ok;

    s = JS_GetOpaque2(ctx, this_val, JS_CLASS_SET);
    if (!s)
        return JS_EXCEPTION;
    setlike = argv[0];
    if (js_setlike_get_props(ctx, setlike, &size, &has, &keys) < 0)
        return JS_EXCEPTION;
    iter = JS_UNDEFINED;
    next = JS_UNDEFINED;
    rval = JS_EXCEPTION;
    found = false;
    if (s->record_count > size)
        goto fini;
    iter = js_create_map_iterator(ctx, this_val, 0, NULL, MAGIC_SET);
    if (JS_IsException(iter))
        goto exception;
    found = true;
    do {
        item = js_map_iterator_next(ctx, iter, 0, NULL, &done, MAGIC_SET);
        if (JS_IsException(item))
            goto exception;
        if (done) // item is JS_UNDEFINED
            break;
        rv = JS_Call(ctx, has, setlike, 1, vc(&item));
        JS_FreeValue(ctx, item);
        ok = JS_ToBoolFree(ctx, rv); // returns -1 if rv is JS_EXCEPTION
        if (ok < 0)
            goto exception;
        found = (ok > 0);
    } while (found);
fini:
    rval = found ? JS_TRUE : JS_FALSE;
exception:
    JS_FreeValue(ctx, has);
    JS_FreeValue(ctx, keys);
    JS_FreeValue(ctx, iter);
    JS_FreeValue(ctx, next);
    return rval;
}

static JSValue js_set_isSupersetOf(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    JSValue has, item, iter, keys, next, rval;
    JSValueConst setlike;
    int done;
    bool found;
    JSMapState *s;
    uint64_t size;

    s = JS_GetOpaque2(ctx, this_val, JS_CLASS_SET);
    if (!s)
        return JS_EXCEPTION;
    setlike = argv[0];
    if (js_setlike_get_props(ctx, setlike, &size, &has, &keys) < 0)
        return JS_EXCEPTION;
    iter = JS_UNDEFINED;
    next = JS_UNDEFINED;
    rval = JS_EXCEPTION;
    found = false;
    if (s->record_count < size)
        goto fini;
    iter = JS_Call(ctx, keys, setlike, 0, NULL);
    if (JS_IsException(iter))
        goto exception;
    next = JS_GetProperty(ctx, iter, JS_ATOM_next);
    if (JS_IsException(next))
        goto exception;
    found = true;
    for (;;) {
        item = JS_IteratorNext(ctx, iter, next, 0, NULL, &done);
        if (JS_IsException(item))
            goto exception;
        if (done) // item is JS_UNDEFINED
            break;
        item = map_normalize_key(ctx, item);
        found = (NULL != map_find_record(ctx, s, item));
        JS_FreeValue(ctx, item);
        if (found)
            continue;
        if (JS_IteratorClose(ctx, iter, /*is_exception_pending*/false) < 0)
            goto exception;
        break;
    }
fini:
    rval = found ? JS_TRUE : JS_FALSE;
exception:
    JS_FreeValue(ctx, has);
    JS_FreeValue(ctx, keys);
    JS_FreeValue(ctx, iter);
    JS_FreeValue(ctx, next);
    return rval;
}

static JSValue js_set_intersection(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    JSValue has, item, iter, keys, newset, next, rv;
    JSValueConst setlike;
    JSMapState *s, *t;
    JSMapRecord *mr;
    uint64_t size;
    int done, ok;

    s = JS_GetOpaque2(ctx, this_val, JS_CLASS_SET);
    if (!s)
        return JS_EXCEPTION;
    setlike = argv[0];
    if (js_setlike_get_props(ctx, setlike, &size, &has, &keys) < 0)
        return JS_EXCEPTION;
    iter = JS_UNDEFINED;
    next = JS_UNDEFINED;
    newset = JS_UNDEFINED;
    if (s->record_count > size) {
        iter = JS_Call(ctx, keys, setlike, 0, NULL);
        if (JS_IsException(iter))
            goto exception;
        next = JS_GetProperty(ctx, iter, JS_ATOM_next);
        if (JS_IsException(next))
            goto exception;
        newset = js_map_constructor(ctx, JS_UNDEFINED, 0, NULL, MAGIC_SET);
        if (JS_IsException(newset))
            goto exception;
        t = JS_GetOpaque(newset, JS_CLASS_SET);
        for (;;) {
            item = JS_IteratorNext(ctx, iter, next, 0, NULL, &done);
            if (JS_IsException(item))
                goto exception;
            if (done) // item is JS_UNDEFINED
                break;
            item = map_normalize_key(ctx, item);
            if (!map_find_record(ctx, s, item)) {
                JS_FreeValue(ctx, item);
            } else if (map_find_record(ctx, t, item)) {
                JS_FreeValue(ctx, item); // no duplicates
            } else if ((mr = map_add_record(ctx, t, item))) {
                mr->value = JS_UNDEFINED;
            } else {
                JS_FreeValue(ctx, item);
                goto exception;
            }
        }
    } else {
        iter = js_create_map_iterator(ctx, this_val, 0, NULL, MAGIC_SET);
        if (JS_IsException(iter))
            goto exception;
        newset = js_map_constructor(ctx, JS_UNDEFINED, 0, NULL, MAGIC_SET);
        if (JS_IsException(newset))
            goto exception;
        t = JS_GetOpaque(newset, JS_CLASS_SET);
        for (;;) {
            item = js_map_iterator_next(ctx, iter, 0, NULL, &done, MAGIC_SET);
            if (JS_IsException(item))
                goto exception;
            if (done) // item is JS_UNDEFINED
                break;
            rv = JS_Call(ctx, has, setlike, 1, vc(&item));
            ok = JS_ToBoolFree(ctx, rv); // returns -1 if rv is JS_EXCEPTION
            if (ok > 0) {
                item = map_normalize_key(ctx, item);
                if (map_find_record(ctx, t, item)) {
                    JS_FreeValue(ctx, item); // no duplicates
                } else if ((mr = map_add_record(ctx, t, item))) {
                    mr->value = JS_UNDEFINED;
                } else {
                    JS_FreeValue(ctx, item);
                    goto exception;
                }
            } else {
                JS_FreeValue(ctx, item);
                if (ok < 0)
                    goto exception;
            }
        }
    }
    goto fini;
exception:
    JS_FreeValue(ctx, newset);
    newset = JS_EXCEPTION;
fini:
    JS_FreeValue(ctx, has);
    JS_FreeValue(ctx, keys);
    JS_FreeValue(ctx, iter);
    JS_FreeValue(ctx, next);
    return newset;
}

static JSValue js_set_difference(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    JSValue has, item, iter, keys, newset, next, rv;
    JSValueConst setlike;
    JSMapState *s, *t;
    JSMapRecord *mr;
    uint64_t size;
    int done;
    int ok;

    s = JS_GetOpaque2(ctx, this_val, JS_CLASS_SET);
    if (!s)
        return JS_EXCEPTION;
    setlike = argv[0];
    if (js_setlike_get_props(ctx, setlike, &size, &has, &keys) < 0)
        return JS_EXCEPTION;
    iter = JS_UNDEFINED;
    next = JS_UNDEFINED;
    newset = JS_UNDEFINED;
    if (s->record_count > size) {
        iter = JS_Call(ctx, keys, setlike, 0, NULL);
        if (JS_IsException(iter))
            goto exception;
        next = JS_GetProperty(ctx, iter, JS_ATOM_next);
        if (JS_IsException(next))
            goto exception;
        newset = js_map_constructor(ctx, JS_UNDEFINED, 1, &this_val, MAGIC_SET);
        if (JS_IsException(newset))
            goto exception;
        t = JS_GetOpaque(newset, JS_CLASS_SET);
        for (;;) {
            item = JS_IteratorNext(ctx, iter, next, 0, NULL, &done);
            if (JS_IsException(item))
                goto exception;
            if (done) // item is JS_UNDEFINED
                break;
            item = map_normalize_key(ctx, item);
            mr = map_find_record(ctx, t, item);
            if (mr)
                map_delete_record(ctx->rt, t, mr);
            JS_FreeValue(ctx, item);
        }
    } else {
        iter = js_create_map_iterator(ctx, this_val, 0, NULL, MAGIC_SET);
        if (JS_IsException(iter))
            goto exception;
        newset = js_map_constructor(ctx, JS_UNDEFINED, 0, NULL, MAGIC_SET);
        if (JS_IsException(newset))
            goto exception;
        t = JS_GetOpaque(newset, JS_CLASS_SET);
        for (;;) {
            item = js_map_iterator_next(ctx, iter, 0, NULL, &done, MAGIC_SET);
            if (JS_IsException(item))
                goto exception;
            if (done) // item is JS_UNDEFINED
                break;
            rv = JS_Call(ctx, has, setlike, 1, vc(&item));
            ok = JS_ToBoolFree(ctx, rv); // returns -1 if rv is JS_EXCEPTION
            if (ok == 0) {
                item = map_normalize_key(ctx, item);
                if (map_find_record(ctx, t, item)) {
                    JS_FreeValue(ctx, item); // no duplicates
                } else if ((mr = map_add_record(ctx, t, item))) {
                    mr->value = JS_UNDEFINED;
                } else {
                    JS_FreeValue(ctx, item);
                    goto exception;
                }
            } else {
                JS_FreeValue(ctx, item);
                if (ok < 0)
                    goto exception;
            }
        }
    }
    goto fini;
exception:
    JS_FreeValue(ctx, newset);
    newset = JS_EXCEPTION;
fini:
    JS_FreeValue(ctx, has);
    JS_FreeValue(ctx, keys);
    JS_FreeValue(ctx, iter);
    JS_FreeValue(ctx, next);
    return newset;
}

static JSValue js_set_symmetricDifference(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv)
{
    JSValue has, item, iter, keys, newset, next;
    JSValueConst setlike;
    struct list_head *el;
    JSMapState *s, *t;
    JSMapRecord *mr;
    uint64_t size;
    int done;
    bool present;

    s = JS_GetOpaque2(ctx, this_val, JS_CLASS_SET);
    if (!s)
        return JS_EXCEPTION;
    setlike = argv[0];
    if (js_setlike_get_props(ctx, setlike, &size, &has, &keys) < 0)
        return JS_EXCEPTION;
    JS_FreeValue(ctx, has);
    iter = JS_UNDEFINED;
    next = JS_UNDEFINED;
    newset = js_map_constructor(ctx, JS_UNDEFINED, 0, NULL, MAGIC_SET);
    if (JS_IsException(newset))
        goto exception;
    t = JS_GetOpaque(newset, JS_CLASS_SET);
    // can't clone this_val using js_map_constructor(),
    // test262 mandates we don't call the .add method
    list_for_each(el, &s->records) {
        mr = list_entry(el, JSMapRecord, link);
        if (mr->empty)
            continue;
        mr = map_add_record(ctx, t, js_dup(mr->key));
        if (!mr)
            goto exception;
        mr->value = JS_UNDEFINED;
    }
    iter = JS_Call(ctx, keys, setlike, 0, NULL);
    if (JS_IsException(iter))
        goto exception;
    next = JS_GetProperty(ctx, iter, JS_ATOM_next);
    if (JS_IsException(next))
        goto exception;
    for (;;) {
        item = JS_IteratorNext(ctx, iter, next, 0, NULL, &done);
        if (JS_IsException(item))
            goto exception;
        if (done) // item is JS_UNDEFINED
            break;
        // note the subtlety here: due to mutating iterators, it's
        // possible for keys to disappear during iteration; test262
        // still expects us to maintain insertion order though, so
        // we first check |this|, then |new|; |new| is a copy of |this|
        // - if item exists in |this|, delete (if it exists) from |new|
        // - if item misses in |this| and |new|, add to |new|
        // - if item exists in |new| but misses in |this|, *don't* add it,
        //   mutating iterator erased it
        item = map_normalize_key(ctx, item);
        present = (NULL != map_find_record(ctx, s, item));
        mr = map_find_record(ctx, t, item);
        if (present) {
            if (mr)
                map_delete_record(ctx->rt, t, mr);
            JS_FreeValue(ctx, item);
        } else if (mr) {
            JS_FreeValue(ctx, item);
        } else {
            mr = map_add_record(ctx, t, item);
            if (!mr) {
                JS_FreeValue(ctx, item);
                goto exception;
            }
            mr->value = JS_UNDEFINED;
        }
    }
    goto fini;
exception:
    JS_FreeValue(ctx, newset);
    newset = JS_EXCEPTION;
fini:
    JS_FreeValue(ctx, keys);
    JS_FreeValue(ctx, next);
    JS_FreeValue(ctx, iter);
    return newset;
}

static JSValue js_set_union(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv)
{
    JSValue has, item, iter, keys, newset, next, rv;
    JSValueConst setlike;
    struct list_head *el;
    JSMapState *s, *t;
    JSMapRecord *mr;
    uint64_t size;
    int done;

    iter = JS_UNDEFINED;
    s = JS_GetOpaque2(ctx, this_val, JS_CLASS_SET);
    if (!s)
        return JS_EXCEPTION;
    setlike = argv[0];
    if (js_setlike_get_props(ctx, setlike, &size, &has, &keys) < 0)
        return JS_EXCEPTION;
    JS_FreeValue(ctx, has);
    iter = JS_UNDEFINED;
    next = JS_UNDEFINED;
    newset = js_map_constructor(ctx, JS_UNDEFINED, 0, NULL, MAGIC_SET);
    if (JS_IsException(newset))
        goto exception;
    t = JS_GetOpaque(newset, JS_CLASS_SET);
    list_for_each(el, &s->records) {
        mr = list_entry(el, JSMapRecord, link);
        if (mr->empty)
            continue;
        mr = map_add_record(ctx, t, js_dup(mr->key));
        if (!mr)
            goto exception;
        mr->value = JS_UNDEFINED;
    }
    iter = JS_Call(ctx, keys, setlike, 0, NULL);
    if (JS_IsException(iter))
        goto exception;
    next = JS_GetProperty(ctx, iter, JS_ATOM_next);
    if (JS_IsException(next))
        goto exception;
    for (;;) {
        item = JS_IteratorNext(ctx, iter, next, 0, NULL, &done);
        if (JS_IsException(item))
            goto exception;
        if (done) // item is JS_UNDEFINED
            break;
        rv = js_map_set(ctx, newset, 1, vc(&item), MAGIC_SET);
        JS_FreeValue(ctx, item);
        if (JS_IsException(rv))
            goto exception;
        JS_FreeValue(ctx, rv);
    }
    goto fini;
exception:
    JS_FreeValue(ctx, newset);
    newset = JS_EXCEPTION;
fini:
    JS_FreeValue(ctx, keys);
    JS_FreeValue(ctx, next);
    JS_FreeValue(ctx, iter);
    return newset;
}

static const JSCFunctionListEntry js_map_funcs[] = {
    JS_CFUNC_DEF("groupBy", 2, js_map_groupBy ),
    JS_CGETSET_DEF("[Symbol.species]", js_get_this, NULL ),
};

static const JSCFunctionListEntry js_set_funcs[] = {
    JS_CGETSET_DEF("[Symbol.species]", js_get_this, NULL ),
};

static const JSCFunctionListEntry js_map_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("set", 2, js_map_set, 0 ),
    JS_CFUNC_MAGIC_DEF("get", 1, js_map_get, 0 ),
    JS_CFUNC_MAGIC_DEF("getOrInsert", 2, js_map_getOrInsert,
                       JS_CLASS_MAP<<1 | /*computed*/false ),
    JS_CFUNC_MAGIC_DEF("getOrInsertComputed", 2, js_map_getOrInsert,
                       JS_CLASS_MAP<<1 | /*computed*/true ),
    JS_CFUNC_MAGIC_DEF("has", 1, js_map_has, 0 ),
    JS_CFUNC_MAGIC_DEF("delete", 1, js_map_delete, 0 ),
    JS_CFUNC_MAGIC_DEF("clear", 0, js_map_clear, 0 ),
    JS_CGETSET_MAGIC_DEF("size", js_map_get_size, NULL, 0),
    JS_CFUNC_MAGIC_DEF("forEach", 1, js_map_forEach, 0 ),
    JS_CFUNC_MAGIC_DEF("values", 0, js_create_map_iterator, (JS_ITERATOR_KIND_VALUE << 2) | 0 ),
    JS_CFUNC_MAGIC_DEF("keys", 0, js_create_map_iterator, (JS_ITERATOR_KIND_KEY << 2) | 0 ),
    JS_CFUNC_MAGIC_DEF("entries", 0, js_create_map_iterator, (JS_ITERATOR_KIND_KEY_AND_VALUE << 2) | 0 ),
    JS_ALIAS_DEF("[Symbol.iterator]", "entries" ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Map", JS_PROP_CONFIGURABLE ),
};

static const JSCFunctionListEntry js_map_iterator_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_map_iterator_next, 0 ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Map Iterator", JS_PROP_CONFIGURABLE ),
};

static const JSCFunctionListEntry js_set_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("add", 1, js_map_set, MAGIC_SET ),
    JS_CFUNC_MAGIC_DEF("has", 1, js_map_has, MAGIC_SET ),
    JS_CFUNC_MAGIC_DEF("delete", 1, js_map_delete, MAGIC_SET ),
    JS_CFUNC_MAGIC_DEF("clear", 0, js_map_clear, MAGIC_SET ),
    JS_CGETSET_MAGIC_DEF("size", js_map_get_size, NULL, MAGIC_SET ),
    JS_CFUNC_MAGIC_DEF("forEach", 1, js_map_forEach, MAGIC_SET ),
    JS_CFUNC_DEF("isDisjointFrom", 1, js_set_isDisjointFrom ),
    JS_CFUNC_DEF("isSubsetOf", 1, js_set_isSubsetOf ),
    JS_CFUNC_DEF("isSupersetOf", 1, js_set_isSupersetOf ),
    JS_CFUNC_DEF("intersection", 1, js_set_intersection ),
    JS_CFUNC_DEF("difference", 1, js_set_difference ),
    JS_CFUNC_DEF("symmetricDifference", 1, js_set_symmetricDifference ),
    JS_CFUNC_DEF("union", 1, js_set_union ),
    JS_CFUNC_MAGIC_DEF("values", 0, js_create_map_iterator, (JS_ITERATOR_KIND_KEY << 2) | MAGIC_SET ),
    JS_ALIAS_DEF("keys", "values" ),
    JS_ALIAS_DEF("[Symbol.iterator]", "values" ),
    JS_CFUNC_MAGIC_DEF("entries", 0, js_create_map_iterator, (JS_ITERATOR_KIND_KEY_AND_VALUE << 2) | MAGIC_SET ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Set", JS_PROP_CONFIGURABLE ),
};

static const JSCFunctionListEntry js_set_iterator_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_map_iterator_next, MAGIC_SET ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Set Iterator", JS_PROP_CONFIGURABLE ),
};

static const JSCFunctionListEntry js_weak_map_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("set", 2, js_map_set, MAGIC_WEAK ),
    JS_CFUNC_MAGIC_DEF("get", 1, js_map_get, MAGIC_WEAK ),
    JS_CFUNC_MAGIC_DEF("getOrInsert", 2, js_map_getOrInsert,
                       JS_CLASS_WEAKMAP<<1 | /*computed*/false ),
    JS_CFUNC_MAGIC_DEF("getOrInsertComputed", 2, js_map_getOrInsert,
                       JS_CLASS_WEAKMAP<<1 | /*computed*/true ),
    JS_CFUNC_MAGIC_DEF("has", 1, js_map_has, MAGIC_WEAK ),
    JS_CFUNC_MAGIC_DEF("delete", 1, js_map_delete, MAGIC_WEAK ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WeakMap", JS_PROP_CONFIGURABLE ),
};

static const JSCFunctionListEntry js_weak_set_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("add", 1, js_map_set, MAGIC_SET | MAGIC_WEAK ),
    JS_CFUNC_MAGIC_DEF("has", 1, js_map_has, MAGIC_SET | MAGIC_WEAK ),
    JS_CFUNC_MAGIC_DEF("delete", 1, js_map_delete, MAGIC_SET | MAGIC_WEAK ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WeakSet", JS_PROP_CONFIGURABLE ),
};

static const JSCFunctionListEntry * const js_map_proto_funcs_ptr[6] = {
    js_map_proto_funcs,
    js_set_proto_funcs,
    js_weak_map_proto_funcs,
    js_weak_set_proto_funcs,
    js_map_iterator_proto_funcs,
    js_set_iterator_proto_funcs,
};

static const uint8_t js_map_proto_funcs_count[6] = {
    countof(js_map_proto_funcs),
    countof(js_set_proto_funcs),
    countof(js_weak_map_proto_funcs),
    countof(js_weak_set_proto_funcs),
    countof(js_map_iterator_proto_funcs),
    countof(js_set_iterator_proto_funcs),
};

int JS_AddIntrinsicMapSet(JSContext *ctx)
{
    int i;
    JSValue obj1;
    char buf[ATOM_GET_STR_BUF_SIZE];
    /* Used to squelch a -Wcast-function-type warning. */
    JSCFunctionType ft = { .constructor_magic = js_map_constructor };

    for(i = 0; i < 4; i++) {
        const char *name = JS_AtomGetStr(ctx, buf, sizeof(buf),
                                         JS_ATOM_Map + i);
        int class_id = JS_CLASS_MAP + i;
        const JSCFunctionListEntry *ctor_funcs;
        int n_ctor_funcs;
        if (class_id == JS_CLASS_MAP) {
            ctor_funcs = js_map_funcs;
            n_ctor_funcs = countof(js_map_funcs);
        } else if (class_id == JS_CLASS_SET) {
            ctor_funcs = js_set_funcs;
            n_ctor_funcs = countof(js_set_funcs);
        } else {
            ctor_funcs = NULL;
            n_ctor_funcs = 0;
        }
        obj1 = JS_NewCConstructor(ctx, class_id, name,
                                  ft.generic, 0, JS_CFUNC_constructor_magic, i,
                                  JS_UNDEFINED,
                                  ctor_funcs, n_ctor_funcs,
                                  js_map_proto_funcs_ptr[i], js_map_proto_funcs_count[i],
                                  0);
        if (JS_IsException(obj1))
            return -1;
        JS_FreeValue(ctx, obj1);
    }

    for(i = 0; i < 2; i++) {
        ctx->class_proto[JS_CLASS_MAP_ITERATOR + i] =
            JS_NewObjectProtoList(ctx, ctx->class_proto[JS_CLASS_ITERATOR],
                                  js_map_proto_funcs_ptr[i + 4],
                                  js_map_proto_funcs_count[i + 4]);
        if (JS_IsException(ctx->class_proto[JS_CLASS_MAP_ITERATOR + i]))
            return -1;
    }
    return 0;
}

/* Generator */
static const JSCFunctionListEntry js_generator_function_proto_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "GeneratorFunction", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_generator_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 1, js_generator_next, GEN_MAGIC_NEXT ),
    JS_ITERATOR_NEXT_DEF("return", 1, js_generator_next, GEN_MAGIC_RETURN ),
    JS_ITERATOR_NEXT_DEF("throw", 1, js_generator_next, GEN_MAGIC_THROW ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Generator", JS_PROP_CONFIGURABLE),
};

/* Explicit resource management */

enum {
    JS_DISPOSE_HINT_SYNC,   /* use: Call(method, value) */
    JS_DISPOSE_HINT_ADOPT,  /* adopt: Call(method, undefined, [value]) */
    JS_DISPOSE_HINT_DEFER,  /* defer: Call(method, undefined) */
};

typedef struct JSDisposableResource {
    JSValue value;
    JSValue method; /* dispose method */
    uint8_t hint;
} JSDisposableResource;

typedef struct JSDisposableStack {
    bool disposed;
    int resource_count;
    int resource_capacity;
    JSDisposableResource *resources;
} JSDisposableStack;

static JSValue js_new_suppressed_error(JSContext *ctx, JSValueConst error,
                                       JSValueConst suppressed)
{
    JSValue obj;

    /* Construct via the intrinsic prototype rather than going through
       SuppressedError.prototype.constructor, which is user-writable. */
    obj = JS_NewObjectProtoClass(ctx,
                                 ctx->native_error_proto[JS_SUPPRESSED_ERROR],
                                 JS_CLASS_ERROR);
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    JS_DefinePropertyValue(ctx, obj, JS_ATOM_error, js_dup(error),
                           JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValue(ctx, obj, JS_ATOM_suppressed, js_dup(suppressed),
                           JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    build_backtrace(ctx, obj, JS_UNDEFINED, NULL, 0, 0,
                    JS_BACKTRACE_FLAG_SKIP_FIRST_LEVEL);
    return obj;
}

/* Perform DisposeResources. Returns 0 on success, -1 on exception.
   completion_error is the pending error (JS_UNDEFINED if none).
   It is consumed (freed) by this function. */
static int js_dispose_resources(JSContext *ctx, JSDisposableStack *ds,
                                JSValue completion_error)
{
    JSValue error = completion_error;
    bool has_error = !JS_IsUndefined(error);
    int i;

    ds->disposed = true;
    /* dispose in LIFO order */
    for (i = ds->resource_count - 1; i >= 0; i--) {
        JSDisposableResource *res = &ds->resources[i];
        JSValue ret;
        if (JS_IsUndefined(res->method)) {
            /* null/undefined resource, skip */
            JS_FreeValue(ctx, res->value);
            continue;
        }
        switch (res->hint) {
        case JS_DISPOSE_HINT_ADOPT:
            ret = JS_Call(ctx, res->method, JS_UNDEFINED, 1, vc(&res->value));
            break;
        case JS_DISPOSE_HINT_DEFER:
            ret = JS_Call(ctx, res->method, JS_UNDEFINED, 0, NULL);
            break;
        default: /* JS_DISPOSE_HINT_SYNC */
            ret = JS_Call(ctx, res->method, res->value, 0, NULL);
            break;
        }
        JS_FreeValue(ctx, res->value);
        JS_FreeValue(ctx, res->method);
        if (JS_IsException(ret)) {
            JSValue new_error = JS_GetException(ctx);
            if (has_error) {
                JSValue suppressed = js_new_suppressed_error(ctx, new_error, error);
                JS_FreeValue(ctx, new_error);
                JS_FreeValue(ctx, error);
                if (JS_IsException(suppressed)) {
                    error = JS_GetException(ctx);
                } else {
                    error = suppressed;
                }
            } else {
                error = new_error;
                has_error = true;
            }
        } else {
            JS_FreeValue(ctx, ret);
        }
    }
    ds->resource_count = 0;
    if (has_error) {
        JS_Throw(ctx, error);
        return -1;
    }
    return 0;
}

static void js_disposable_stack_clear(JSRuntime *rt, JSDisposableStack *ds)
{
    int i;
    for (i = 0; i < ds->resource_count; i++) {
        JS_FreeValueRT(rt, ds->resources[i].value);
        JS_FreeValueRT(rt, ds->resources[i].method);
    }
    js_free_rt(rt, ds->resources);
}

static int js_disposable_stack_add(JSContext *ctx, JSDisposableStack *ds,
                                   JSValueConst value, JSValueConst method,
                                   int hint)
{
    if (ds->resource_count >= ds->resource_capacity) {
        int new_cap = max_int(ds->resource_capacity * 2, 4);
        JSDisposableResource *new_res;
        new_res = js_realloc(ctx, ds->resources,
                             new_cap * sizeof(JSDisposableResource));
        if (!new_res)
            return -1;
        ds->resources = new_res;
        ds->resource_capacity = new_cap;
    }
    ds->resources[ds->resource_count].value = js_dup(value);
    ds->resources[ds->resource_count].method = js_dup(method);
    ds->resources[ds->resource_count].hint = hint;
    ds->resource_count++;
    return 0;
}

