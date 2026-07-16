/* Engine domain source: modules/eval_modules.inc -> eval_api.
 * Ownership: modules subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static void js_parse_init(JSContext *ctx, JSParseState *s,
                          const char *input, size_t input_len,
                          const char *filename, int line)
{
    memset(s, 0, sizeof(*s));
    s->ctx = ctx;
    s->filename = filename;
    s->line_num = line;
    s->col_num = 1;
    s->buf_start = s->buf_ptr = (const uint8_t *)input;
    s->buf_end = s->buf_ptr + input_len;
    s->mark = s->buf_ptr + min_int(1, input_len);
    s->eol = s->buf_ptr;
    s->token.val = ' ';
    s->token.line_num = 1;
    s->token.col_num = 1;
}

static JSValue JS_EvalFunctionInternal(JSContext *ctx, JSValue fun_obj,
                                       JSValueConst this_obj,
                                       JSVarRef **var_refs, JSStackFrame *sf)
{
    JSValue ret_val;
    uint32_t tag;

    tag = JS_VALUE_GET_TAG(fun_obj);
    if (tag == JS_TAG_FUNCTION_BYTECODE) {
        fun_obj = js_closure(ctx, fun_obj, var_refs, sf);
        ret_val = JS_CallFree(ctx, fun_obj, this_obj, 0, NULL);
    } else if (tag == JS_TAG_MODULE) {
        JSModuleDef *m;
        m = JS_VALUE_GET_PTR(fun_obj);
        /* the module refcount should be >= 2 */
        JS_FreeValue(ctx, fun_obj);
        if (js_create_module_function(ctx, m) < 0)
            goto fail;
        if (js_link_module(ctx, m) < 0)
            goto fail;
        ret_val = js_evaluate_module(ctx, m);
        if (JS_IsException(ret_val)) {
        fail:
            return JS_EXCEPTION;
        }
    } else {
        JS_FreeValue(ctx, fun_obj);
        ret_val = JS_ThrowTypeError(ctx, "bytecode function expected");
    }
    return ret_val;
}

JSValue JS_EvalFunction(JSContext *ctx, JSValue fun_obj)
{
    return JS_EvalFunctionInternal(ctx, fun_obj, ctx->global_obj, NULL, NULL);
}

#ifndef QJS_DISABLE_PARSER

/* 'input' must be zero terminated i.e. input[input_len] = '\0'. */
/* `export_name` and `input` may be pure ASCII or UTF-8 encoded */
static JSValue __JS_EvalInternal(JSContext *ctx, JSValueConst this_obj,
                                 const char *input, size_t input_len,
                                 const char *filename, int line, int flags, int scope_idx)
{
    JSParseState s1, *s = &s1;
    int err, eval_type;
    JSValue fun_obj, ret_val;
    JSStackFrame *sf;
    JSVarRef **var_refs;
    JSFunctionBytecode *b;
    JSFunctionDef *fd;
    JSModuleDef *m;
    bool is_strict_mode;

    js_parse_init(ctx, s, input, input_len, filename, line);
    skip_shebang(&s->buf_ptr, s->buf_end);

    eval_type = flags & JS_EVAL_TYPE_MASK;
    m = NULL;
    if (eval_type == JS_EVAL_TYPE_DIRECT) {
        JSObject *p;
        sf = ctx->rt->current_stack_frame;
        assert(sf != NULL);
        assert(JS_VALUE_GET_TAG(sf->cur_func) == JS_TAG_OBJECT);
        p = JS_VALUE_GET_OBJ(sf->cur_func);
        assert(js_class_has_bytecode(p->class_id));
        b = p->u.func.function_bytecode;
        var_refs = p->u.func.var_refs;
        is_strict_mode = b->is_strict_mode;
    } else {
        sf = NULL;
        b = NULL;
        var_refs = NULL;
        is_strict_mode = (flags & JS_EVAL_FLAG_STRICT) != 0;
        if (eval_type == JS_EVAL_TYPE_MODULE) {
            JSAtom module_name = JS_NewAtom(ctx, filename);
            if (module_name == JS_ATOM_NULL)
                return JS_EXCEPTION;
            m = js_new_module_def(ctx, module_name);
            if (!m)
                return JS_EXCEPTION;
            is_strict_mode = true;
        }
    }
    fd = js_new_function_def(ctx, NULL, true, false, filename, line, 1);
    if (!fd)
        goto fail1;
    s->cur_func = fd;
    fd->eval_type = eval_type;
    fd->has_this_binding = (eval_type != JS_EVAL_TYPE_DIRECT);
    fd->backtrace_barrier = ((flags & JS_EVAL_FLAG_BACKTRACE_BARRIER) != 0);
    if (eval_type == JS_EVAL_TYPE_DIRECT) {
        fd->new_target_allowed = b->new_target_allowed;
        fd->super_call_allowed = b->super_call_allowed;
        fd->super_allowed = b->super_allowed;
        fd->arguments_allowed = b->arguments_allowed;
    } else {
        fd->new_target_allowed = false;
        fd->super_call_allowed = false;
        fd->super_allowed = false;
        fd->arguments_allowed = true;
    }
    fd->is_strict_mode = is_strict_mode;
    fd->func_name = JS_DupAtom(ctx, JS_ATOM__eval_);
    if (b) {
        if (add_closure_variables(ctx, fd, b, scope_idx))
            goto fail;
    }
    fd->module = m;
    if (m != NULL || (flags & JS_EVAL_FLAG_ASYNC)) {
        fd->in_function_body = true;
        fd->func_kind = JS_FUNC_ASYNC;
    }
    s->is_module = (m != NULL);
    s->allow_html_comments = !s->is_module;

    push_scope(s); /* body scope */
    fd->body_scope = fd->scope_level;

    err = js_parse_program(s);
    if (err) {
    fail:
        free_token(s, &s->token);
        js_free_function_def(ctx, fd);
        goto fail1;
    }

    if (m != NULL)
        m->has_tla = fd->has_await;

    /* create the function object and all the enclosed functions */
    fun_obj = js_create_function(ctx, fd);
    if (JS_IsException(fun_obj))
        goto fail1;
    /* Could add a flag to avoid resolution if necessary */
    if (m) {
        m->func_obj = fun_obj;
        if (js_resolve_module(ctx, m) < 0)
            goto fail1;
        fun_obj = JS_NewModuleValue(ctx, m);
    }
    if (flags & JS_EVAL_FLAG_COMPILE_ONLY) {
        ret_val = fun_obj;
    } else {
        ret_val = JS_EvalFunctionInternal(ctx, fun_obj, this_obj, var_refs, sf);
    }
    return ret_val;
 fail1:
    /* XXX: should free all the unresolved dependencies */
    if (m)
        js_free_module_def(ctx, m);
    return JS_EXCEPTION;
}

