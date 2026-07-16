/* Engine domain source: core/engine_core.inc -> runtime_support.
 * Ownership: core subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

bool turbojs_internal_is_job_pending(JSRuntime *rt)
{
    return !list_empty(&rt->job_list);
}

JSContext *turbojs_internal_get_pending_job_context(JSRuntime *rt)
{
    if (turbojs_internal_is_job_pending(rt)) {
        return list_entry(rt->job_list.next, JSJobEntry, link)->ctx;
    }
    return NULL;
}

/* return < 0 if exception, 0 if no job pending, 1 if a job was
   executed successfully. the context of the job is stored in '*pctx' */
int turbojs_internal_execute_pending_job(JSRuntime *rt, JSContext **pctx)
{
    JSContext *ctx;
    JSJobEntry *e;
    JSValue res;
    int i, ret;

    if (list_empty(&rt->job_list)) {
        *pctx = NULL;
        return 0;
    }

    /* get the first pending job and execute it */
    e = list_entry(rt->job_list.next, JSJobEntry, link);
    list_del(&e->link);
    ctx = e->ctx;
    res = e->job_func(e->ctx, e->argc, vc(e->argv));
    for(i = 0; i < e->argc; i++)
        JS_FreeValue(ctx, e->argv[i]);
    if (JS_IsException(res))
        ret = -1;
    else
        ret = 1;
    JS_FreeValue(ctx, res);
    js_free(ctx, e);
    *pctx = ctx;
    return ret;
}

static inline uint32_t atom_get_free(const JSAtomStruct *p)
{
    return (uintptr_t)p >> 1;
}

static inline bool atom_is_free(const JSAtomStruct *p)
{
    return (uintptr_t)p & 1;
}

static inline JSAtomStruct *atom_set_free(uint32_t v)
{
    return (JSAtomStruct *)(((uintptr_t)v << 1) | 1);
}

/* Note: the string contents are uninitialized */
static JSString *js_alloc_string_rt(JSRuntime *rt, int max_len, int is_wide_char)
{
    JSString *str;
    str = js_malloc_rt(rt, sizeof(JSString) + (max_len << is_wide_char) + 1 - is_wide_char);
    if (unlikely(!str))
        return NULL;
    JS_REF_COUNT(str) = 1;
    str->is_wide_char = is_wide_char;
    str->len = max_len;
    str->kind = JS_STRING_KIND_NORMAL;
    str->atom_type = 0;
    str->hash = 0;          /* optional but costless */
    str->hash_next = 0;     /* optional */
#ifdef ENABLE_DUMPS // JS_DUMP_LEAKS
    list_add_tail(&str->link, &rt->string_list);
#endif
    return str;
}

static JSString *js_alloc_string(JSContext *ctx, int max_len, int is_wide_char)
{
    JSString *p;
    p = js_alloc_string_rt(ctx->rt, max_len, is_wide_char);
    if (unlikely(!p)) {
        JS_ThrowOutOfMemory(ctx);
        return NULL;
    }
    return p;
}

static inline void js_free_string0(JSRuntime *rt, JSString *str);

/* same as JS_FreeValueRT() but faster */
static inline void js_free_string(JSRuntime *rt, JSString *str)
{
    if (--JS_REF_COUNT(str) <= 0)
        js_free_string0(rt, str);
}

static inline void js_free_string0(JSRuntime *rt, JSString *str)
{
    JSStringSlice *slice;

    if (str->atom_type) {
        JS_FreeAtomStruct(rt, str);
    } else {
#ifdef ENABLE_DUMPS // JS_DUMP_LEAKS
        list_del(&str->link);
#endif
        switch (str->kind) {
        case JS_STRING_KIND_SLICE:
            slice = (void *)&str[1];
            js_free_string(rt, slice->parent); // safe, recurses only 1 level
            break;
        case JS_STRING_KIND_INDIRECT:
            js_free_rt(rt, strv(str));
            break;
        }
        js_free_rt(rt, str);
    }
}

