/* Engine domain source: objects/shapes_objects.inc -> object_core.
 * Ownership: objects subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static __maybe_unused void JS_DumpShapes(JSRuntime *rt)
{
    int i;
    JSShape *sh;
    struct list_head *el;
    JSObject *p;
    JSGCObjectHeader *gp;

    printf("JSShapes: {\n");
    printf("%5s %4s %14s %5s %5s %s\n", "SLOT", "REFS", "PROTO", "SIZE", "COUNT", "PROPS");
    for(i = 0; i < rt->shape_hash_size; i++) {
        for(sh = rt->shape_hash[i]; sh != NULL; sh = sh->shape_hash_next) {
            JS_DumpShape(rt, i, sh);
            assert(sh->is_hashed);
        }
    }
    /* dump non-hashed shapes */
    list_for_each(el, &rt->gc_obj_list) {
        gp = list_entry(el, JSGCObjectHeader, link);
        if (JS_GC_TYPE(gp) == JS_GC_OBJ_TYPE_JS_OBJECT) {
            p = (JSObject *)gp;
            if (!p->shape->is_hashed) {
                JS_DumpShape(rt, -1, p->shape);
            }
        }
    }
    printf("}\n");
}

/* 'props[]' is used to initialized the object properties. The number
   of elements depends on the shape. */
static JSValue JS_NewObjectFromShape(JSContext *ctx, JSShape *sh, JSClassID class_id,
                                     JSProperty *props)
{
    JSObject *p;
    int i;

    js_trigger_gc(ctx->rt, sizeof(JSObject));
    p = js_malloc(ctx, sizeof(JSObject));
    if (unlikely(!p))
        goto fail;
    p->class_id = class_id;
    p->extensible = true;
    p->free_mark = 0;
    p->is_exotic = 0;
    p->fast_array = 0;
    p->is_constructor = 0;
    p->is_uncatchable_error = 0;
    p->tmp_mark = 0;
    p->is_HTMLDDA = 0;
    p->is_prototype = 0;
    p->first_weak_ref = NULL;
    p->u.opaque = NULL;
    p->shape = sh;
    p->prop = js_malloc(ctx, sizeof(JSProperty) * sh->prop_size);
    if (unlikely(!p->prop)) {
        js_free(ctx, p);
    fail:
        if (props) {
            JSShapeProperty *prs = get_shape_prop(sh);
            for(i = 0; i < sh->prop_count; i++) {
                free_property(ctx->rt, &props[i], prs->flags);
                prs++;
            }
        }
        js_free_shape(ctx->rt, sh);
        return JS_EXCEPTION;
    }

    switch(class_id) {
    case JS_CLASS_OBJECT:
        break;
    case JS_CLASS_ARRAY:
        {
            JSProperty *pr;
            p->is_exotic = 1;
            p->fast_array = 1;
            p->u.array.u.values = NULL;
            p->u.array.count = 0;
            p->u.array.u1.size = 0;
            if (!props) {
                /* XXX: remove */
                /* the length property is always the first one */
                if (likely(sh == ctx->array_shape)) {
                    pr = &p->prop[0];
                } else {
                    /* only used for the first array */
                    /* cannot fail */
                    pr = add_property(ctx, p, JS_ATOM_length,
                                      JS_PROP_WRITABLE | JS_PROP_LENGTH);
                }
                pr->u.value = js_int32(0);
            }
        }
        break;
    case JS_CLASS_C_FUNCTION:
        p->prop[0].u.value = JS_UNDEFINED;
        break;
    case JS_CLASS_ARGUMENTS:
    case JS_CLASS_MAPPED_ARGUMENTS:
    case JS_CLASS_UINT8C_ARRAY:
    case JS_CLASS_INT8_ARRAY:
    case JS_CLASS_UINT8_ARRAY:
    case JS_CLASS_INT16_ARRAY:
    case JS_CLASS_UINT16_ARRAY:
    case JS_CLASS_INT32_ARRAY:
    case JS_CLASS_UINT32_ARRAY:
    case JS_CLASS_BIG_INT64_ARRAY:
    case JS_CLASS_BIG_UINT64_ARRAY:
    case JS_CLASS_FLOAT16_ARRAY:
    case JS_CLASS_FLOAT32_ARRAY:
    case JS_CLASS_FLOAT64_ARRAY:
        p->is_exotic = 1;
        p->fast_array = 1;
        p->u.array.u.ptr = NULL;
        p->u.array.count = 0;
        break;
    case JS_CLASS_DATAVIEW:
        p->u.array.u.ptr = NULL;
        p->u.array.count = 0;
        break;
    case JS_CLASS_ERROR:
    case JS_CLASS_NUMBER:
    case JS_CLASS_STRING:
    case JS_CLASS_BOOLEAN:
    case JS_CLASS_SYMBOL:
    case JS_CLASS_DATE:
    case JS_CLASS_BIG_INT:
        p->u.object_data = JS_UNDEFINED;
        goto set_exotic;
    case JS_CLASS_REGEXP:
        p->u.regexp.pattern = NULL;
        p->u.regexp.bytecode = NULL;
        goto set_exotic;
    default:
    set_exotic:
        if (ctx->rt->class_array[class_id].exotic) {
            p->is_exotic = 1;
        }
        break;
    }
    JS_REF_COUNT(p) = 1;
    add_gc_object(ctx->rt, &p->header, JS_GC_OBJ_TYPE_JS_OBJECT);
    if (props) {
        for(i = 0; i < sh->prop_count; i++)
            p->prop[i] = props[i];
    }
    return JS_MKPTR(JS_TAG_OBJECT, p);
}

