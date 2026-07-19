/* Engine domain source: builtins/bigint_typedarray.inc -> weakref_dom_builtin.
 * Ownership: builtins subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

int JS_AddIntrinsicWeakRef(JSContext *ctx)
{
    JSRuntime *rt = ctx->rt;
    JSValue obj;

    /* WeakRef */
    if (!JS_IsRegisteredClass(rt, JS_CLASS_WEAK_REF)) {
        if (init_class_range(rt, js_weakref_class_def, JS_CLASS_WEAK_REF,
                             countof(js_weakref_class_def)))
            return -1;
    }
    obj = JS_NewCConstructor(ctx, JS_CLASS_WEAK_REF, "WeakRef",
                             js_weakref_constructor, 1, JS_CFUNC_constructor_or_func, 0,
                             JS_UNDEFINED,
                             NULL, 0,
                             js_weakref_proto_funcs, countof(js_weakref_proto_funcs),
                             0);
    if (JS_IsException(obj))
        return -1;
    JS_FreeValue(ctx, obj);

    /* FinalizationRegistry */
    if (!JS_IsRegisteredClass(rt, JS_CLASS_FINALIZATION_REGISTRY)) {
        if (init_class_range(rt, js_finrec_class_def, JS_CLASS_FINALIZATION_REGISTRY,
                             countof(js_finrec_class_def)))
            return -1;
    }
    obj = JS_NewCConstructor(ctx, JS_CLASS_FINALIZATION_REGISTRY, "FinalizationRegistry",
                             js_finrec_constructor, 1, JS_CFUNC_constructor_or_func, 0,
                             JS_UNDEFINED,
                             NULL, 0,
                             js_finrec_proto_funcs, countof(js_finrec_proto_funcs),
                             0);
    if (JS_IsException(obj))
        return -1;
    JS_FreeValue(ctx, obj);
    return 0;
}

static void reset_weak_ref(JSRuntime *rt, JSWeakRefRecord **first_weak_ref)
{
    JSWeakRefRecord *wr, *wr_next;
    JSWeakRefData *wrd;
    JSMapRecord *mr;
    JSMapState *s;
    JSFinRecEntry *fre;

    /* first pass to remove the records from the WeakMap/WeakSet
       lists */
    for(wr = *first_weak_ref; wr != NULL; wr = wr->next_weak_ref) {
        switch(wr->kind) {
        case JS_WEAK_REF_KIND_MAP:
            mr = wr->u.map_record;
            s = mr->map;
            assert(s->is_weak);
            assert(!mr->empty); /* no iterator on WeakMap/WeakSet */
            list_del(&mr->hash_link);
            list_del(&mr->link);
            s->record_count--;
            break;
        case JS_WEAK_REF_KIND_WEAK_REF:
            wrd = wr->u.weak_ref_data;
            wrd->target = JS_UNDEFINED;
            break;
        case JS_WEAK_REF_KIND_FINALIZATION_REGISTRY_ENTRY:
            fre = wr->u.fin_rec_entry;
            list_del(&fre->link);
            break;
        default:
            abort();
        }
    }

    /* second pass to free the values to avoid modifying the weak
       reference list while traversing it. */
    for(wr = *first_weak_ref; wr != NULL; wr = wr_next) {
        wr_next = wr->next_weak_ref;
        switch(wr->kind) {
        case JS_WEAK_REF_KIND_MAP:
            mr = wr->u.map_record;
            JS_FreeValueRT(rt, mr->value);
            js_free_rt(rt, mr);
            break;
        case JS_WEAK_REF_KIND_WEAK_REF:
            wrd = wr->u.weak_ref_data;
            JS_SetOpaqueInternal(wrd->obj, &js_weakref_sentinel);
            js_free_rt(rt, wrd);
            break;
        case JS_WEAK_REF_KIND_FINALIZATION_REGISTRY_ENTRY: {
            fre = wr->u.fin_rec_entry;
            /**
             * During the GC sweep phase the held object might be
             * collected first (free_mark set). Also skip if the
             * callback or held value are part of a cycle being
             * collected (header.mark is set for objects on
             * tmp_obj_list during gc_free_cycles).
             */
            bool enqueue = !rt->in_free;
            if (enqueue && JS_IsObject(fre->held_val)) {
                JSObject *p = JS_VALUE_GET_OBJ(fre->held_val);
                if (p->free_mark || JS_GC_MARK(p))
                    enqueue = false;
            }
            if (enqueue && JS_IsObject(fre->cb)) {
                JSObject *p = JS_VALUE_GET_OBJ(fre->cb);
                if (p->free_mark || JS_GC_MARK(p))
                    enqueue = false;
            }
            if (enqueue) {
                JSValueConst args[2];
                args[0] = fre->cb;
                args[1] = fre->held_val;
                JS_EnqueueJob(fre->ctx, js_finrec_job, 2, args);
            }
            js_finrec_free(rt, fre);
            break;
        }
        default:
            abort();
        }
        js_free_rt(rt, wr);
    }

    *first_weak_ref = NULL; /* fail safe */
}

static bool is_valid_weakref_target(JSValueConst val)
{
    switch (JS_VALUE_GET_TAG(val)) {
    case JS_TAG_OBJECT:
        break;
    case JS_TAG_SYMBOL: {
        // Per spec: prohibit symbols registered with Symbol.for()
        JSAtomStruct *p = JS_VALUE_GET_PTR(val);
        if (p->atom_type != JS_ATOM_TYPE_GLOBAL_SYMBOL)
            break;
        // fallthru
    }
    default:
        return false;
    }

    return true;
}

static void insert_weakref_record(JSValueConst target,
                                  struct JSWeakRefRecord *wr)
{
    JSWeakRefRecord **pwr = get_first_weak_ref(target);
    /* Add the weak reference */
    wr->next_weak_ref = *pwr;
    *pwr = wr;
}

/* CallSite */

static void js_callsite_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSCallSiteData *csd = JS_GetOpaque(val, JS_CLASS_CALL_SITE);
    if (csd) {
        JS_FreeValueRT(rt, csd->filename);
        JS_FreeValueRT(rt, csd->func);
        JS_FreeValueRT(rt, csd->func_name);
        js_free_rt(rt, csd);
    }
}

static void js_callsite_mark(JSRuntime *rt, JSValueConst val,
                             JS_MarkFunc *mark_func)
{
    JSCallSiteData *csd = JS_GetOpaque(val, JS_CLASS_CALL_SITE);
    if (csd) {
        JS_MarkValue(rt, csd->filename, mark_func);
        JS_MarkValue(rt, csd->func, mark_func);
        JS_MarkValue(rt, csd->func_name, mark_func);
    }
}

static JSValue js_new_callsite(JSContext *ctx, JSCallSiteData *csd) {
    JSValue obj = js_create_from_ctor(ctx, JS_UNDEFINED, JS_CLASS_CALL_SITE);
    if (JS_IsException(obj))
        return JS_EXCEPTION;

    JSCallSiteData *csd1 = js_malloc(ctx, sizeof(*csd));
    if (!csd1) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }

    memcpy(csd1, csd, sizeof(*csd));

    JS_SetOpaqueInternal(obj, csd1);

    return obj;
}

