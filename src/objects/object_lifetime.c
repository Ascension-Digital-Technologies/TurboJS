/* Engine domain source: objects/shapes_objects.inc -> object_lifetime.
 * Ownership: objects subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

JSValue JS_NewCFunctionData2(JSContext *ctx, JSCFunctionData *func,
                             const char *name,
                             int length, int magic, int data_len,
                             JSValueConst *data)
{
    JSCFunctionDataRecord *s;
    JSAtom name_atom;
    JSValue func_obj;
    int i;

    func_obj = JS_NewObjectProtoClass(ctx, ctx->function_proto,
                                      JS_CLASS_C_FUNCTION_DATA);
    if (JS_IsException(func_obj))
        return func_obj;
    s = js_malloc(ctx, sizeof(*s) + data_len * sizeof(JSValue));
    if (!s) {
        JS_FreeValue(ctx, func_obj);
        return JS_EXCEPTION;
    }
    s->func = func;
    s->length = length;
    s->data_len = data_len;
    s->magic = magic;
    for(i = 0; i < data_len; i++)
        s->data[i] = js_dup(data[i]);
    JS_SetOpaqueInternal(func_obj, s);
    name_atom = JS_ATOM_empty_string;
    if (name && *name) {
        name_atom = JS_NewAtom(ctx, name);
        if (name_atom == JS_ATOM_NULL) {
            JS_FreeValue(ctx, func_obj);
            return JS_EXCEPTION;
        }
    }
    js_function_set_properties(ctx, func_obj, name_atom, length);
    JS_FreeAtom(ctx, name_atom);
    return func_obj;
}

JSValue JS_NewCFunctionData(JSContext *ctx, JSCFunctionData *func,
                            int length, int magic, int data_len,
                            JSValueConst *data)
{
    return JS_NewCFunctionData2(ctx, func, NULL, length, magic, data_len, data);
}

static JSContext *js_autoinit_get_realm(JSProperty *pr)
{
    return (JSContext *)(pr->u.init.realm_and_id & ~3);
}

static JSAutoInitIDEnum js_autoinit_get_id(JSProperty *pr)
{
    return pr->u.init.realm_and_id & 3;
}

static void js_autoinit_free(JSRuntime *rt, JSProperty *pr)
{
    JS_FreeContext(js_autoinit_get_realm(pr));
}

static void js_autoinit_mark(JSRuntime *rt, JSProperty *pr,
                             JS_MarkFunc *mark_func)
{
    mark_func(rt, &js_autoinit_get_realm(pr)->header);
}

typedef struct JSCClosureRecord {
    JSCClosure *func;
    uint16_t length;
    uint16_t magic;
    void *opaque;
    void (*opaque_finalize)(void *opaque);
} JSCClosureRecord;

static void js_c_closure_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSCClosureRecord *s = JS_GetOpaque(val, JS_CLASS_C_CLOSURE);

    if (s) {
        if (s->opaque_finalize)
           s->opaque_finalize(s->opaque);

        js_free_rt(rt, s);
    }
}

static JSValue js_call_c_closure(JSContext *ctx, JSValueConst func_obj,
                                 JSValueConst this_val,
                                 int argc, JSValueConst *argv, int flags)
{
    JSRuntime *rt = ctx->rt;
    JSStackFrame sf_s, *sf = &sf_s, *prev_sf;
    JSCClosureRecord *s = JS_GetOpaque(func_obj, JS_CLASS_C_CLOSURE);
    JSValueConst *arg_buf;
    JSValue ret;
    int arg_count;
    int i;
    size_t stack_size;

    arg_buf = argv;
    arg_count = s->length;
    if (unlikely(argc < arg_count)) {
        stack_size = arg_count * sizeof(arg_buf[0]);
        if (js_check_stack_overflow(rt, stack_size))
            return JS_ThrowStackOverflow(ctx);
        arg_buf = alloca(stack_size);
        for (i = 0; i < argc; i++)
            arg_buf[i] = argv[i];
        for (i = argc; i < arg_count; i++)
            arg_buf[i] = JS_UNDEFINED;
    }

    prev_sf = rt->current_stack_frame;
    sf->prev_frame = prev_sf;
    rt->current_stack_frame = sf;
    // TODO(bnoordhuis) switch realms like js_call_c_function does
    sf->is_strict_mode = false;
    sf->cur_func = unsafe_unconst(func_obj);
    sf->arg_count = argc;
    ret = s->func(ctx, this_val, argc, arg_buf, s->magic, s->opaque);
    rt->current_stack_frame = sf->prev_frame;

    return ret;
}

JSValue JS_NewCClosure(JSContext *ctx, JSCClosure *func, const char *name,
                       JSCClosureFinalizerFunc *opaque_finalize,
                       int length, int magic, void *opaque)
{
    JSCClosureRecord *s;
    JSAtom name_atom;
    JSValue func_obj;

    func_obj = JS_NewObjectProtoClass(ctx, ctx->function_proto,
        JS_CLASS_C_CLOSURE);
    if (JS_IsException(func_obj))
        return func_obj;
    s = js_malloc(ctx, sizeof(*s));
    if (!s) {
        JS_FreeValue(ctx, func_obj);
        return JS_EXCEPTION;
    }
    s->func = func;
    s->length = length;
    s->magic = magic;
    s->opaque = opaque;
    s->opaque_finalize = opaque_finalize;
    JS_SetOpaqueInternal(func_obj, s);
    name_atom = JS_ATOM_empty_string;
    if (name && *name) {
        name_atom = JS_NewAtom(ctx, name);
        if (name_atom == JS_ATOM_NULL) {
            JS_FreeValue(ctx, func_obj);
            return JS_EXCEPTION;
        }
    }
    js_function_set_properties(ctx, func_obj, name_atom, length);
    JS_FreeAtom(ctx, name_atom);
    return func_obj;
}

static void free_property(JSRuntime *rt, JSProperty *pr, int prop_flags)
{
    if (unlikely(prop_flags & JS_PROP_TMASK)) {
        if ((prop_flags & JS_PROP_TMASK) == JS_PROP_GETSET) {
            if (pr->u.getset.getter)
                JS_FreeValueRT(rt, JS_MKPTR(JS_TAG_OBJECT, pr->u.getset.getter));
            if (pr->u.getset.setter)
                JS_FreeValueRT(rt, JS_MKPTR(JS_TAG_OBJECT, pr->u.getset.setter));
        } else if ((prop_flags & JS_PROP_TMASK) == JS_PROP_VARREF) {
            free_var_ref(rt, pr->u.var_ref);
        } else if ((prop_flags & JS_PROP_TMASK) == JS_PROP_AUTOINIT) {
            js_autoinit_free(rt, pr);
        }
    } else {
        JS_FreeValueRT(rt, pr->u.value);
    }
}

static inline JSShapeProperty *find_own_property1(JSObject *p, JSAtom atom)
{
    JSShape *sh;
    JSShapeProperty *pr, *prop;
    intptr_t h;
    sh = p->shape;
    h = (uintptr_t)atom & sh->prop_hash_mask;
    h = prop_hash_end(sh)[-h - 1];
    prop = get_shape_prop(sh);
    while (h) {
        pr = &prop[h - 1];
        if (likely(pr->atom == atom)) {
            return pr;
        }
        h = pr->hash_next;
    }
    return NULL;
}

static inline JSShapeProperty *find_own_property(JSProperty **ppr,
                                                 JSObject *p,
                                                 JSAtom atom)
{
    JSShape *sh;
    JSShapeProperty *pr, *prop;
    intptr_t h;
    sh = p->shape;
    h = (uintptr_t)atom & sh->prop_hash_mask;
    h = prop_hash_end(sh)[-h - 1];
    prop = get_shape_prop(sh);
    while (h) {
        pr = &prop[h - 1];
        if (likely(pr->atom == atom)) {
            *ppr = &p->prop[h - 1];
            /* the compiler should be able to assume that pr != NULL here */
            return pr;
        }
        h = pr->hash_next;
    }
    *ppr = NULL;
    return NULL;
}