void turbojs_internal_set_runtime_info(JSRuntime *rt, const char *s)
{
    if (rt)
        rt->rt_info = s;
}

void turbojs_internal_free_runtime(JSRuntime *rt)
{
    struct list_head *el, *el1;
    bool leak = false;
    int i;

    rt->in_free = true;
    if (rt->jit_code_cache) {
        TurboJS_CodeCacheDestroy((TurboJSCodeCache *)rt->jit_code_cache);
        rt->jit_code_cache = NULL;
    }
    JS_FreeValueRT(rt, rt->current_exception);

    list_for_each_safe(el, el1, &rt->job_list) {
        JSJobEntry *e = list_entry(el, JSJobEntry, link);
        for(i = 0; i < e->argc; i++)
            JS_FreeValueRT(rt, e->argv[i]);
        js_free_rt(rt, e);
    }
    init_list_head(&rt->job_list);

    JS_RunGC(rt);

#ifdef ENABLE_DUMPS // JS_DUMP_LEAKS
    /* leaking objects */
    if (check_dump_flag(rt, JS_DUMP_LEAKS)) {
        bool header_done;
        JSGCObjectHeader *p;
        int count;

        /* remove the internal refcounts to display only the object
           referenced externally */
        list_for_each(el, &rt->gc_obj_list) {
            p = list_entry(el, JSGCObjectHeader, link);
            JS_GC_MARK(p) = 0;
        }
        gc_decref(rt);

        header_done = false;
        list_for_each(el, &rt->gc_obj_list) {
            p = list_entry(el, JSGCObjectHeader, link);
            if (JS_REF_COUNT(p) != 0) {
                if (!header_done) {
                    printf("Object leaks:\n");
                    JS_DumpObjectHeader(rt);
                    header_done = true;
                }
                JS_DumpGCObject(rt, p);
                leak = true;
            }
        }

        count = 0;
        list_for_each(el, &rt->gc_obj_list) {
            p = list_entry(el, JSGCObjectHeader, link);
            if (JS_REF_COUNT(p) == 0) {
                count++;
            }
        }
        if (count != 0)
            printf("Secondary object leaks: %d\n", count);
    }
#endif

    assert(list_empty(&rt->gc_obj_list));

    /* free the classes */
    for(i = 0; i < rt->class_count; i++) {
        JSClass *cl = &rt->class_array[i];
        if (cl->class_id != 0) {
            JS_FreeAtomRT(rt, cl->class_name);
        }
    }
    js_free_rt(rt, rt->class_array);

#ifdef ENABLE_DUMPS // JS_DUMP_ATOM_LEAKS
    /* only the atoms defined in JS_InitAtoms() should be left */
    if (check_dump_flag(rt, JS_DUMP_ATOM_LEAKS)) {
        bool header_done = false;

        for(i = 0; i < rt->atom_size; i++) {
            JSAtomStruct *p = rt->atom_array[i];
            if (!atom_is_free(p) /* && p->str*/) {
                if (i >= JS_ATOM_END || JS_REF_COUNT(p) != 1) {
                    if (!header_done) {
                        header_done = true;
                        if (rt->rt_info) {
                            printf("%s:1: atom leakage:", rt->rt_info);
                        } else {
                            printf("Atom leaks:\n"
                                   "    %6s %6s %s\n",
                                   "ID", "REFCNT", "NAME");
                        }
                    }
                    if (rt->rt_info) {
                        printf(" ");
                    } else {
                        printf("    %6u %6u ", i, JS_REF_COUNT(p));
                    }
                    switch (p->atom_type) {
                    case JS_ATOM_TYPE_STRING:
                        JS_DumpString(rt, p);
                        break;
                    case JS_ATOM_TYPE_GLOBAL_SYMBOL:
                        printf("Symbol.for(");
                        JS_DumpString(rt, p);
                        printf(")");
                        break;
                    case JS_ATOM_TYPE_SYMBOL:
                        if (p->hash == JS_ATOM_HASH_SYMBOL) {
                            printf("Symbol(");
                            JS_DumpString(rt, p);
                            printf(")");
                        } else {
                            printf("Private(");
                            JS_DumpString(rt, p);
                            printf(")");
                        }
                        break;
                    }
                    if (rt->rt_info) {
                        printf(":%u", JS_REF_COUNT(p));
                    } else {
                        printf("\n");
                    }
                    leak = true;
                }
            }
        }
        if (rt->rt_info && header_done)
            printf("\n");
    }
#endif

    /* free the atoms */
    for(i = 0; i < rt->atom_size; i++) {
        JSAtomStruct *p = rt->atom_array[i];
        if (!atom_is_free(p)) {
#ifdef ENABLE_DUMPS // JS_DUMP_LEAKS
            list_del(&p->link);
#endif
            js_free_rt(rt, p);
        }
    }
    js_free_rt(rt, rt->atom_array);
    js_free_rt(rt, rt->atom_hash);
    js_free_rt(rt, rt->shape_hash);
#ifdef ENABLE_DUMPS // JS_DUMP_LEAKS
    if (check_dump_flag(rt, JS_DUMP_LEAKS) && !list_empty(&rt->string_list)) {
        if (rt->rt_info) {
            printf("%s:1: string leakage:", rt->rt_info);
        } else {
            printf("String leaks:\n"
                   "    %6s %s\n",
                   "REFCNT", "VALUE");
        }
        list_for_each_safe(el, el1, &rt->string_list) {
            JSString *str = list_entry(el, JSString, link);
            if (rt->rt_info) {
                printf(" ");
            } else {
                printf("    %6u ", JS_REF_COUNT(str));
            }
            JS_DumpString(rt, str);
            if (rt->rt_info) {
                printf(":%u", JS_REF_COUNT(str));
            } else {
                printf("\n");
            }
            list_del(&str->link);
            js_free_rt(rt, str);
        }
        if (rt->rt_info)
            printf("\n");
        leak = true;
    }
#endif

    while (rt->finalizers) {
        JSRuntimeFinalizerState *fs = rt->finalizers;
        rt->finalizers = fs->next;
        fs->finalizer(rt, fs->arg);
        js_free_rt(rt, fs);
    }

#ifdef ENABLE_DUMPS // JS_DUMP_LEAKS
    if (check_dump_flag(rt, JS_DUMP_LEAKS)) {
        JSMallocState *s = &rt->malloc_state;
        if (s->malloc_count > 1) {
            if (rt->rt_info)
                printf("%s:1: ", rt->rt_info);
            printf("Memory leak: %zd bytes lost in %zd block%s\n",
                   s->malloc_size - sizeof(JSRuntime),
                   s->malloc_count - 1, &"s"[s->malloc_count == 2]);
            leak = true;
        }
    }
#endif

    leak &= check_dump_flag(rt, JS_ABORT_ON_LEAKS);

    js_arena_free_all(rt);

    {
        JSMallocState *ms = &rt->malloc_state;
        rt->mf.js_free(ms->opaque, rt);
    }

    if (leak)
        abort();
}

