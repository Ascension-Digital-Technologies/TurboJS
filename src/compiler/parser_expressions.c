/* Engine domain source: compiler/parser_compiler.inc -> parser_expressions.
 * Ownership: compiler subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static JSModuleDef *js_new_module_def(JSContext *ctx, JSAtom name)
{
    JSModuleDef *m;
    m = js_mallocz(ctx, sizeof(*m));
    if (!m) {
        JS_FreeAtom(ctx, name);
        return NULL;
    }
    JS_REF_COUNT(m) = 1;
    m->module_name = name;
    m->module_ns = JS_UNDEFINED;
    m->func_obj = JS_UNDEFINED;
    m->eval_exception = JS_UNDEFINED;
    m->meta_obj = JS_UNDEFINED;
    m->promise = JS_UNDEFINED;
    m->resolving_funcs[0] = JS_UNDEFINED;
    m->resolving_funcs[1] = JS_UNDEFINED;
    m->private_value = JS_UNDEFINED;
    list_add_tail(&m->link, &ctx->loaded_modules);
    return m;
}

static void js_mark_module_def(JSRuntime *rt, JSModuleDef *m,
                               JS_MarkFunc *mark_func)
{
    int i;

    for(i = 0; i < m->req_module_entries_count; i++) {
        JSReqModuleEntry *rme = &m->req_module_entries[i];
        JS_MarkValue(rt, rme->attributes, mark_func);
    }

    for(i = 0; i < m->export_entries_count; i++) {
        JSExportEntry *me = &m->export_entries[i];
        if (me->export_type == JS_EXPORT_TYPE_LOCAL &&
            me->u.local.var_ref) {
            mark_func(rt, &me->u.local.var_ref->header);
        }
    }

    JS_MarkValue(rt, m->module_ns, mark_func);
    JS_MarkValue(rt, m->func_obj, mark_func);
    JS_MarkValue(rt, m->eval_exception, mark_func);
    JS_MarkValue(rt, m->meta_obj, mark_func);
    JS_MarkValue(rt, m->promise, mark_func);
    JS_MarkValue(rt, m->resolving_funcs[0], mark_func);
    JS_MarkValue(rt, m->resolving_funcs[1], mark_func);
    JS_MarkValue(rt, m->private_value, mark_func);
}

static void js_free_module_def(JSContext *ctx, JSModuleDef *m)
{
    int i;

    JS_FreeAtom(ctx, m->module_name);

    for(i = 0; i < m->req_module_entries_count; i++) {
        JSReqModuleEntry *rme = &m->req_module_entries[i];
        JS_FreeAtom(ctx, rme->module_name);
        JS_FreeValue(ctx, rme->attributes);
    }
    js_free(ctx, m->req_module_entries);

    for(i = 0; i < m->export_entries_count; i++) {
        JSExportEntry *me = &m->export_entries[i];
        if (me->export_type == JS_EXPORT_TYPE_LOCAL)
            free_var_ref(ctx->rt, me->u.local.var_ref);
        JS_FreeAtom(ctx, me->export_name);
        JS_FreeAtom(ctx, me->local_name);
    }
    js_free(ctx, m->export_entries);

    js_free(ctx, m->star_export_entries);

    for(i = 0; i < m->import_entries_count; i++) {
        JSImportEntry *mi = &m->import_entries[i];
        JS_FreeAtom(ctx, mi->import_name);
    }
    js_free(ctx, m->import_entries);
    js_free(ctx, m->async_parent_modules);

    JS_FreeValue(ctx, m->module_ns);
    JS_FreeValue(ctx, m->func_obj);
    JS_FreeValue(ctx, m->eval_exception);
    JS_FreeValue(ctx, m->meta_obj);
    JS_FreeValue(ctx, m->promise);
    JS_FreeValue(ctx, m->resolving_funcs[0]);
    JS_FreeValue(ctx, m->resolving_funcs[1]);
    JS_FreeValue(ctx, m->private_value);
    list_del(&m->link);
    js_free(ctx, m);
}

#ifndef TURBOJS_DISABLE_PARSER

static int add_req_module_entry(JSContext *ctx, JSModuleDef *m,
                                JSAtom module_name)
{
    JSReqModuleEntry *rme;
    int i;

    /* no need to add the module request if it is already present */
    for(i = 0; i < m->req_module_entries_count; i++) {
        rme = &m->req_module_entries[i];
        if (rme->module_name == module_name)
            return i;
    }

    if (js_resize_array(ctx, (void **)&m->req_module_entries,
                        sizeof(JSReqModuleEntry),
                        &m->req_module_entries_size,
                        m->req_module_entries_count + 1))
        return -1;
    rme = &m->req_module_entries[m->req_module_entries_count++];
    rme->module_name = JS_DupAtom(ctx, module_name);
    rme->module = NULL;
    rme->attributes = JS_UNDEFINED;
    return i;
}

#endif // TURBOJS_DISABLE_PARSER