static void js_new_callsite_data(JSContext *ctx, JSCallSiteData *csd, JSStackFrame *sf)
{
    const char *func_name_str;
    JSObject *p;

    csd->func = js_dup(sf->cur_func);
    /* func_name_str is UTF-8 encoded if needed */
    func_name_str = get_func_name(ctx, sf->cur_func);
    if (!func_name_str || func_name_str[0] == '\0')
        csd->func_name = JS_NULL;
    else
        csd->func_name = JS_NewString(ctx, func_name_str);
    JS_FreeCString(ctx, func_name_str);
    if (JS_IsException(csd->func_name))
        csd->func_name = JS_NULL;

    p = JS_VALUE_GET_OBJ(sf->cur_func);
    if (js_class_has_bytecode(p->class_id)) {
        JSFunctionBytecode *b = p->u.func.function_bytecode;
        int line_num1, col_num1;
        line_num1 = find_line_num(ctx, b,
                                  sf->cur_pc - b->byte_code_buf - 1,
                                  &col_num1);
        csd->native = false;
        csd->line_num = line_num1;
        csd->col_num = col_num1;
        csd->filename = JS_AtomToString(ctx, b->filename);
        if (JS_IsException(csd->filename)) {
            csd->filename = JS_NULL;
            JS_FreeValue(ctx, JS_GetException(ctx)); // Clear exception.
        }
    } else {
        csd->native = true;
        csd->line_num = -1;
        csd->col_num = -1;
        csd->filename = JS_NULL;
    }
}

static void js_new_callsite_data2(JSContext *ctx, JSCallSiteData *csd, const char *filename, int line_num, int col_num)
{
    csd->func = JS_NULL;
    csd->func_name = JS_NULL;
    csd->native = false;
    csd->line_num = line_num;
    csd->col_num = col_num;
    /* filename is UTF-8 encoded if needed (original argument to __JS_EvalInternal()) */
    csd->filename = JS_NewString(ctx, filename);
    if (JS_IsException(csd->filename)) {
        csd->filename = JS_NULL;
        JS_FreeValue(ctx, JS_GetException(ctx)); // Clear exception.
    }
}

static JSValue js_callsite_getfield(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
    JSCallSiteData *csd = JS_GetOpaque2(ctx, this_val, JS_CLASS_CALL_SITE);
    if (!csd)
        return JS_EXCEPTION;
    JSValue *field = (void *)((char *)csd + magic);
    return js_dup(*field);
}

static JSValue js_callsite_isnative(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    JSCallSiteData *csd = JS_GetOpaque2(ctx, this_val, JS_CLASS_CALL_SITE);
    if (!csd)
        return JS_EXCEPTION;
    return js_bool(csd->native);
}

static JSValue js_callsite_getnumber(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
    JSCallSiteData *csd = JS_GetOpaque2(ctx, this_val, JS_CLASS_CALL_SITE);
    if (!csd)
        return JS_EXCEPTION;
    int *field = (void *)((char *)csd + magic);
    return js_int32(*field);
}

static const JSCFunctionListEntry js_callsite_proto_funcs[] = {
    JS_CFUNC_DEF("isNative", 0, js_callsite_isnative),
    JS_CFUNC_MAGIC_DEF("getFileName", 0, js_callsite_getfield, offsetof(JSCallSiteData, filename)),
    JS_CFUNC_MAGIC_DEF("getFunction", 0, js_callsite_getfield, offsetof(JSCallSiteData, func)),
    JS_CFUNC_MAGIC_DEF("getFunctionName", 0, js_callsite_getfield, offsetof(JSCallSiteData, func_name)),
    JS_CFUNC_MAGIC_DEF("getColumnNumber", 0, js_callsite_getnumber, offsetof(JSCallSiteData, col_num)),
    JS_CFUNC_MAGIC_DEF("getLineNumber", 0, js_callsite_getnumber, offsetof(JSCallSiteData, line_num)),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "CallSite", JS_PROP_CONFIGURABLE ),
};

static const JSClassShortDef js_callsite_class_def[] = {
    { JS_ATOM_CallSite, js_callsite_finalizer, js_callsite_mark }, /* JS_CLASS_CALL_SITE */
};

static int _JS_AddIntrinsicCallSite(JSContext *ctx)
{
    JSRuntime *rt = ctx->rt;

    if (!JS_IsRegisteredClass(rt, JS_CLASS_CALL_SITE)) {
        if (init_class_range(rt, js_callsite_class_def, JS_CLASS_CALL_SITE,
                             countof(js_callsite_class_def)))
            return -1;
    }
    ctx->class_proto[JS_CLASS_CALL_SITE] = JS_NewObject(ctx);
    return JS_SetPropertyFunctionList(ctx, ctx->class_proto[JS_CLASS_CALL_SITE],
                                      js_callsite_proto_funcs,
                                      countof(js_callsite_proto_funcs));
}

/* DOMException */
typedef struct JSDOMExceptionData {
    JSValue name;
    JSValue message;
    int code;
} JSDOMExceptionData;

typedef struct JSDOMExceptionNameDef {
    const char * const name;
    const char * const code_name;
} JSDOMExceptionNameDef;

static const JSDOMExceptionNameDef js_dom_exception_names_table[] = {
    { "IndexSizeError", "INDEX_SIZE_ERR" },
    { NULL, "DOMSTRING_SIZE_ERR" },
    { "HierarchyRequestError", "HIERARCHY_REQUEST_ERR" },
    { "WrongDocumentError", "WRONG_DOCUMENT_ERR" },
    { "InvalidCharacterError", "INVALID_CHARACTER_ERR" },
    { NULL, "NO_DATA_ALLOWED_ERR" },
    { "NoModificationAllowedError", "NO_MODIFICATION_ALLOWED_ERR" },
    { "NotFoundError", "NOT_FOUND_ERR" },
    { "NotSupportedError", "NOT_SUPPORTED_ERR" },
    { "InUseAttributeError", "INUSE_ATTRIBUTE_ERR" },
    { "InvalidStateError", "INVALID_STATE_ERR" },
    { "SyntaxError", "SYNTAX_ERR" },
    { "InvalidModificationError", "INVALID_MODIFICATION_ERR" },
    { "NamespaceError", "NAMESPACE_ERR" },
    { "InvalidAccessError", "INVALID_ACCESS_ERR" },
    { NULL, "VALIDATION_ERR" },
    { "TypeMismatchError", "TYPE_MISMATCH_ERR" },
    { "SecurityError", "SECURITY_ERR" },
    { "NetworkError", "NETWORK_ERR" },
    { "AbortError", "ABORT_ERR" },
    { "URLMismatchError", "URL_MISMATCH_ERR" },
    { "QuotaExceededError", "QUOTA_EXCEEDED_ERR" },
    { "TimeoutError", "TIMEOUT_ERR" },
    { "InvalidNodeTypeError", "INVALID_NODE_TYPE_ERR" },
    { "DataCloneError", "DATA_CLONE_ERR" }
};

static void js_domexception_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSDOMExceptionData *s = JS_GetOpaque(val, JS_CLASS_DOM_EXCEPTION);
    if (s) {
        JS_FreeValueRT(rt, s->name);
        JS_FreeValueRT(rt, s->message);
        js_free_rt(rt, s);
    }
}