JSContext *turbojs_internal_new_context_raw(JSRuntime *rt)
{
    JSContext *ctx;
    int i;

    ctx = js_mallocz_rt(rt, sizeof(JSContext));
    if (!ctx)
        return NULL;
    JS_REF_COUNT(ctx) = 1;
    add_gc_object(rt, &ctx->header, JS_GC_OBJ_TYPE_JS_CONTEXT);

    ctx->class_proto = js_malloc_rt(rt, sizeof(ctx->class_proto[0]) *
                                    rt->class_count);
    if (!ctx->class_proto) {
        js_free_rt(rt, ctx);
        return NULL;
    }
    ctx->rt = rt;
    list_add_tail(&ctx->link, &rt->context_list);
    for(i = 0; i < rt->class_count; i++)
        ctx->class_proto[i] = JS_NULL;
    ctx->array_ctor = JS_NULL;
    ctx->iterator_ctor = JS_NULL;
    ctx->iterator_ctor_getset = JS_NULL;
    ctx->regexp_ctor = JS_NULL;
    ctx->promise_ctor = JS_NULL;
    ctx->error_ctor = JS_NULL;
    ctx->error_back_trace = JS_UNDEFINED;
    ctx->error_prepare_stack = JS_UNDEFINED;
    ctx->error_stack_trace_limit = js_int32(10);
    init_list_head(&ctx->loaded_modules);

    if (JS_AddIntrinsicBasicObjects(ctx)) {
        JS_FreeContext(ctx);
        return NULL;
    }
    return ctx;
}