/* WARNING: proto must be an object or JS_NULL */
JSValue JS_NewObjectProtoClass(JSContext *ctx, JSValueConst proto_val,
                               JSClassID class_id)
{
    JSShape *sh;
    JSObject *proto;

    proto = object_or_null(proto_val);
    sh = find_hashed_shape_proto(ctx->rt, proto);
    if (likely(sh)) {
        sh = js_dup_shape(sh);
    } else {
        sh = js_new_shape(ctx, proto);
        if (!sh)
            return JS_EXCEPTION;
    }
    return JS_NewObjectFromShape(ctx, sh, class_id, NULL);
}

/* WARNING: the shape is not hashed. It is used for objects where
   factorizing the shape is not relevant (prototypes, constructors) */
static JSValue JS_NewObjectProtoClassAlloc(JSContext *ctx, JSValueConst proto_val,
                                           JSClassID class_id, int n_alloc_props)
{
    JSShape *sh;
    JSObject *proto;
    int hash_size, hash_bits;

    if (n_alloc_props <= JS_PROP_INITIAL_SIZE) {
        n_alloc_props = JS_PROP_INITIAL_SIZE;
        hash_size = JS_PROP_INITIAL_HASH_SIZE;
    } else {
        hash_bits = 32 - clz32(n_alloc_props - 1); /* ceil(log2(radix)) */
        hash_size = 1 << hash_bits;
    }
    proto = object_or_null(proto_val);
    sh = js_new_shape_nohash(ctx, proto, hash_size, n_alloc_props);
    if (!sh)
        return JS_EXCEPTION;
    return JS_NewObjectFromShape(ctx, sh, class_id, NULL);
}

static int JS_SetObjectData(JSContext *ctx, JSValueConst obj, JSValue val)
{
    JSObject *p;

    if (JS_VALUE_GET_TAG(obj) == JS_TAG_OBJECT) {
        p = JS_VALUE_GET_OBJ(obj);
        switch(p->class_id) {
        case JS_CLASS_NUMBER:
        case JS_CLASS_STRING:
        case JS_CLASS_BOOLEAN:
        case JS_CLASS_SYMBOL:
        case JS_CLASS_DATE:
        case JS_CLASS_BIG_INT:
            JS_FreeValue(ctx, p->u.object_data);
            p->u.object_data = val; /* for JS_CLASS_STRING, 'val' must
                                       be JS_TAG_STRING (and not a rope) */
            return 0;
        }
    }
    JS_FreeValue(ctx, val);
    if (!JS_IsException(obj))
        JS_ThrowTypeError(ctx, "invalid object type");
    return -1;
}