#endif // QJS_DISABLE_PARSER

/* the indirection is needed to make 'eval' optional */
static JSValue JS_EvalInternal(JSContext *ctx, JSValueConst this_obj,
                               const char *input, size_t input_len,
                               const char *filename, int line, int flags, int scope_idx)
{
    JSRuntime *rt = ctx->rt;

    if (unlikely(!ctx->eval_internal)) {
        return JS_ThrowTypeError(ctx, "eval is not supported");
    }
    if (!rt->current_stack_frame) {
        JS_FreeValueRT(rt, ctx->error_back_trace);
        ctx->error_back_trace = JS_UNDEFINED;
    }
    return ctx->eval_internal(ctx, this_obj, input, input_len, filename, line,
                              flags, scope_idx);
}

static JSValue JS_EvalObject(JSContext *ctx, JSValueConst this_obj,
                             JSValueConst val, int flags, int scope_idx)
{
    JSValue ret;
    const char *str;
    size_t len;

    if (!JS_IsString(val))
        return js_dup(val);
    str = JS_ToCStringLen(ctx, &len, val);
    if (!str)
        return JS_EXCEPTION;
    ret = JS_EvalInternal(ctx, this_obj, str, len, "<input>", 1, flags, scope_idx);
    JS_FreeCString(ctx, str);
    return ret;

}

JSValue JS_EvalThis(JSContext *ctx, JSValueConst this_obj,
                    const char *input, size_t input_len,
                    const char *filename, int eval_flags)
{
    JSEvalOptions options = {
        .version = JS_EVAL_OPTIONS_VERSION,
        .filename = filename,
        .line_num = 1,
        .eval_flags = eval_flags
    };
    return JS_EvalThis2(ctx, this_obj, input, input_len, &options);
}

JSValue JS_EvalThis2(JSContext *ctx, JSValueConst this_obj,
                     const char *input, size_t input_len,
                     JSEvalOptions *options)
{
    const char *filename = "<unnamed>";
    int line = 1;
    int eval_flags = 0;
    if (options) {
        if (options->version != JS_EVAL_OPTIONS_VERSION)
            return JS_ThrowInternalError(ctx, "bad JSEvalOptions version");
        if (options->filename)
            filename = options->filename;
        if (options->line_num != 0)
            line = options->line_num;
        eval_flags = options->eval_flags;
    }
    JSValue ret;

    assert((eval_flags & JS_EVAL_TYPE_MASK) == JS_EVAL_TYPE_GLOBAL ||
           (eval_flags & JS_EVAL_TYPE_MASK) == JS_EVAL_TYPE_MODULE);
    ret = JS_EvalInternal(ctx, this_obj, input, input_len, filename, line,
                          eval_flags, -1);
    return ret;
}

JSValue JS_Eval(JSContext *ctx, const char *input, size_t input_len,
                const char *filename, int eval_flags)
{
    JSEvalOptions options = {
        .version = JS_EVAL_OPTIONS_VERSION,
        .filename = filename,
        .line_num = 1,
        .eval_flags = eval_flags
    };
    return JS_EvalThis2(ctx, ctx->global_obj, input, input_len, &options);
}

JSValue JS_Eval2(JSContext *ctx, const char *input, size_t input_len, JSEvalOptions *options)
{
    return JS_EvalThis2(ctx, ctx->global_obj, input, input_len, options);
}

int JS_ResolveModule(JSContext *ctx, JSValueConst obj)
{
    if (JS_VALUE_GET_TAG(obj) == JS_TAG_MODULE) {
        JSModuleDef *m = JS_VALUE_GET_PTR(obj);
        if (js_resolve_module(ctx, m) < 0) {
            js_free_modules(ctx, JS_FREE_MODULE_NOT_RESOLVED);
            return -1;
        }
    }
    return 0;
}

/*******************************************************************/