static void js_domexception_mark(JSRuntime *rt, JSValueConst val,
                                 JS_MarkFunc *mark_func)
{
    JSDOMExceptionData *s = JS_GetOpaque(val, JS_CLASS_DOM_EXCEPTION);
    if (s) {
        JS_MarkValue(rt, s->name, mark_func);
        JS_MarkValue(rt, s->message, mark_func);
    }
}

static JSValue js_domexception_constructor0(JSContext *ctx, JSValueConst new_target,
                                            int argc, JSValueConst *argv,
                                            int backtrace_flags)
{
    JSDOMExceptionData *s;
    JSValue obj, message, name;

    obj = js_create_from_ctor(ctx, new_target, JS_CLASS_DOM_EXCEPTION);
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    if (!JS_IsUndefined(argv[0]))
        message = JS_ToString(ctx, argv[0]);
    else
        message = js_empty_string(ctx->rt);
    if (JS_IsException(message))
        goto fail1;
    if (!JS_IsUndefined(argv[1]))
        name = JS_ToString(ctx, argv[1]);
    else
        name = JS_AtomToString(ctx, JS_ATOM_Error);
    if (JS_IsException(name))
        goto fail2;
    s = js_malloc(ctx, sizeof(*s));
    if (!s)
        goto fail3;
    s->name = name;
    s->message = message;
    s->code = -1;
    JS_SetOpaqueInternal(obj, s);
    build_backtrace(ctx, obj, JS_UNDEFINED, NULL, 0, 0, backtrace_flags);
    return obj;
fail3:
    JS_FreeValue(ctx, name);
fail2:
    JS_FreeValue(ctx, message);
fail1:
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue js_domexception_constructor(JSContext *ctx, JSValueConst new_target,
                                           int argc, JSValueConst *argv)
{
    if (JS_IsUndefined(new_target))
        return JS_ThrowTypeError(ctx, "constructor requires 'new'");
    return js_domexception_constructor0(ctx, new_target, argc, argv,
                                        JS_BACKTRACE_FLAG_SKIP_FIRST_LEVEL);
}

static JSValue js_domexception_getfield(JSContext *ctx, JSValueConst this_val,
                                        int magic)
{
    JSDOMExceptionData *s;
    JSValue *valp;

    s = JS_GetOpaque2(ctx, this_val, JS_CLASS_DOM_EXCEPTION);
    if (!s)
        return JS_EXCEPTION;
    valp = (void *)((char *)s + magic);
    return js_dup(*valp);
}

static JSValue js_domexception_get_code(JSContext *ctx, JSValueConst this_val)
{
    JSDOMExceptionData *s;
    const char *name, *it;
    int i;
    size_t len;

    s = JS_GetOpaque2(ctx, this_val, JS_CLASS_DOM_EXCEPTION);
    if (!s)
        return JS_EXCEPTION;
    if (s->code == -1) {
        name = JS_ToCStringLen(ctx, &len, s->name);
        if (!name)
            return JS_EXCEPTION;
        for (i = 0; i < countof(js_dom_exception_names_table); i++) {
            it = js_dom_exception_names_table[i].name;
            if (it && !strcmp(it, name) && len == strlen(it)) {
                s->code = i;
                break;
            }
        }
        s->code++;
        JS_FreeCString(ctx, name);
    }
    return js_int32(s->code);
}

static const JSCFunctionListEntry js_domexception_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("name", js_domexception_getfield, NULL,
        offsetof(JSDOMExceptionData, name) ),
    JS_CGETSET_MAGIC_DEF("message", js_domexception_getfield, NULL,
        offsetof(JSDOMExceptionData, message) ),
    JS_CGETSET_DEF("code", js_domexception_get_code, NULL ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "DOMException", JS_PROP_CONFIGURABLE ),
};

static const JSClassShortDef js_domexception_class_def[] = {
    { JS_ATOM_DOMException, js_domexception_finalizer, js_domexception_mark }, /* JS_CLASS_DOM_EXCEPTION */
};

JSValue JS_PRINTF_FORMAT_ATTR(3, 4) JS_ThrowDOMException(JSContext *ctx, const char *name, JS_PRINTF_FORMAT const char *fmt, ...)
{
    JSValue obj, js_name, js_message;
    JSValueConst argv[2];
    va_list ap;
    char buf[256];

    assert(JS_IsRegisteredClass(ctx->rt, JS_CLASS_DOM_EXCEPTION));
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    js_name = JS_NewString(ctx, name);
    if (JS_IsException(js_name))
        return JS_EXCEPTION;
    js_message = JS_NewString(ctx, buf);
    if (JS_IsException(js_message)) {
        JS_FreeValue(ctx, js_name);
        return JS_EXCEPTION;
    }
    argv[0] = js_message;
    argv[1] = js_name;
    obj = js_domexception_constructor0(ctx, JS_UNDEFINED, 2, argv, 0);
    JS_FreeValue(ctx, js_message);
    JS_FreeValue(ctx, js_name);
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    return JS_Throw(ctx, obj);
}