JSContext *turbojs_internal_new_context(JSRuntime *rt)
{
    JSContext *ctx;
    ctx = JS_NewContextRaw(rt);
    if (!ctx)
        return NULL;

    if (JS_AddIntrinsicBaseObjects(ctx) ||
        JS_AddIntrinsicDate(ctx) ||
        JS_AddIntrinsicEval(ctx) ||
        JS_AddIntrinsicRegExp(ctx) ||
        JS_AddIntrinsicJSON(ctx) ||
        JS_AddIntrinsicProxy(ctx) ||
        JS_AddIntrinsicMapSet(ctx) ||
        JS_AddIntrinsicTypedArrays(ctx) ||
        JS_AddIntrinsicPromise(ctx) ||
        JS_AddIntrinsicWeakRef(ctx) ||
        JS_AddIntrinsicAToB(ctx) ||
        JS_AddPerformance(ctx)) {
        JS_FreeContext(ctx);
        return NULL;
    }

    return ctx;
}

void *turbojs_internal_get_context_opaque(JSContext *ctx)
{
    return ctx->user_opaque;
}

void turbojs_internal_set_context_opaque(JSContext *ctx, void *opaque)
{
    ctx->user_opaque = opaque;
}

/* set the new value and free the old value after (freeing the value
   can reallocate the object data) */
static inline void set_value(JSContext *ctx, JSValue *pval, JSValue new_val)
{
    JSValue old_val;
    old_val = *pval;
    *pval = new_val;
    JS_FreeValue(ctx, old_val);
}

void turbojs_internal_set_class_proto(JSContext *ctx, JSClassID class_id, JSValue obj)
{
    assert(class_id < ctx->rt->class_count);
    set_value(ctx, &ctx->class_proto[class_id], obj);
}

JSValue turbojs_internal_get_class_proto(JSContext *ctx, JSClassID class_id)
{
    assert(class_id < ctx->rt->class_count);
    return js_dup(ctx->class_proto[class_id]);
}

JSValue turbojs_internal_get_function_proto(JSContext *ctx)
{
    return js_dup(ctx->function_proto);
}

typedef enum JSFreeModuleEnum {
    JS_FREE_MODULE_ALL,
    JS_FREE_MODULE_NOT_RESOLVED,
} JSFreeModuleEnum;

/* XXX: would be more efficient with separate module lists */
static void js_free_modules(JSContext *ctx, JSFreeModuleEnum flag)
{
    struct list_head *el, *el1;
    list_for_each_safe(el, el1, &ctx->loaded_modules) {
        JSModuleDef *m = list_entry(el, JSModuleDef, link);
        if (flag == JS_FREE_MODULE_ALL ||
            (flag == JS_FREE_MODULE_NOT_RESOLVED && !m->resolved)) {
            js_free_module_def(ctx, m);
        }
    }
}

JSContext *turbojs_internal_dup_context(JSContext *ctx)
{
    JS_REF_COUNT(ctx)++;
    return ctx;
}