JSValue JS_NewObjectClass(JSContext *ctx, JSClassID class_id)
{
    return JS_NewObjectProtoClass(ctx, ctx->class_proto[class_id], class_id);
}

JSValue JS_NewObjectProto(JSContext *ctx, JSValueConst proto)
{
    return JS_NewObjectProtoClass(ctx, proto, JS_CLASS_OBJECT);
}

JSValue JS_NewObjectFrom(JSContext *ctx, int count, const JSAtom *props,
                         const JSValue *values)
{
    JSShapeProperty *pr;
    uint32_t *hash;
    JSRuntime *rt;
    JSObject *p;
    JSShape *sh;
    JSValue obj;
    JSAtom atom;
    intptr_t h;
    int i;

    rt = ctx->rt;
    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    if (count > 0) {
        p = JS_VALUE_GET_OBJ(obj);
        sh = p->shape;
        assert(sh->is_hashed);
        assert(JS_REF_COUNT(sh) == 1);
        js_shape_hash_unlink(rt, sh);
        if (resize_properties(ctx, &sh, p, count)) {
            js_shape_hash_link(rt, sh);
            JS_FreeValue(ctx, obj);
            return JS_EXCEPTION;
        }
        p->shape = sh;
        for (i = 0; i < count; i++) {
            atom = props[i];
            pr = &get_shape_prop(sh)[i];
            sh->hash = shape_hash(shape_hash(sh->hash, atom), JS_PROP_C_W_E);
            h = atom & sh->prop_hash_mask;
            hash = &prop_hash_end(sh)[-h - 1];
            pr->hash_next = *hash;
            *hash = i + 1;
            pr->atom = JS_DupAtom(ctx, atom);
            pr->flags = JS_PROP_C_W_E;
            p->prop[i].u.value = values[i];
        }
        js_shape_hash_link(rt, sh);
        sh->prop_count = count;
    }
    return obj;
}

JSValue JS_NewObjectFromStr(JSContext *ctx, int count, const char **props,
                            const JSValue *values)
{
    JSAtom atoms_s[16], *atoms = atoms_s;
    JSValue ret;
    int i;

    i = 0;
    ret = JS_EXCEPTION;
    if (count < 1)
        goto out;
    if (count > (int)countof(atoms_s)) {
        atoms = js_malloc(ctx, count * sizeof(*atoms));
        if (!atoms)
            return JS_EXCEPTION;
    }
    for (i = 0; i < count; i++) {
        atoms[i] = JS_NewAtom(ctx, props[i]);
        if (atoms[i] == JS_ATOM_NULL)
            goto out;
    }
    ret = JS_NewObjectFrom(ctx, count, atoms, values);
out:
    while (i-- > 0)
        JS_FreeAtom(ctx, atoms[i]);
    if (atoms != atoms_s)
        js_free(ctx, atoms);
    return ret;
}

JSValue JS_NewArray(JSContext *ctx)
{
    return JS_NewObjectFromShape(ctx, js_dup_shape(ctx->array_shape),
                                 JS_CLASS_ARRAY, NULL);
}

// note: takes ownership of |values|, unlike js_create_array
JSValue JS_NewArrayFrom(JSContext *ctx, int count, const JSValue *values)
{
    JSObject *p;
    JSValue obj;
    int i;

    obj = JS_NewArray(ctx);
    if (JS_IsException(obj))
        goto exception;
    if (count > 0) {
        p = JS_VALUE_GET_OBJ(obj);
        if (expand_fast_array(ctx, p, count)) {
            JS_FreeValue(ctx, obj);
            goto exception;
        }
        p->u.array.count = count;
        p->prop[0].u.value = js_int32(count);
        memcpy(p->u.array.u.values, values, count * sizeof(*values));
    }
    return obj;
exception:
    for (i = 0; i < count; i++)
        JS_FreeValue(ctx, values[i]);
    return JS_EXCEPTION;
}