static void free_var_ref(JSRuntime *rt, JSVarRef *var_ref)
{
    if (var_ref) {
        assert(JS_REF_COUNT(var_ref) > 0);
        if (--JS_REF_COUNT(var_ref) == 0) {
            if (var_ref->is_detached) {
                JS_FreeValueRT(rt, var_ref->value);
                remove_gc_object(&var_ref->header);
            } else {
                JSStackFrame *sf = var_ref->stack_frame;
                assert(sf->var_refs[var_ref->var_ref_idx] == var_ref);
                sf->var_refs[var_ref->var_ref_idx] = NULL;
            }
            js_free_rt(rt, var_ref);
        }
    }
}

static void js_array_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);
    uint32_t i;

    for(i = 0; i < p->u.array.count; i++) {
        JS_FreeValueRT(rt, p->u.array.u.values[i]);
    }
    js_free_rt(rt, p->u.array.u.values);
}

static void js_array_mark(JSRuntime *rt, JSValueConst val,
                          JS_MarkFunc *mark_func)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);
    uint32_t i;

    for(i = 0; i < p->u.array.count; i++) {
        JS_MarkValue(rt, p->u.array.u.values[i], mark_func);
    }
}

static void js_object_data_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);
    JS_FreeValueRT(rt, p->u.object_data);
    p->u.object_data = JS_UNDEFINED;
}