/* used by the GC */
static void JS_MarkContext(JSRuntime *rt, JSContext *ctx,
                           JS_MarkFunc *mark_func)
{
    int i;
    struct list_head *el;

    /* modules are not seen by the GC, so we directly mark the objects
       referenced by each module */
    list_for_each(el, &ctx->loaded_modules) {
        JSModuleDef *m = list_entry(el, JSModuleDef, link);
        js_mark_module_def(rt, m, mark_func);
    }

    JS_MarkValue(rt, ctx->global_obj, mark_func);
    JS_MarkValue(rt, ctx->global_var_obj, mark_func);

    JS_MarkValue(rt, ctx->throw_type_error, mark_func);
    JS_MarkValue(rt, ctx->eval_obj, mark_func);

    JS_MarkValue(rt, ctx->array_proto_values, mark_func);
    for(i = 0; i < JS_NATIVE_ERROR_COUNT; i++) {
        JS_MarkValue(rt, ctx->native_error_proto[i], mark_func);
    }
    JS_MarkValue(rt, ctx->error_ctor, mark_func);
    JS_MarkValue(rt, ctx->error_back_trace, mark_func);
    JS_MarkValue(rt, ctx->error_prepare_stack, mark_func);
    JS_MarkValue(rt, ctx->error_stack_trace_limit, mark_func);
    for(i = 0; i < rt->class_count; i++) {
        JS_MarkValue(rt, ctx->class_proto[i], mark_func);
    }
    JS_MarkValue(rt, ctx->iterator_ctor, mark_func);
    JS_MarkValue(rt, ctx->iterator_ctor_getset, mark_func);
    JS_MarkValue(rt, ctx->async_iterator_proto, mark_func);
    JS_MarkValue(rt, ctx->promise_ctor, mark_func);
    JS_MarkValue(rt, ctx->array_ctor, mark_func);
    JS_MarkValue(rt, ctx->regexp_ctor, mark_func);
    JS_MarkValue(rt, ctx->function_ctor, mark_func);
    JS_MarkValue(rt, ctx->function_proto, mark_func);

    if (ctx->array_shape)
        mark_func(rt, &ctx->array_shape->header);

    if (ctx->arguments_shape)
        mark_func(rt, &ctx->arguments_shape->header);

    if (ctx->mapped_arguments_shape)
        mark_func(rt, &ctx->mapped_arguments_shape->header);

    if (ctx->regexp_shape)
        mark_func(rt, &ctx->regexp_shape->header);

    if (ctx->regexp_result_shape)
        mark_func(rt, &ctx->regexp_result_shape->header);
}

void turbojs_internal_free_context(JSContext *ctx)
{
    JSRuntime *rt = ctx->rt;
    int i;

    if (--JS_REF_COUNT(ctx) > 0)
        return;
    assert(JS_REF_COUNT(ctx) == 0);

#ifdef ENABLE_DUMPS // JS_DUMP_ATOMS
    if (check_dump_flag(rt, JS_DUMP_ATOMS))
        JS_DumpAtoms(ctx->rt);
#endif
#ifdef ENABLE_DUMPS // JS_DUMP_SHAPES
    if (check_dump_flag(rt, JS_DUMP_SHAPES))
        JS_DumpShapes(ctx->rt);
#endif
#ifdef ENABLE_DUMPS // JS_DUMP_OBJECTS
    if (check_dump_flag(rt, JS_DUMP_OBJECTS)) {
        struct list_head *el;
        JSGCObjectHeader *p;
        printf("JSObjects: {\n");
        JS_DumpObjectHeader(ctx->rt);
        list_for_each(el, &rt->gc_obj_list) {
            p = list_entry(el, JSGCObjectHeader, link);
            JS_DumpGCObject(rt, p);
        }
        printf("}\n");
    }
#endif
#ifdef ENABLE_DUMPS // JS_DUMP_MEM
    if (check_dump_flag(rt, JS_DUMP_MEM)) {
        JSMemoryUsage stats;
        JS_ComputeMemoryUsage(rt, &stats);
        JS_DumpMemoryUsage(stdout, &stats, rt);
    }
#endif

    js_free_modules(ctx, JS_FREE_MODULE_ALL);

    JS_FreeValue(ctx, ctx->global_obj);
    JS_FreeValue(ctx, ctx->global_var_obj);

    JS_FreeValue(ctx, ctx->throw_type_error);
    JS_FreeValue(ctx, ctx->eval_obj);

    JS_FreeValue(ctx, ctx->array_proto_values);
    for(i = 0; i < JS_NATIVE_ERROR_COUNT; i++) {
        JS_FreeValue(ctx, ctx->native_error_proto[i]);
    }
    JS_FreeValue(ctx, ctx->error_ctor);
    JS_FreeValue(ctx, ctx->error_back_trace);
    JS_FreeValue(ctx, ctx->error_prepare_stack);
    JS_FreeValue(ctx, ctx->error_stack_trace_limit);
    for(i = 0; i < rt->class_count; i++) {
        JS_FreeValue(ctx, ctx->class_proto[i]);
    }
    js_free_rt(rt, ctx->class_proto);
    JS_FreeValue(ctx, ctx->iterator_ctor);
    JS_FreeValue(ctx, ctx->iterator_ctor_getset);
    JS_FreeValue(ctx, ctx->async_iterator_proto);
    JS_FreeValue(ctx, ctx->promise_ctor);
    JS_FreeValue(ctx, ctx->array_ctor);
    JS_FreeValue(ctx, ctx->regexp_ctor);
    JS_FreeValue(ctx, ctx->function_ctor);
    JS_FreeValue(ctx, ctx->function_proto);

    js_free_shape_null(ctx->rt, ctx->array_shape);
    js_free_shape_null(ctx->rt, ctx->arguments_shape);
    js_free_shape_null(ctx->rt, ctx->mapped_arguments_shape);
    js_free_shape_null(ctx->rt, ctx->regexp_shape);
    js_free_shape_null(ctx->rt, ctx->regexp_result_shape);

    list_del(&ctx->link);
    remove_gc_object(&ctx->header);
    js_free_rt(ctx->rt, ctx);
}