JSValue JS_NewObject(JSContext *ctx)
{
    /* inline JS_NewObjectClass(ctx, JS_CLASS_OBJECT); */
    return JS_NewObjectProtoClass(ctx, ctx->class_proto[JS_CLASS_OBJECT], JS_CLASS_OBJECT);
}

static void js_function_set_properties(JSContext *ctx, JSValue func_obj,
                                       JSAtom name, int len)
{
    /* ES6 feature non compatible with ES5.1: length is configurable */
    JS_DefinePropertyValue(ctx, func_obj, JS_ATOM_length, js_int32(len),
                           JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValue(ctx, func_obj, JS_ATOM_name,
                           JS_AtomToString(ctx, name), JS_PROP_CONFIGURABLE);
}

static bool js_class_has_bytecode(JSClassID class_id)
{
    return (class_id == JS_CLASS_BYTECODE_FUNCTION ||
            class_id == JS_CLASS_GENERATOR_FUNCTION ||
            class_id == JS_CLASS_ASYNC_FUNCTION ||
            class_id == JS_CLASS_ASYNC_GENERATOR_FUNCTION);
}

/* return NULL without exception if not a function or no bytecode */
static JSFunctionBytecode *JS_GetFunctionBytecode(JSValueConst val)
{
    JSObject *p;
    if (JS_VALUE_GET_TAG(val) != JS_TAG_OBJECT)
        return NULL;
    p = JS_VALUE_GET_OBJ(val);
    if (!js_class_has_bytecode(p->class_id))
        return NULL;
    return p->u.func.function_bytecode;
}

static void js_method_set_home_object(JSContext *ctx, JSValue func_obj,
                                      JSValue home_obj)
{
    JSObject *p, *p1;
    JSFunctionBytecode *b;

    if (JS_VALUE_GET_TAG(func_obj) != JS_TAG_OBJECT)
        return;
    p = JS_VALUE_GET_OBJ(func_obj);
    if (!js_class_has_bytecode(p->class_id))
        return;
    b = p->u.func.function_bytecode;
    if (b->need_home_object) {
        p1 = p->u.func.home_object;
        if (p1) {
            JS_FreeValue(ctx, JS_MKPTR(JS_TAG_OBJECT, p1));
        }
        if (JS_VALUE_GET_TAG(home_obj) == JS_TAG_OBJECT)
            p1 = JS_VALUE_GET_OBJ(js_dup(home_obj));
        else
            p1 = NULL;
        p->u.func.home_object = p1;
    }
}

static JSValue js_get_function_name(JSContext *ctx, JSAtom name)
{
    JSValue name_str;

    name_str = JS_AtomToString(ctx, name);
    if (JS_AtomSymbolHasDescription(ctx, name)) {
        name_str = JS_ConcatString3(ctx, "[", name_str, "]");
    }
    return name_str;
}

/* Modify the name of a method according to the atom and
   'flags'. 'flags' is a bitmask of JS_PROP_HAS_GET and
   JS_PROP_HAS_SET. Also set the home object of the method.
   Return < 0 if exception. */
static int js_method_set_properties(JSContext *ctx, JSValue func_obj,
                                    JSAtom name, int flags, JSValue home_obj)
{
    JSValue name_str;

    name_str = js_get_function_name(ctx, name);
    if (flags & JS_PROP_HAS_GET) {
        name_str = JS_ConcatString3(ctx, "get ", name_str, "");
    } else if (flags & JS_PROP_HAS_SET) {
        name_str = JS_ConcatString3(ctx, "set ", name_str, "");
    }
    if (JS_IsException(name_str))
        return -1;
    if (JS_DefinePropertyValue(ctx, func_obj, JS_ATOM_name, name_str,
                               JS_PROP_CONFIGURABLE) < 0)
        return -1;
    js_method_set_home_object(ctx, func_obj, home_obj);
    return 0;
}

/* Note: at least 'length' arguments will be readable in 'argv' */
/* `name` may be NULL, pure ASCII or UTF-8 encoded */
JSValue JS_NewCFunction3(JSContext *ctx, JSCFunction *func,
                         const char *name,
                         int length, JSCFunctionEnum cproto, int magic,
                         JSValueConst proto_val, int n_fields)
{
    JSValue func_obj;
    JSObject *p;
    JSAtom name_atom;

    if (n_fields > 0) {
        func_obj = JS_NewObjectProtoClassAlloc(ctx, proto_val, JS_CLASS_C_FUNCTION, n_fields);
    } else {
        func_obj = JS_NewObjectProtoClass(ctx, proto_val, JS_CLASS_C_FUNCTION);
    }
    if (JS_IsException(func_obj))
        return func_obj;
    p = JS_VALUE_GET_OBJ(func_obj);
    p->u.cfunc.realm = JS_DupContext(ctx);
    p->u.cfunc.c_function.generic = func;
    p->u.cfunc.length = length;
    p->u.cfunc.cproto = cproto;
    p->u.cfunc.magic = magic;
    p->is_constructor = (cproto == JS_CFUNC_constructor ||
                         cproto == JS_CFUNC_constructor_magic ||
                         cproto == JS_CFUNC_constructor_or_func ||
                         cproto == JS_CFUNC_constructor_or_func_magic);
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

/* Note: at least 'length' arguments will be readable in 'argv' */
JSValue JS_NewCFunction2(JSContext *ctx, JSCFunction *func,
                         const char *name,
                         int length, JSCFunctionEnum cproto, int magic)
{
    return JS_NewCFunction3(ctx, func, name, length, cproto, magic,
                            ctx->function_proto, 0);
}

typedef struct JSCFunctionDataRecord {
    JSCFunctionData *func;
    uint8_t length;
    uint8_t data_len;
    uint16_t magic;
    JSValue data[];
} JSCFunctionDataRecord;

static void js_c_function_data_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSCFunctionDataRecord *s = JS_GetOpaque(val, JS_CLASS_C_FUNCTION_DATA);
    int i;

    if (s) {
        for(i = 0; i < s->data_len; i++) {
            JS_FreeValueRT(rt, s->data[i]);
        }
        js_free_rt(rt, s);
    }
}

static void js_c_function_data_mark(JSRuntime *rt, JSValueConst val,
                                    JS_MarkFunc *mark_func)
{
    JSCFunctionDataRecord *s = JS_GetOpaque(val, JS_CLASS_C_FUNCTION_DATA);
    int i;

    if (s) {
        for(i = 0; i < s->data_len; i++) {
            JS_MarkValue(rt, s->data[i], mark_func);
        }
    }
}

static JSValue js_call_c_function_data(JSContext *ctx, JSValueConst func_obj,
                                       JSValueConst this_val,
                                       int argc, JSValueConst *argv, int flags)
{
    JSRuntime *rt = ctx->rt;
    JSStackFrame sf_s, *sf = &sf_s, *prev_sf;
    JSCFunctionDataRecord *s;
    JSValueConst *arg_buf;
    JSValue ret;
    size_t stack_size;
    int arg_count;
    int i;

    s = JS_GetOpaque(func_obj, JS_CLASS_C_FUNCTION_DATA);
    if (!s)
        return JS_EXCEPTION; // can't really happen
    arg_buf = argv;
    arg_count = s->length;
    if (unlikely(argc < arg_count)) {
        stack_size = arg_count * sizeof(arg_buf[0]);
        if (js_check_stack_overflow(rt, stack_size))
            return JS_ThrowStackOverflow(ctx);
        arg_buf = alloca(stack_size);
        for(i = 0; i < argc; i++)
            arg_buf[i] = argv[i];
        for(i = argc; i < arg_count; i++)
            arg_buf[i] = JS_UNDEFINED;
    }
    prev_sf = rt->current_stack_frame;
    sf->prev_frame = prev_sf;
    rt->current_stack_frame = sf;
    // TODO(bnoordhuis) switch realms like js_call_c_function does
    sf->is_strict_mode = false;
    sf->cur_func = unsafe_unconst(func_obj);
    sf->arg_count = argc;
    ret = s->func(ctx, this_val, argc, arg_buf, s->magic, vc(s->data));
    rt->current_stack_frame = sf->prev_frame;
    return ret;
}