static void js_object_data_mark(JSRuntime *rt, JSValueConst val,
                                JS_MarkFunc *mark_func)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);
    JS_MarkValue(rt, p->u.object_data, mark_func);
}

static void js_c_function_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);

    if (p->u.cfunc.realm)
        JS_FreeContext(p->u.cfunc.realm);
}

static void js_c_function_mark(JSRuntime *rt, JSValueConst val,
                               JS_MarkFunc *mark_func)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);

    if (p->u.cfunc.realm)
        mark_func(rt, &p->u.cfunc.realm->header);
}

static void js_bytecode_function_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSObject *p1, *p = JS_VALUE_GET_OBJ(val);
    JSFunctionBytecode *b;
    JSVarRef **var_refs;
    int i;

    p1 = p->u.func.home_object;
    if (p1) {
        JS_FreeValueRT(rt, JS_MKPTR(JS_TAG_OBJECT, p1));
    }
    b = p->u.func.function_bytecode;
    if (b) {
        var_refs = p->u.func.var_refs;
        if (var_refs) {
            for(i = 0; i < b->closure_var_count; i++)
                free_var_ref(rt, var_refs[i]);
            js_free_rt(rt, var_refs);
        }
        JS_FreeValueRT(rt, JS_MKPTR(JS_TAG_FUNCTION_BYTECODE, b));
    }
}

static void js_bytecode_function_mark(JSRuntime *rt, JSValueConst val,
                                      JS_MarkFunc *mark_func)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);
    JSVarRef **var_refs = p->u.func.var_refs;
    JSFunctionBytecode *b = p->u.func.function_bytecode;
    int i;

    if (p->u.func.home_object) {
        JS_MarkValue(rt, JS_MKPTR(JS_TAG_OBJECT, p->u.func.home_object),
                     mark_func);
    }
    if (b) {
        if (var_refs) {
            for(i = 0; i < b->closure_var_count; i++) {
                JSVarRef *var_ref = var_refs[i];
                if (var_ref && var_ref->is_detached) {
                    mark_func(rt, &var_ref->header);
                }
            }
        }
        /* must mark the function bytecode because template objects may be
           part of a cycle */
        JS_MarkValue(rt, JS_MKPTR(JS_TAG_FUNCTION_BYTECODE, b), mark_func);
    }
}

static void js_bound_function_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);
    JSBoundFunction *bf = p->u.bound_function;
    int i;

    JS_FreeValueRT(rt, bf->func_obj);
    JS_FreeValueRT(rt, bf->this_val);
    for(i = 0; i < bf->argc; i++) {
        JS_FreeValueRT(rt, bf->argv[i]);
    }
    js_free_rt(rt, bf);
}

static void js_bound_function_mark(JSRuntime *rt, JSValueConst val,
                                JS_MarkFunc *mark_func)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);
    JSBoundFunction *bf = p->u.bound_function;
    int i;

    JS_MarkValue(rt, bf->func_obj, mark_func);
    JS_MarkValue(rt, bf->this_val, mark_func);
    for(i = 0; i < bf->argc; i++)
        JS_MarkValue(rt, bf->argv[i], mark_func);
}

static void js_for_in_iterator_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);
    JSForInIterator *it = p->u.for_in_iterator;
    JS_FreeValueRT(rt, it->obj);
    js_free_rt(rt, it);
}

static void js_for_in_iterator_mark(JSRuntime *rt, JSValueConst val,
                                JS_MarkFunc *mark_func)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);
    JSForInIterator *it = p->u.for_in_iterator;
    JS_MarkValue(rt, it->obj, mark_func);
}

static void free_object(JSRuntime *rt, JSObject *p)
{
    int i;
    JSClassFinalizer *finalizer;
    JSShape *sh;
    JSShapeProperty *pr;

    p->free_mark = 1; /* used to tell the object is invalid when
                         freeing cycles */
    /* free all the fields */
    sh = p->shape;
    pr = get_shape_prop(sh);
    for(i = 0; i < sh->prop_count; i++) {
        free_property(rt, &p->prop[i], pr->flags);
        pr++;
    }
    js_free_rt(rt, p->prop);
    /* as an optimization we destroy the shape immediately without
       putting it in gc_zero_ref_count_list */
    js_free_shape(rt, sh);

    /* fail safe */
    p->shape = NULL;
    p->prop = NULL;

    if (unlikely(p->first_weak_ref)) {
        reset_weak_ref(rt, &p->first_weak_ref);
    }

    finalizer = rt->class_array[p->class_id].finalizer;
    if (finalizer)
        (*finalizer)(rt, JS_MKPTR(JS_TAG_OBJECT, p));

    /* fail safe */
    p->class_id = 0;
    p->u.opaque = NULL;
    p->u.func.var_refs = NULL;
    p->u.func.home_object = NULL;

    remove_gc_object(&p->header);
    if (rt->gc_phase == JS_GC_PHASE_REMOVE_CYCLES && JS_REF_COUNT(p) != 0) {
        list_add_tail(&p->header.link, &rt->gc_zero_ref_count_list);
    } else {
        js_free_rt(rt, p);
    }
}