JSRuntime *turbojs_internal_get_context_runtime(JSContext *ctx)
{
    return ctx->rt;
}

static void update_stack_limit(JSRuntime *rt)
{
#if defined(__wasi__)
    rt->stack_limit = 0; /* no limit */
#else
    if (rt->stack_size == 0) {
        rt->stack_limit = 0; /* no limit */
    } else {
        rt->stack_limit = rt->stack_top - rt->stack_size;
    }
#endif
}

void turbojs_internal_set_max_stack_size(JSRuntime *rt, size_t stack_size)
{
    rt->stack_size = stack_size;
    update_stack_limit(rt);
}

void turbojs_internal_update_stack_top(JSRuntime *rt)
{
    rt->stack_top = js_get_stack_pointer();
    update_stack_limit(rt);
}

static inline bool is_strict_mode(JSContext *ctx)
{
    JSStackFrame *sf = ctx->rt->current_stack_frame;
    return sf && sf->is_strict_mode;
}


void turbojs_internal_set_jit_threshold(JSRuntime *rt, uint32_t threshold)
{
    if (rt)
        rt->jit_compile_threshold = threshold ? threshold : 1;
}

TurboJSRuntimeJITStats turbojs_internal_get_jit_stats(const JSRuntime *rt)
{
    TurboJSRuntimeJITStats out;
    TurboJSCodeCacheStats cache_stats;
    memset(&out, 0, sizeof(out));
    if (!rt)
        return out;
    out.interpreted_calls = rt->jit_interpreted_calls;
    out.native_calls = rt->jit_native_calls;
    out.guard_failures = rt->jit_guard_failures;
    if (rt->jit_code_cache) {
        cache_stats = TurboJS_CodeCacheGetStats((const TurboJSCodeCache *)rt->jit_code_cache);
        out.cache_hits = cache_stats.hits;
        out.cache_misses = cache_stats.misses;
        out.compilations = cache_stats.compilations;
        out.evictions = cache_stats.evictions;
        out.cache_entries = cache_stats.entry_count;
        out.native_code_bytes = cache_stats.code_bytes;
    }
    return out;
}

void turbojs_internal_clear_jit_cache(JSRuntime *rt)
{
    if (rt && rt->jit_code_cache)
        TurboJS_CodeCacheClear((TurboJSCodeCache *)rt->jit_code_cache);
}