int JS_AddIntrinsicDOMException(JSContext *ctx)
{
    JSRuntime *rt = ctx->rt;
    int i;
    JSAtom name;
    JSValue ctor, proto;

    if (!JS_IsRegisteredClass(rt, JS_CLASS_DOM_EXCEPTION)) {
        if (init_class_range(rt, js_domexception_class_def, JS_CLASS_DOM_EXCEPTION,
                             countof(js_domexception_class_def)))
            return -1;
    }
    proto = JS_NewObjectClass(ctx, JS_CLASS_ERROR);
    JS_SetPropertyFunctionList(ctx, proto,
                               js_domexception_proto_funcs,
                               countof(js_domexception_proto_funcs));
    ctor = JS_NewCFunction2(ctx, js_domexception_constructor, "DOMException", 2,
                            JS_CFUNC_constructor_or_func, 0);
    JS_SetConstructor(ctx, ctor, proto);
    for (i = 0; i < countof(js_dom_exception_names_table); i++) {
        name = JS_NewAtom(ctx, js_dom_exception_names_table[i].code_name);
        JS_DefinePropertyValue(ctx, proto, name, js_int32(i + 1),
                               JS_PROP_ENUMERABLE);
        JS_DefinePropertyValue(ctx, ctor, name, js_int32(i + 1),
                               JS_PROP_ENUMERABLE);
        JS_FreeAtom(ctx, name);
    }
    JS_DefinePropertyValue(ctx, ctx->global_obj, JS_ATOM_DOMException, ctor,
                           JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    ctx->class_proto[JS_CLASS_DOM_EXCEPTION] = proto;
    return 0;
}
/* base64 */

static const unsigned char b64_enc[64] = {
    'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
    'Q','R','S','T','U','V','W','X','Y','Z',
    'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p',
    'q','r','s','t','u','v','w','x','y','z',
    '0','1','2','3','4','5','6','7','8','9',
    '+','/'
};

static const unsigned char b64url_enc[64] = {
    'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
    'Q','R','S','T','U','V','W','X','Y','Z',
    'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p',
    'q','r','s','t','u','v','w','x','y','z',
    '0','1','2','3','4','5','6','7','8','9',
    '-','_'
};

enum { K_VAL = 1u, K_WS = 2u };

static const uint8_t b64_val[256] = {
    ['A']=0, ['B']=1, ['C']=2, ['D']=3, ['E']=4, ['F']=5, ['G']=6, ['H']=7,
    ['I']=8, ['J']=9, ['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,
    ['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,['Y']=24,['Z']=25,
    ['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,['g']=32,['h']=33,
    ['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,['o']=40,['p']=41,
    ['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,['w']=48,['x']=49,['y']=50,['z']=51,
    ['0']=52,['1']=53,['2']=54,['3']=55,['4']=56,['5']=57,['6']=58,['7']=59,['8']=60,['9']=61,
    ['+']=62, ['/']=63,
    ['-']=62, ['_']=63,
};

static const char b64_flags[256] = {
    [' ']=K_WS, ['\t']=K_WS, ['\n']=K_WS, ['\f']=K_WS, ['\r']=K_WS,
    ['A']=K_VAL,['B']=K_VAL,['C']=K_VAL,['D']=K_VAL,['E']=K_VAL,['F']=K_VAL,['G']=K_VAL,['H']=K_VAL,
    ['I']=K_VAL,['J']=K_VAL,['K']=K_VAL,['L']=K_VAL,['M']=K_VAL,['N']=K_VAL,['O']=K_VAL,['P']=K_VAL,
    ['Q']=K_VAL,['R']=K_VAL,['S']=K_VAL,['T']=K_VAL,['U']=K_VAL,['V']=K_VAL,['W']=K_VAL,['X']=K_VAL,
    ['Y']=K_VAL,['Z']=K_VAL,
    ['a']=K_VAL,['b']=K_VAL,['c']=K_VAL,['d']=K_VAL,['e']=K_VAL,['f']=K_VAL,['g']=K_VAL,['h']=K_VAL,
    ['i']=K_VAL,['j']=K_VAL,['k']=K_VAL,['l']=K_VAL,['m']=K_VAL,['n']=K_VAL,['o']=K_VAL,['p']=K_VAL,
    ['q']=K_VAL,['r']=K_VAL,['s']=K_VAL,['t']=K_VAL,['u']=K_VAL,['v']=K_VAL,['w']=K_VAL,['x']=K_VAL,
    ['y']=K_VAL,['z']=K_VAL,
    ['0']=K_VAL,['1']=K_VAL,['2']=K_VAL,['3']=K_VAL,['4']=K_VAL,['5']=K_VAL,['6']=K_VAL,['7']=K_VAL,
    ['8']=K_VAL,['9']=K_VAL,
    ['+']=K_VAL,['/']=K_VAL,
};

static const char b64_flags_url[256] = {
    [' ']=K_WS, ['\t']=K_WS, ['\n']=K_WS, ['\f']=K_WS, ['\r']=K_WS,
    ['A']=K_VAL,['B']=K_VAL,['C']=K_VAL,['D']=K_VAL,['E']=K_VAL,['F']=K_VAL,['G']=K_VAL,['H']=K_VAL,
    ['I']=K_VAL,['J']=K_VAL,['K']=K_VAL,['L']=K_VAL,['M']=K_VAL,['N']=K_VAL,['O']=K_VAL,['P']=K_VAL,
    ['Q']=K_VAL,['R']=K_VAL,['S']=K_VAL,['T']=K_VAL,['U']=K_VAL,['V']=K_VAL,['W']=K_VAL,['X']=K_VAL,
    ['Y']=K_VAL,['Z']=K_VAL,
    ['a']=K_VAL,['b']=K_VAL,['c']=K_VAL,['d']=K_VAL,['e']=K_VAL,['f']=K_VAL,['g']=K_VAL,['h']=K_VAL,
    ['i']=K_VAL,['j']=K_VAL,['k']=K_VAL,['l']=K_VAL,['m']=K_VAL,['n']=K_VAL,['o']=K_VAL,['p']=K_VAL,
    ['q']=K_VAL,['r']=K_VAL,['s']=K_VAL,['t']=K_VAL,['u']=K_VAL,['v']=K_VAL,['w']=K_VAL,['x']=K_VAL,
    ['y']=K_VAL,['z']=K_VAL,
    ['0']=K_VAL,['1']=K_VAL,['2']=K_VAL,['3']=K_VAL,['4']=K_VAL,['5']=K_VAL,['6']=K_VAL,['7']=K_VAL,
    ['8']=K_VAL,['9']=K_VAL,
    ['-']=K_VAL,['_']=K_VAL,
};

static size_t b64_encode(const uint8_t *src, size_t len, char *dst,
                         const unsigned char *alpha)
{
    size_t i = 0, j = 0;
    size_t main_len = (len / 3) * 3;

    for (; i < main_len; i += 3, j += 4) {
        uint32_t v = 65536*src[i] + 256*src[i + 1] + src[i + 2];
        dst[j + 0] = alpha[(v >> 18) & 63];
        dst[j + 1] = alpha[(v >> 12) & 63];
        dst[j + 2] = alpha[(v >> 6) & 63];
        dst[j + 3] = alpha[v & 63];
    }

    size_t rem = len - i;
    if (rem == 1) {
        uint32_t v = 65536*src[i];
        dst[j++] = alpha[(v >> 18) & 63];
        dst[j++] = alpha[(v >> 12) & 63];
        dst[j++] = '=';
        dst[j++] = '=';
    } else if (rem == 2) {
        uint32_t v = 65536*src[i] + 256*src[i + 1];
        dst[j++] = alpha[(v >> 18) & 63];
        dst[j++] = alpha[(v >> 12) & 63];
        dst[j++] = alpha[(v >> 6) & 63];
        dst[j++] = '=';
    }
    return j;
}

/* Implements https://infra.spec.whatwg.org/#forgiving-base64-decode */
static size_t
b64_decode(const char *src, size_t len, uint8_t *dst, int *err)
{
    size_t i, j;
    uint32_t acc;
    int seen, pad;
    unsigned ch;

    acc = 0;
    seen = 0;
    for (i = 0, j = 0; i < len; i++) {
        ch = (unsigned char)src[i];
        if ((b64_flags[ch] & K_WS))
            continue;
        if (!(b64_flags[ch] & K_VAL))
            break;
        acc = (acc << 6) | b64_val[ch];
        seen++;
        if (seen == 4) {
            dst[j++] = (acc >> 16) & 0xFF;
            dst[j++] = (acc >> 8) & 0xFF;
            dst[j++] = acc & 0xFF;
            seen = 0;
            acc = 0;
        }
    }

    if (seen != 0) {
        if (seen == 3) {
            dst[j++] = (acc >> 10) & 0xFF;
            dst[j++] = (acc >> 2) & 0xFF;
        } else if (seen == 2) {
            dst[j++] = (acc >> 4) & 0xFF;
        } else {
            *err = 1;
            return 0;
        }
        for (pad = 0; i < len; i++) {
            ch = (unsigned char)src[i];
            if (pad < 2 && ch == '=')
                pad++;
            else if (!(b64_flags[ch] & K_WS))
                break;
        }
        if (pad != 0 && seen + pad != 4) {
            *err = 1;
            return 0;
        }
    }

    *err = i < len;
    return j;
}

static JSValue js_btoa(JSContext *ctx, JSValueConst this_val,
                       int argc, JSValueConst *argv)
{
    const uint8_t *in8;
    uint8_t *tmp = NULL;
    uint8_t *outp;
    JSValue val, ret = JS_EXCEPTION;
    JSString *s, *ostr;
    size_t len, out_len, written;

    val = JS_ToString(ctx, argv[0]);
    if (unlikely(JS_IsException(val)))
        return JS_EXCEPTION;

    s = JS_VALUE_GET_STRING(val);
    len = (size_t)s->len;

    if (likely(!s->is_wide_char)) {
        in8 = (const uint8_t *)str8(s);
    } else {
        const uint16_t *src = str16(s);
        tmp = js_malloc(ctx, likely(len) ? len : 1);
        if (unlikely(!tmp))
            goto fail;
        for (size_t i = 0; i < len; i++) {
            uint32_t c = src[i];
            if (unlikely(c > 0xFF)) {
                JS_ThrowDOMException(ctx, "InvalidCharacterError",
                                     "String contains an invalid character");
                goto fail;
            }
            tmp[i] = (uint8_t)c;
        }
        in8 = tmp;
    }

    if (unlikely(len > (SIZE_MAX - 2) / 3)) {
        JS_ThrowRangeError(ctx, "input too large");
        goto fail;
    }
    out_len = 4 * ((len + 2) / 3);
    if (unlikely(out_len > JS_STRING_LEN_MAX)) {
        JS_ThrowRangeError(ctx, "output too large");
        goto fail;
    }

    ostr = js_alloc_string(ctx, out_len, 0);
    if (unlikely(!ostr))
        goto fail;

    outp = str8(ostr);
    written = b64_encode(in8, len, (char *)outp, b64_enc);
    outp[written] = '\0';
    ostr->len = out_len;
    ret = JS_MKPTR(JS_TAG_STRING, ostr);
fail:
    if (tmp)
        js_free(ctx, tmp);
    JS_FreeValue(ctx, val);
    return ret;
}

static JSValue js_atob(JSContext *ctx, JSValueConst this_val,
                       int argc, JSValueConst *argv)
{
    const uint8_t *in;
    uint8_t *tmp = NULL, *outp;
    JSValue val, ret = JS_EXCEPTION;
    JSString *s, *ostr;
    size_t slen, out_cap, out_len;
    int err;

    val = JS_ToString(ctx, argv[0]);
    if (unlikely(JS_IsException(val)))
        return JS_EXCEPTION;

    s = JS_VALUE_GET_STRING(val);
    slen = (size_t)s->len;

    if (likely(!s->is_wide_char)) {
        const uint8_t *p = (const uint8_t *)str8(s);
        for (size_t i = 0; i < slen; i++) {
            if (unlikely(p[i] & 0x80)) {
                JS_ThrowDOMException(ctx, "InvalidCharacterError",
                    "The string to be decoded is not correctly encoded");
                goto fail;
            }
        }
        in = p;
    } else {
        const uint16_t *src = str16(s);
        tmp = js_malloc(ctx, likely(slen) ? slen : 1);
        if (unlikely(!tmp))
            goto fail;
        for (size_t i = 0; i < slen; i++) {
            if (unlikely(src[i] > 0x7F)) {
                JS_ThrowDOMException(ctx, "InvalidCharacterError",
                    "The string to be decoded is not correctly encoded");
                goto fail;
            }
            tmp[i] = (uint8_t)src[i];
        }
        in = tmp;
    }

    if (unlikely(slen > (SIZE_MAX / 3) * 4)) {
        JS_ThrowRangeError(ctx, "input too large");
        goto fail;
    }
    out_cap = (slen / 4) * 3 + 3;
    if (unlikely(out_cap > JS_STRING_LEN_MAX)) {
        JS_ThrowRangeError(ctx, "output too large");
        goto fail;
    }

    ostr = js_alloc_string(ctx, out_cap, 0);
    if (unlikely(!ostr))
        goto fail;

    outp = str8(ostr);
    err = 0;
    out_len = b64_decode((const char *)in, slen, outp, &err);

    if (unlikely(err)) {
        js_free_string(ctx->rt, ostr);
        JS_ThrowDOMException(ctx, "InvalidCharacterError",
            "The string to be decoded is not correctly encoded");
        goto fail;
    }
    outp[out_len] = '\0';
    ostr->len = out_len;
    ret = JS_MKPTR(JS_TAG_STRING, ostr);
fail:
    if (tmp)
        js_free(ctx, tmp);
    JS_FreeValue(ctx, val);
    return ret;
}

static const JSCFunctionListEntry js_base64_funcs[] = {
    JS_CFUNC_DEF("btoa", 1, js_btoa),
    JS_CFUNC_DEF("atob", 1, js_atob),
};


/* Uint8Array base64/hex (tc39 proposal-arraybuffer-base64) */

enum {
    B64_ALPHABET_BASE64 = 0,
    B64_ALPHABET_BASE64URL = 1,
};

enum {
    B64_LAST_LOOSE = 0,
    B64_LAST_STRICT = 1,
    B64_LAST_STOP_BEFORE_PARTIAL = 2,
};


static size_t b64_skip_ws(const char *src, size_t len, size_t index,
                          const char *flags)
{
    while (index < len && (flags[(unsigned char)src[index]] & K_WS))
        index++;
    return index;
}

/* Implements the FromBase64 abstract operation.
   src/src_len: the input string (must be ASCII/latin1)
   dst/max_len: output buffer
   flags: b64_flags or b64_flags_url (selects valid characters)
   last_chunk: B64_LAST_LOOSE, B64_LAST_STRICT, or B64_LAST_STOP_BEFORE_PARTIAL
   *p_read: set to number of input characters consumed
   *p_err: set to 1 on error, 0 on success
   Returns: number of bytes written to dst */
static size_t from_base64(const char *src, size_t src_len,
                          uint8_t *dst, size_t max_len,
                          const char *flags, int last_chunk,
                          size_t *p_read, int *p_err)
{
    size_t read = 0, written = 0;
    uint32_t acc = 0;
    int seen = 0;
    size_t index = 0;

    *p_err = 0;

    if (max_len == 0) {
        *p_read = 0;
        return 0;
    }

    /* Fast path: decode complete groups of 4 valid characters.
       Breaks out on whitespace, padding, invalid chars, or capacity. */
    while (index + 4 <= src_len && written + 3 <= max_len) {
        uint8_t f = flags[(unsigned char)src[index]]
                  & flags[(unsigned char)src[index + 1]]
                  & flags[(unsigned char)src[index + 2]]
                  & flags[(unsigned char)src[index + 3]];
        if (!(f & K_VAL))
            break;
        uint32_t v = ((uint32_t)b64_val[(unsigned char)src[index]] << 18)
                   | ((uint32_t)b64_val[(unsigned char)src[index + 1]] << 12)
                   | ((uint32_t)b64_val[(unsigned char)src[index + 2]] << 6)
                   | (uint32_t)b64_val[(unsigned char)src[index + 3]];
        dst[written]     = (uint8_t)(v >> 16);
        dst[written + 1] = (uint8_t)(v >> 8);
        dst[written + 2] = (uint8_t)(v);
        written += 3;
        index += 4;
    }
    read = index;

    if (written >= max_len) {
        *p_read = read;
        return written;
    }

    /* Slow path: handle whitespace, padding, partial groups, capacity. */
    for (;;) {
        index = b64_skip_ws(src, src_len, index, flags);

        if (index == src_len) {
            if (seen > 0) {
                if (last_chunk == B64_LAST_STOP_BEFORE_PARTIAL) {
                    *p_read = read;
                    return written;
                }
                if (last_chunk == B64_LAST_STRICT) {
                    *p_err = 1;
                    return 0;
                }
                /* loose */
                if (seen == 1) {
                    *p_err = 1;
                    return 0;
                }
                goto decode_partial;
            }
            *p_read = src_len;
            return written;
        }

        unsigned char ch = src[index++];

        if (ch == '=') {
            if (seen < 2) {
                *p_err = 1;
                return 0;
            }
            index = b64_skip_ws(src, src_len, index, flags);
            if (seen == 2) {
                if (index == src_len) {
                    if (last_chunk == B64_LAST_STOP_BEFORE_PARTIAL) {
                        *p_read = read;
                        return written;
                    }
                    *p_err = 1;
                    return 0;
                }
                if (src[index] == '=') {
                    index++;
                    index = b64_skip_ws(src, src_len, index, flags);
                } else {
                    *p_err = 1;
                    return 0;
                }
            }
            /* After padding, only whitespace is allowed */
            if (index != src_len) {
                *p_err = 1;
                return 0;
            }
            if (last_chunk == B64_LAST_STRICT) {
                uint32_t mask = (seen == 2) ? 0xF : 0x3;
                if (acc & mask) {
                    *p_err = 1;
                    return 0;
                }
            }
            goto decode_partial;
        }

        if (!(flags[ch] & K_VAL)) {
            *p_err = 1;
            return 0;
        }

        /* Check remaining capacity before committing to this group */
        {
            size_t remaining = max_len - written;
            if ((remaining == 1 && seen == 2) ||
                    (remaining == 2 && seen == 3)) {
                *p_read = read;
                return written;
            }
        }

        acc = (acc << 6) | b64_val[ch];
        seen++;

        if (seen == 4) {
            dst[written]     = (uint8_t)(acc >> 16);
            dst[written + 1] = (uint8_t)(acc >> 8);
            dst[written + 2] = (uint8_t)(acc);
            written += 3;
            acc = 0;
            seen = 0;
            read = index;
            if (written >= max_len) {
                *p_read = read;
                return written;
            }
        }
    }

decode_partial:
    if (seen == 2) {
        dst[written++] = (uint8_t)(acc >> 4);
    } else if (seen == 3) {
        dst[written]     = (uint8_t)(acc >> 10);
        dst[written + 1] = (uint8_t)(acc >> 2);
        written += 2;
    }
    *p_read = src_len;
    return written;
}

/* Hex helpers */
static const char u8a_hex_digits[] = "0123456789abcdef";

static int u8a_hex_nibble(unsigned char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static size_t u8a_hex_encode(const uint8_t *src, size_t len, char *dst)
{
    for (size_t i = 0; i < len; i++) {
        dst[i * 2]     = u8a_hex_digits[src[i] >> 4];
        dst[i * 2 + 1] = u8a_hex_digits[src[i] & 0xF];
    }
    return len * 2;
}

/* Decode hex string to bytes.
   Returns bytes written. Sets *p_read to chars consumed, *p_err on error. */
static size_t u8a_hex_decode(const char *src, size_t src_len,
                             uint8_t *dst, size_t max_len,
                             size_t *p_read, int *p_err)
{
    size_t written = 0, i = 0;
    *p_err = 0;

    if (src_len & 1) {
        *p_err = 1;
        return 0;
    }

    while (i < src_len && written < max_len) {
        int hi = u8a_hex_nibble(src[i]);
        int lo = u8a_hex_nibble(src[i + 1]);
        if (hi < 0 || lo < 0) {
            *p_err = 1;
            return 0;
        }
        dst[written++] = (uint8_t)((hi << 4) | lo);
        i += 2;
    }

    *p_read = i;
    return written;
}

/* Validate that this_val is a Uint8Array (type check only, no detach check).
   Returns the JSObject pointer or NULL on error (throws). */
static JSObject *check_uint8array(JSContext *ctx, JSValueConst this_val)
{
    JSObject *p;

    if (JS_VALUE_GET_TAG(this_val) != JS_TAG_OBJECT)
        goto fail;
    p = JS_VALUE_GET_OBJ(this_val);
    if (p->class_id != JS_CLASS_UINT8_ARRAY)
        goto fail;
    return p;
fail:
    JS_ThrowTypeError(ctx, "not a Uint8Array");
    return NULL;
}

/* Get the data pointer and length of a Uint8Array, checking for detached
   buffers. Must be called after options are read (per spec ordering).
   Returns 0 on success, -1 on error (throws). */
static int get_uint8array_bytes(JSContext *ctx, JSObject *p,
                                uint8_t **pdata, size_t *plen)
{
    if (typed_array_is_oob(p)) {
        JS_ThrowTypeErrorArrayBufferOOB(ctx);
        return -1;
    }
    *pdata = p->u.array.u.uint8_ptr;
    *plen = p->u.array.count;
    return 0;
}

/* Validate options is undefined or an object (GetOptionsObject).
   Returns 0 on success, -1 on error (throws). */
static int check_options_object(JSContext *ctx, JSValueConst options)
{
    if (JS_IsUndefined(options))
        return 0;
    if (!JS_IsObject(options)) {
        JS_ThrowTypeError(ctx, "options must be an object");
        return -1;
    }
    return 0;
}

/* Parse the 'alphabet' option from an options object.
   Returns B64_ALPHABET_BASE64 or B64_ALPHABET_BASE64URL, or -1 on error. */
static int parse_alphabet_option(JSContext *ctx, JSValueConst options)
{
    JSValue val;
    const char *str;
    int ret;

    if (JS_IsUndefined(options))
        return B64_ALPHABET_BASE64;

    val = JS_GetPropertyStr(ctx, options, "alphabet");
    if (JS_IsException(val))
        return -1;
    if (JS_IsUndefined(val))
        return B64_ALPHABET_BASE64;
    if (!JS_IsString(val)) {
        JS_FreeValue(ctx, val);
        JS_ThrowTypeError(ctx, "expected string for alphabet");
        return -1;
    }

    str = JS_ToCString(ctx, val);
    JS_FreeValue(ctx, val);
    if (!str)
        return -1;

    if (!strcmp(str, "base64"))
        ret = B64_ALPHABET_BASE64;
    else if (!strcmp(str, "base64url"))
        ret = B64_ALPHABET_BASE64URL;
    else {
        JS_ThrowTypeError(ctx, "invalid alphabet");
        ret = -1;
    }
    JS_FreeCString(ctx, str);
    return ret;
}

/* Parse the 'lastChunkHandling' option. Returns mode or -1 on error. */
static int parse_last_chunk_option(JSContext *ctx, JSValueConst options)
{
    JSValue val;
    const char *str;
    int ret;

    if (JS_IsUndefined(options))
        return B64_LAST_LOOSE;

    val = JS_GetPropertyStr(ctx, options, "lastChunkHandling");
    if (JS_IsException(val))
        return -1;
    if (JS_IsUndefined(val))
        return B64_LAST_LOOSE;
    if (!JS_IsString(val)) {
        JS_FreeValue(ctx, val);
        JS_ThrowTypeError(ctx, "expected string for lastChunkHandling");
        return -1;
    }

    str = JS_ToCString(ctx, val);
    JS_FreeValue(ctx, val);
    if (!str)
        return -1;

    if (!strcmp(str, "loose"))
        ret = B64_LAST_LOOSE;
    else if (!strcmp(str, "strict"))
        ret = B64_LAST_STRICT;
    else if (!strcmp(str, "stop-before-partial"))
        ret = B64_LAST_STOP_BEFORE_PARTIAL;
    else {
        JS_ThrowTypeError(ctx, "invalid lastChunkHandling option");
        ret = -1;
    }
    JS_FreeCString(ctx, str);
    return ret;
}

/* Uint8Array.prototype.toBase64([options]) */
static JSValue js_uint8array_to_base64(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    uint8_t *data;
    size_t len;
    JSValue options;
    JSObject *p;
    int alphabet, omit_padding;
    size_t out_len, written;
    JSString *ostr;
    char *dst;

    p = check_uint8array(ctx, this_val);
    if (!p)
        return JS_EXCEPTION;

    options = argc > 0 ? unsafe_unconst(argv[0]) : JS_UNDEFINED;
    if (check_options_object(ctx, options))
        return JS_EXCEPTION;
    alphabet = parse_alphabet_option(ctx, options);
    if (alphabet < 0)
        return JS_EXCEPTION;

    omit_padding = 0;
    if (!JS_IsUndefined(options)) {
        JSValue op_val = JS_GetPropertyStr(ctx, options, "omitPadding");
        if (JS_IsException(op_val))
            return JS_EXCEPTION;
        omit_padding = JS_ToBool(ctx, op_val);
        JS_FreeValue(ctx, op_val);
    }

    if (get_uint8array_bytes(ctx, p, &data, &len))
        return JS_EXCEPTION;

    out_len = 4 * ((len + 2) / 3);

    if (unlikely(out_len > JS_STRING_LEN_MAX))
        return JS_ThrowRangeError(ctx, "output too large");

    ostr = js_alloc_string(ctx, out_len, 0);
    if (!ostr)
        return JS_EXCEPTION;

    dst = (char *)str8(ostr);
    written = b64_encode(data, len, dst,
                         alphabet == B64_ALPHABET_BASE64URL ? b64url_enc : b64_enc);
    if (omit_padding) {
        while (written > 0 && dst[written - 1] == '=')
            written--;
    }
    dst[written] = '\0';

    ostr->len = written;
    return JS_MKPTR(JS_TAG_STRING, ostr);
}

/* Uint8Array.prototype.toHex() */
static JSValue js_uint8array_to_hex(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    uint8_t *data;
    size_t len, out_len;
    JSObject *p;
    JSString *ostr;

    p = check_uint8array(ctx, this_val);
    if (!p)
        return JS_EXCEPTION;
    if (get_uint8array_bytes(ctx, p, &data, &len))
        return JS_EXCEPTION;

    out_len = len * 2;
    if (unlikely(out_len > JS_STRING_LEN_MAX))
        return JS_ThrowRangeError(ctx, "output too large");

    ostr = js_alloc_string(ctx, out_len, 0);
    if (!ostr)
        return JS_EXCEPTION;

    u8a_hex_encode(data, len, (char *)str8(ostr));
    str8(ostr)[out_len] = '\0';
    return JS_MKPTR(JS_TAG_STRING, ostr);
}

/* Uint8Array.fromBase64(string[, options]) */
static JSValue js_uint8array_from_base64(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    const char *str;
    size_t str_len, read_pos, decoded_len, out_cap;
    int alphabet, last_chunk, err;
    uint8_t *buf;
    JSValue result, options;

    if (!JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "expected string");

    str = JS_ToCStringLen(ctx, &str_len, argv[0]);
    if (!str)
        return JS_EXCEPTION;

    options = argc > 1 ? unsafe_unconst(argv[1]) : JS_UNDEFINED;
    if (check_options_object(ctx, options)) {
        JS_FreeCString(ctx, str);
        return JS_EXCEPTION;
    }
    alphabet = parse_alphabet_option(ctx, options);
    if (alphabet < 0) {
        JS_FreeCString(ctx, str);
        return JS_EXCEPTION;
    }
    last_chunk = parse_last_chunk_option(ctx, options);
    if (last_chunk < 0) {
        JS_FreeCString(ctx, str);
        return JS_EXCEPTION;
    }

    out_cap = (str_len / 4) * 3 + 3;
    buf = js_malloc(ctx, out_cap ? out_cap : 1);
    if (!buf) {
        JS_FreeCString(ctx, str);
        return JS_EXCEPTION;
    }

    decoded_len = from_base64(str, str_len, buf, out_cap,
                              alphabet == B64_ALPHABET_BASE64URL
                                  ? b64_flags_url : b64_flags,
                              last_chunk, &read_pos, &err);
    JS_FreeCString(ctx, str);

    if (err) {
        js_free(ctx, buf);
        return JS_ThrowSyntaxError(ctx, "invalid base64 string");
    }

    result = JS_NewUint8ArrayCopy(ctx, buf, decoded_len);
    js_free(ctx, buf);
    return result;
}

/* Uint8Array.fromHex(string) */
static JSValue js_uint8array_from_hex(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    const char *str;
    size_t str_len, read_pos, decoded_len, out_cap;
    int err;
    uint8_t *buf;
    JSValue result;

    if (!JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "expected string");

    str = JS_ToCStringLen(ctx, &str_len, argv[0]);
    if (!str)
        return JS_EXCEPTION;

    out_cap = str_len / 2 + 1;
    buf = js_malloc(ctx, out_cap ? out_cap : 1);
    if (!buf) {
        JS_FreeCString(ctx, str);
        return JS_EXCEPTION;
    }

    decoded_len = u8a_hex_decode(str, str_len, buf, out_cap, &read_pos, &err);
    JS_FreeCString(ctx, str);

    if (err) {
        js_free(ctx, buf);
        return JS_ThrowSyntaxError(ctx, "invalid hex string");
    }

    result = JS_NewUint8ArrayCopy(ctx, buf, decoded_len);
    js_free(ctx, buf);
    return result;
}

/* Return a { read, written } result object */
static JSValue js_make_read_written(JSContext *ctx, size_t read, size_t written)
{
    JSValue obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    if (JS_DefinePropertyValueStr(ctx, obj, "read",
            js_uint32(read), JS_PROP_C_W_E) < 0)
        goto fail;
    if (JS_DefinePropertyValueStr(ctx, obj, "written",
            js_uint32(written), JS_PROP_C_W_E) < 0)
        goto fail;
    return obj;
fail:
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

/* Uint8Array.prototype.setFromBase64(string[, options]) */
static JSValue js_uint8array_set_from_base64(JSContext *ctx,
                                             JSValueConst this_val,
                                             int argc, JSValueConst *argv)
{
    uint8_t *data;
    size_t len;
    const char *str;
    size_t str_len, read_pos, decoded_len;
    JSObject *p;
    int alphabet, last_chunk, err;
    JSValue options;

    p = check_uint8array(ctx, this_val);
    if (!p)
        return JS_EXCEPTION;

    if (typed_array_is_immutable(p))
        return JS_ThrowTypeErrorImmutableArrayBuffer(ctx);

    if (!JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "expected string");

    str = JS_ToCStringLen(ctx, &str_len, argv[0]);
    if (!str)
        return JS_EXCEPTION;

    options = argc > 1 ? unsafe_unconst(argv[1]) : JS_UNDEFINED;
    if (check_options_object(ctx, options)) {
        JS_FreeCString(ctx, str);
        return JS_EXCEPTION;
    }
    alphabet = parse_alphabet_option(ctx, options);
    if (alphabet < 0) {
        JS_FreeCString(ctx, str);
        return JS_EXCEPTION;
    }
    last_chunk = parse_last_chunk_option(ctx, options);
    if (last_chunk < 0) {
        JS_FreeCString(ctx, str);
        return JS_EXCEPTION;
    }

    if (get_uint8array_bytes(ctx, p, &data, &len)) {
        JS_FreeCString(ctx, str);
        return JS_EXCEPTION;
    }

    decoded_len = from_base64(str, str_len, data, len,
                              alphabet == B64_ALPHABET_BASE64URL
                                  ? b64_flags_url : b64_flags,
                              last_chunk, &read_pos, &err);
    JS_FreeCString(ctx, str);

    if (err)
        return JS_ThrowSyntaxError(ctx, "invalid base64 string");

    return js_make_read_written(ctx, read_pos, decoded_len);
}

/* Uint8Array.prototype.setFromHex(string) */
static JSValue js_uint8array_set_from_hex(JSContext *ctx,
                                          JSValueConst this_val,
                                          int argc, JSValueConst *argv)
{
    uint8_t *data;
    size_t len;
    const char *str;
    size_t str_len, read_pos, decoded_len;
    JSObject *p;
    int err;

    p = check_uint8array(ctx, this_val);
    if (!p)
        return JS_EXCEPTION;

    if (typed_array_is_immutable(p))
        return JS_ThrowTypeErrorImmutableArrayBuffer(ctx);

    if (!JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "expected string");

    str = JS_ToCStringLen(ctx, &str_len, argv[0]);
    if (!str)
        return JS_EXCEPTION;

    if (get_uint8array_bytes(ctx, p, &data, &len)) {
        JS_FreeCString(ctx, str);
        return JS_EXCEPTION;
    }

    decoded_len = u8a_hex_decode(str, str_len, data, len, &read_pos, &err);
    JS_FreeCString(ctx, str);

    if (err)
        return JS_ThrowSyntaxError(ctx, "invalid hex string");

    return js_make_read_written(ctx, read_pos, decoded_len);
}

static const JSCFunctionListEntry js_uint8array_proto_funcs[] = {
    JS_CFUNC_DEF("toBase64", 0, js_uint8array_to_base64),
    JS_CFUNC_DEF("toHex", 0, js_uint8array_to_hex),
    JS_CFUNC_DEF("setFromBase64", 1, js_uint8array_set_from_base64),
    JS_CFUNC_DEF("setFromHex", 1, js_uint8array_set_from_hex),
};

static const JSCFunctionListEntry js_uint8array_funcs[] = {
    JS_CFUNC_DEF("fromBase64", 1, js_uint8array_from_base64),
    JS_CFUNC_DEF("fromHex", 1, js_uint8array_from_hex),
};

static int js_uint8array_funcs_init(JSContext *ctx)
{
    JSValue ctor, proto;

    ctor = JS_GetProperty(ctx, ctx->global_obj, JS_ATOM_Uint8Array);
    if (JS_IsException(ctor))
        return -1;
    proto = JS_GetProperty(ctx, ctor, JS_ATOM_prototype);
    if (JS_IsException(proto)) {
        JS_FreeValue(ctx, ctor);
        return -1;
    }
    JS_SetPropertyFunctionList(ctx, proto,
                               js_uint8array_proto_funcs,
                               countof(js_uint8array_proto_funcs));
    JS_FreeValue(ctx, proto);
    JS_SetPropertyFunctionList(ctx, ctor,
                               js_uint8array_funcs,
                               countof(js_uint8array_funcs));
    JS_FreeValue(ctx, ctor);
    return 0;
}

int JS_AddIntrinsicAToB(JSContext *ctx)
{
    if (!JS_IsRegisteredClass(ctx->rt, JS_CLASS_DOM_EXCEPTION)) {
        if (JS_AddIntrinsicDOMException(ctx))
            return -1;
    }
    JS_SetPropertyFunctionList(ctx, ctx->global_obj,
                               js_base64_funcs, countof(js_base64_funcs));
    return 0;
}

bool JS_DetectModule(const char *input, size_t input_len)
{
#ifndef TURBOJS_DISABLE_PARSER
    JSRuntime *rt;
    JSContext *ctx;
    JSValue val;
    bool is_module;

    is_module = true;
    rt = JS_NewRuntime();
    if (!rt)
        return false;
    ctx = JS_NewContextRaw(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return false;
    }
    JS_AddIntrinsicRegExpCompiler(ctx); // otherwise regexp literals don't parse
    val = __JS_EvalInternal(ctx, JS_UNDEFINED, input, input_len, "<unnamed>", 1,
                            JS_EVAL_TYPE_MODULE|JS_EVAL_FLAG_COMPILE_ONLY, -1);
    if (JS_IsException(val)) {
        const char *msg = JS_ToCString(ctx, rt->current_exception);
        // gruesome hack to recognize exceptions from import statements;
        // necessary because we don't pass in a module loader
        is_module = !!strstr(msg, "ReferenceError: could not load module");
        JS_FreeCString(ctx, msg);
    }
    JS_FreeValue(ctx, val);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return is_module;
#else
    return false;
#endif // TURBOJS_DISABLE_PARSER
}

uintptr_t js_std_cmd(int cmd, ...) {
    JSContext *ctx;
    JSRuntime *rt;
    JSValue *pv;
    uintptr_t rv;
    va_list ap;

    rv = 0;
    va_start(ap, cmd);
    switch (cmd) {
    case 0: // GetOpaque
        rt = va_arg(ap, JSRuntime *);
        rv = (uintptr_t)rt->libc_opaque;
        break;
    case 1: // SetOpaque
        rt = va_arg(ap, JSRuntime *);
        rt->libc_opaque = va_arg(ap, void *);
        break;
    case 2: // ErrorBackTrace
        ctx = va_arg(ap, JSContext *);
        pv = va_arg(ap, JSValue *);
        *pv = ctx->error_back_trace;
        ctx->error_back_trace = JS_UNDEFINED;
        break;
    case 3: // GetStringKind
        ctx = va_arg(ap, JSContext *);
        pv = va_arg(ap, JSValue *);
        rv = -1;
        if (JS_IsString(*pv))
            rv = JS_VALUE_GET_STRING(*pv)->kind;
        break;
    default:
        rv = -1;
    }
    va_end(ap);

    return rv;
}

#undef malloc
#undef free
#undef realloc