static void free_gc_object(JSRuntime *rt, JSGCObjectHeader *gp)
{
    switch(JS_GC_TYPE(gp)) {
    case JS_GC_OBJ_TYPE_JS_OBJECT:
        free_object(rt, (JSObject *)gp);
        break;
    case JS_GC_OBJ_TYPE_FUNCTION_BYTECODE:
        free_function_bytecode(rt, (JSFunctionBytecode *)gp);
        break;
    default:
        abort();
    }
}

static void free_zero_refcount(JSRuntime *rt)
{
    struct list_head *el;
    JSGCObjectHeader *p;

    rt->gc_phase = JS_GC_PHASE_DECREF;
    for(;;) {
        el = rt->gc_zero_ref_count_list.next;
        if (el == &rt->gc_zero_ref_count_list)
            break;
        p = list_entry(el, JSGCObjectHeader, link);
        assert(JS_REF_COUNT(p) == 0);
        free_gc_object(rt, p);
    }
    rt->gc_phase = JS_GC_PHASE_NONE;
}

/* called with the ref_count of 'v' reaches zero. */
static void js_free_value_rt(JSRuntime *rt, JSValue v)
{
    uint32_t tag = JS_VALUE_GET_TAG(v);

#ifdef ENABLE_DUMPS // JS_DUMP_FREE
    if (check_dump_flag(rt, JS_DUMP_FREE)) {
        /* Prevent invalid object access during GC */
        if ((rt->gc_phase != JS_GC_PHASE_REMOVE_CYCLES)
        ||  (tag != JS_TAG_OBJECT && tag != JS_TAG_FUNCTION_BYTECODE)) {
            printf("Freeing ");
            if (tag == JS_TAG_OBJECT) {
                JS_DumpObject(rt, JS_VALUE_GET_OBJ(v));
            } else {
                JS_DumpValue(rt, v);
                printf("\n");
            }
        }
    }
#endif

    switch(tag) {
    case JS_TAG_STRING:
        js_free_string0(rt, JS_VALUE_GET_STRING(v));
        break;
    case JS_TAG_STRING_ROPE:
        {
            JSStringRope *p = JS_VALUE_GET_STRING_ROPE(v);
            JS_FreeValueRT(rt, p->left);
            JS_FreeValueRT(rt, p->right);
            js_free_rt(rt, p);
        }
        break;
    case JS_TAG_OBJECT:
    case JS_TAG_FUNCTION_BYTECODE:
        {
            JSGCObjectHeader *p = JS_VALUE_GET_PTR(v);
            if (rt->gc_phase != JS_GC_PHASE_REMOVE_CYCLES) {
                list_del(&p->link);
                list_add(&p->link, &rt->gc_zero_ref_count_list);
                if (rt->gc_phase == JS_GC_PHASE_NONE) {
                    free_zero_refcount(rt);
                }
            }
        }
        break;
    case JS_TAG_MODULE:
        abort(); /* never freed here */
        break;
    case JS_TAG_BIG_INT:
        {
            JSBigInt *p = JS_VALUE_GET_PTR(v);
            js_free_rt(rt, p);
        }
        break;
    case JS_TAG_SYMBOL:
        {
            JSAtomStruct *p = JS_VALUE_GET_PTR(v);
            JS_FreeAtomStruct(rt, p);
        }
        break;
    default:
        printf("js_free_value_rt: unknown tag=%d\n", tag);
        abort();
    }
}

void JS_FreeValueRT(JSRuntime *rt, JSValue v)
{
    if (JS_VALUE_HAS_REF_COUNT(v)) {
        void *p = JS_VALUE_GET_PTR(v);
        if (--JS_REF_COUNT(p) <= 0) {
            js_free_value_rt(rt, v);
        }
    }
}

void JS_FreeValue(JSContext *ctx, JSValue v)
{
    JS_FreeValueRT(ctx->rt, v);
}

