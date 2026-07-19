/* Engine domain source: modules/eval_modules.inc -> module_linker.
 * Ownership: modules subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static int js_execute_async_module(JSContext *ctx, JSModuleDef *m)
{
    JSValue promise, m_obj;
    JSValue resolve_funcs[2], ret_val;
    promise = js_async_function_call(ctx, m->func_obj, JS_UNDEFINED, 0, NULL, 0);
    if (JS_IsException(promise))
        return -1;
    m_obj = JS_NewModuleValue(ctx, m);
    resolve_funcs[0] = JS_NewCFunctionData(ctx, js_async_module_execution_fulfilled, 0, 0, 1, vc(&m_obj));
    resolve_funcs[1] = JS_NewCFunctionData(ctx, js_async_module_execution_rejected, 0, 0, 1, vc(&m_obj));
    ret_val = js_promise_then(ctx, promise, 2, vc(resolve_funcs));
    JS_FreeValue(ctx, ret_val);
    JS_FreeValue(ctx, m_obj);
    JS_FreeValue(ctx, resolve_funcs[0]);
    JS_FreeValue(ctx, resolve_funcs[1]);
    JS_FreeValue(ctx, promise);
    return 0;
}

/* return < 0 in case of exception. *pvalue contains the exception. */
static int js_execute_sync_module(JSContext *ctx, JSModuleDef *m,
                                  JSValue *pvalue)
{
    if (m->init_func) {
        /* C module init : no asynchronous execution */
        if (m->init_func(ctx, m) < 0)
            goto fail;
    } else {
        JSValue promise;
        JSPromiseStateEnum state;

        promise = js_async_function_call(ctx, m->func_obj, JS_UNDEFINED, 0, NULL, 0);
        if (JS_IsException(promise))
            goto fail;
        state = JS_PromiseState(ctx, promise);
        if (state == JS_PROMISE_FULFILLED) {
            JS_FreeValue(ctx, promise);
        } else if (state == JS_PROMISE_REJECTED) {
            *pvalue = JS_PromiseResult(ctx, promise);
            JS_FreeValue(ctx, promise);
            return -1;
        } else {
            JS_FreeValue(ctx, promise);
            JS_ThrowTypeError(ctx, "promise is pending");
        fail:
            *pvalue = JS_GetException(ctx);
            return -1;
        }
    }
    *pvalue = JS_UNDEFINED;
    return 0;
}

/* spec: InnerModuleEvaluation. Return (index, JS_UNDEFINED) or (-1,
   exception) */
static int js_inner_module_evaluation(JSContext *ctx, JSModuleDef *m,
                                      int index, JSModuleDef **pstack_top,
                                      JSValue *pvalue)
{
    JSModuleDef *m1;
    int i;

    if (js_check_stack_overflow(ctx->rt, 0)) {
        JS_ThrowStackOverflow(ctx);
        *pvalue = JS_GetException(ctx);
        return -1;
    }

#ifdef ENABLE_DUMPS // JS_DUMP_MODULE_RESOLVE
    if (check_dump_flag(ctx->rt, JS_DUMP_MODULE_RESOLVE)) {
        char buf1[ATOM_GET_STR_BUF_SIZE];
        printf("js_inner_module_evaluation '%s':\n", JS_AtomGetStr(ctx, buf1, sizeof(buf1), m->module_name));
    }
#endif

    if (m->status == JS_MODULE_STATUS_EVALUATING_ASYNC ||
        m->status == JS_MODULE_STATUS_EVALUATED) {
        if (m->eval_has_exception) {
            *pvalue = js_dup(m->eval_exception);
            return -1;
        } else {
            *pvalue = JS_UNDEFINED;
            return index;
        }
    }
    if (m->status == JS_MODULE_STATUS_EVALUATING) {
        *pvalue = JS_UNDEFINED;
        return index;
    }
    assert(m->status == JS_MODULE_STATUS_LINKED);

    m->status = JS_MODULE_STATUS_EVALUATING;
    m->dfs_index = index;
    m->dfs_ancestor_index = index;
    m->pending_async_dependencies = 0;
    index++;
    /* push 'm' on stack */
    m->stack_prev = *pstack_top;
    *pstack_top = m;

    for(i = 0; i < m->req_module_entries_count; i++) {
        JSReqModuleEntry *rme = &m->req_module_entries[i];
        m1 = rme->module;
        index = js_inner_module_evaluation(ctx, m1, index, pstack_top, pvalue);
        if (index < 0)
            return -1;
        assert(m1->status == JS_MODULE_STATUS_EVALUATING ||
               m1->status == JS_MODULE_STATUS_EVALUATING_ASYNC ||
               m1->status == JS_MODULE_STATUS_EVALUATED);
        if (m1->status == JS_MODULE_STATUS_EVALUATING) {
            m->dfs_ancestor_index = min_int(m->dfs_ancestor_index,
                                            m1->dfs_ancestor_index);
        } else {
            m1 = m1->cycle_root;
            assert(m1->status == JS_MODULE_STATUS_EVALUATING_ASYNC ||
                   m1->status == JS_MODULE_STATUS_EVALUATED);
            if (m1->eval_has_exception) {
                *pvalue = js_dup(m1->eval_exception);
                return -1;
            }
        }
        if (m1->async_evaluation) {
            m->pending_async_dependencies++;
            if (js_resize_array(ctx, (void **)&m1->async_parent_modules, sizeof(m1->async_parent_modules[0]), &m1->async_parent_modules_size, m1->async_parent_modules_count + 1)) {
                *pvalue = JS_GetException(ctx);
                return -1;
            }
            m1->async_parent_modules[m1->async_parent_modules_count++] = m;
        }
    }

    if (m->pending_async_dependencies > 0) {
        assert(!m->async_evaluation);
        m->async_evaluation = true;
        m->async_evaluation_timestamp =
            ctx->rt->module_async_evaluation_next_timestamp++;
    } else if (m->has_tla) {
        assert(!m->async_evaluation);
        m->async_evaluation = true;
        m->async_evaluation_timestamp =
            ctx->rt->module_async_evaluation_next_timestamp++;
        js_execute_async_module(ctx, m);
    } else {
        if (js_execute_sync_module(ctx, m, pvalue) < 0)
            return -1;
    }

    assert(m->dfs_ancestor_index <= m->dfs_index);
    if (m->dfs_index == m->dfs_ancestor_index) {
        for(;;) {
            /* pop m1 from stack */
            m1 = *pstack_top;
            *pstack_top = m1->stack_prev;
            if (!m1->async_evaluation) {
                m1->status = JS_MODULE_STATUS_EVALUATED;
            } else {
                m1->status = JS_MODULE_STATUS_EVALUATING_ASYNC;
            }
            /* spec bug: cycle_root must be assigned before the test */
            m1->cycle_root = m;
            if (m1 == m)
                break;
        }
    }
    *pvalue = JS_UNDEFINED;
    return index;
}

/* Run the <eval> function of the module and of all its requested
   modules. Return a promise or an exception. */
static JSValue js_evaluate_module(JSContext *ctx, JSModuleDef *m)
{
    JSModuleDef *m1, *stack_top;
    JSValue ret_val, result;

    assert(m->status == JS_MODULE_STATUS_LINKED ||
           m->status == JS_MODULE_STATUS_EVALUATING_ASYNC ||
           m->status == JS_MODULE_STATUS_EVALUATED);
    if (m->status == JS_MODULE_STATUS_EVALUATING_ASYNC ||
        m->status == JS_MODULE_STATUS_EVALUATED) {
        m = m->cycle_root;
    }
    /* a promise may be created only on the cycle_root of a cycle */
    if (!JS_IsUndefined(m->promise))
        return js_dup(m->promise);
    m->promise = JS_NewPromiseCapability(ctx, m->resolving_funcs);
    if (JS_IsException(m->promise))
        return JS_EXCEPTION;

    stack_top = NULL;
    if (js_inner_module_evaluation(ctx, m, 0, &stack_top, &result) < 0) {
        while (stack_top != NULL) {
            m1 = stack_top;
            assert(m1->status == JS_MODULE_STATUS_EVALUATING);
            m1->status = JS_MODULE_STATUS_EVALUATED;
            m1->eval_has_exception = true;
            m1->eval_exception = js_dup(result);
            m1->cycle_root = m; /* spec bug: should be present */
            stack_top = m1->stack_prev;
        }
        JS_FreeValue(ctx, result);
        assert(m->status == JS_MODULE_STATUS_EVALUATED);
        assert(m->eval_has_exception);
        ret_val = JS_Call(ctx, m->resolving_funcs[1], JS_UNDEFINED,
                          1, vc(&m->eval_exception));
        JS_FreeValue(ctx, ret_val);
    } else {
        assert(m->status == JS_MODULE_STATUS_EVALUATING_ASYNC ||
               m->status == JS_MODULE_STATUS_EVALUATED);
        assert(!m->eval_has_exception);
        if (!m->async_evaluation) {
            assert(m->status == JS_MODULE_STATUS_EVALUATED);
            JSValueConst value = JS_UNDEFINED;
            ret_val = JS_Call(ctx, m->resolving_funcs[0], JS_UNDEFINED,
                              1, &value);
            JS_FreeValue(ctx, ret_val);
        }
        assert(stack_top == NULL);
    }
    return js_dup(m->promise);
}

#ifndef TURBOJS_DISABLE_PARSER

/* Parse 'with { key: "value", ... }' clause for import attributes.
   rme->attributes is set to JS_UNDEFINED if no 'with' clause or an object
   containing the attributes as key/value pairs. If rme->attributes is already
   set (from a previous import of the same module), we still parse the tokens
   but skip adding to the object since they should be the same. */
static __exception int js_parse_with_clause(JSParseState *s, JSReqModuleEntry *rme)
{
    JSContext *ctx = s->ctx;
    JSAtom key;
    int ret;
    bool already_set;

    if (s->token.val != TOK_WITH)
        return 0; /* no 'with' clause */

    /* If attributes already set from previous import of same module,
       just parse to consume tokens but don't modify the object. */
    already_set = !JS_IsUndefined(rme->attributes);

    if (next_token(s))
        return -1;
    if (js_parse_expect(s, '{'))
        return -1;
    while (s->token.val != '}') {
        if (s->token.val == TOK_STRING) {
            key = JS_ValueToAtom(ctx, s->token.u.str.str);
            if (key == JS_ATOM_NULL)
                return -1;
        } else {
            if (!token_is_ident(s->token.val)) {
                js_parse_error(s, "identifier expected");
                return -1;
            }
            key = JS_DupAtom(ctx, s->token.u.ident.atom);
        }
        if (next_token(s)) {
            JS_FreeAtom(ctx, key);
            return -1;
        }
        if (js_parse_expect(s, ':')) {
            JS_FreeAtom(ctx, key);
            return -1;
        }
        if (s->token.val != TOK_STRING) {
            js_parse_error(s, "string expected");
            JS_FreeAtom(ctx, key);
            return -1;
        }
        if (!already_set) {
            if (JS_IsUndefined(rme->attributes)) {
                JSValue attributes = JS_NewObjectProto(ctx, JS_NULL);
                if (JS_IsException(attributes)) {
                    JS_FreeAtom(ctx, key);
                    return -1;
                }
                rme->attributes = attributes;
            }
            /* check for duplicate keys */
            ret = JS_HasProperty(ctx, rme->attributes, key);
            if (ret != 0) {
                if (ret < 0) {
                    JS_FreeAtom(ctx, key);
                    return -1;
                } else {
                    js_parse_error(s, "duplicate with key");
                    JS_FreeAtom(ctx, key);
                    return -1;
                }
            }
            ret = JS_DefinePropertyValue(ctx, rme->attributes, key,
                                         js_dup(s->token.u.str.str), JS_PROP_C_W_E);
            if (ret < 0) {
                JS_FreeAtom(ctx, key);
                return -1;
            }
        }
        JS_FreeAtom(ctx, key);
        if (next_token(s))
            return -1;
        if (s->token.val != '}') {
            if (js_parse_expect(s, ','))
                return -1;
        }
    }
    /* check attributes validity if checker function provided */
    if (!already_set && !JS_IsUndefined(rme->attributes) &&
        ctx->rt->module_check_attrs &&
        ctx->rt->module_check_attrs(ctx, ctx->rt->module_loader_opaque, rme->attributes) < 0) {
        return -1;
    }
    return js_parse_expect(s, '}');
}

/* return the module index in m->req_module_entries[] or < 0 if error */
static __exception int js_parse_from_clause(JSParseState *s, JSModuleDef *m)
{
    JSAtom module_name;
    int idx;

    if (!token_is_pseudo_keyword(s, JS_ATOM_from)) {
        js_parse_error(s, "from clause expected");
        return -1;
    }
    if (next_token(s))
        return -1;
    if (s->token.val != TOK_STRING) {
        js_parse_error(s, "string expected");
        return -1;
    }
    module_name = JS_ValueToAtom(s->ctx, s->token.u.str.str);
    if (module_name == JS_ATOM_NULL)
        return -1;
    if (next_token(s)) {
        JS_FreeAtom(s->ctx, module_name);
        return -1;
    }

    idx = add_req_module_entry(s->ctx, m, module_name);
    JS_FreeAtom(s->ctx, module_name);
    if (idx < 0)
        return -1;
    if (s->token.val == TOK_WITH) {
        if (js_parse_with_clause(s, &m->req_module_entries[idx]))
            return -1;
    }
    return idx;
}

static bool has_unmatched_surrogate(const uint16_t *s, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (is_lo_surrogate(s[i]))
            return true;
        if (!is_hi_surrogate(s[i]))
            continue;
        if (++i == n)
            return true;
        if (!is_lo_surrogate(s[i]))
            return true;
    }
    return false;
}

static __exception int js_parse_export(JSParseState *s)
{
    JSContext *ctx = s->ctx;
    JSModuleDef *m = s->cur_func->module;
    JSAtom local_name, export_name;
    int first_export, idx, i, tok;
    JSExportEntry *me;

    if (next_token(s))
        return -1;

    tok = s->token.val;
    if (tok == TOK_CLASS) {
        return js_parse_class(s, false, JS_PARSE_EXPORT_NAMED);
    } else if (tok == TOK_FUNCTION ||
               (token_is_pseudo_keyword(s, JS_ATOM_async) &&
                peek_token(s, true) == TOK_FUNCTION)) {
        return js_parse_function_decl2(s, JS_PARSE_FUNC_STATEMENT,
                                       JS_FUNC_NORMAL, JS_ATOM_NULL,
                                       s->token.ptr,
                                       s->token.line_num,
                                       s->token.col_num,
                                       JS_PARSE_EXPORT_NAMED, NULL);
    }

    if (next_token(s))
        return -1;

    switch(tok) {
    case '{':
        first_export = m->export_entries_count;
        bool has_string_binding = false;
        while (s->token.val != '}') {
            if (token_is_ident(s->token.val)) {
                local_name = JS_DupAtom(ctx, s->token.u.ident.atom);
            } else if (s->token.val == TOK_STRING) {
                local_name = JS_ValueToAtom(ctx, s->token.u.str.str);
                if (local_name == JS_ATOM_NULL)
                    return -1;
                has_string_binding = true;
            } else {
                return js_parse_error(s, "identifier or string expected");
            }
            export_name = JS_ATOM_NULL;
            if (next_token(s))
                goto fail;
            if (token_is_pseudo_keyword(s, JS_ATOM_as)) {
                if (next_token(s))
                    goto fail;
                if (token_is_ident(s->token.val)) {
                    export_name = JS_DupAtom(ctx, s->token.u.ident.atom);
                } else if (s->token.val == TOK_STRING) {
                    JSString *p = JS_VALUE_GET_STRING(s->token.u.str.str);
                    if (p->is_wide_char && has_unmatched_surrogate(str16(p), p->len)) {
                        js_parse_error(s, "illegal export name");
                        return -1;
                    }
                    export_name = JS_ValueToAtom(ctx, s->token.u.str.str);
                    if (export_name == JS_ATOM_NULL) {
                        return -1;
                    }
                } else {
                    js_parse_error(s, "identifier or string expected");
                    goto fail;
                }
                if (next_token(s)) {
                fail:
                    JS_FreeAtom(ctx, local_name);
                fail1:
                    JS_FreeAtom(ctx, export_name);
                    return -1;
                }
            } else {
                export_name = JS_DupAtom(ctx, local_name);
            }
            me = add_export_entry(s, m, local_name, export_name,
                                  JS_EXPORT_TYPE_LOCAL);
            JS_FreeAtom(ctx, local_name);
            JS_FreeAtom(ctx, export_name);
            if (!me)
                return -1;
            if (s->token.val != ',')
                break;
            if (next_token(s))
                return -1;
        }
        if (js_parse_expect(s, '}'))
            return -1;
        if (token_is_pseudo_keyword(s, JS_ATOM_from)) {
            idx = js_parse_from_clause(s, m);
            if (idx < 0)
                return -1;
            for(i = first_export; i < m->export_entries_count; i++) {
                me = &m->export_entries[i];
                me->export_type = JS_EXPORT_TYPE_INDIRECT;
                me->u.req_module_idx = idx;
            }
        } else if (has_string_binding) {
            // Without 'from' clause, string literals cannot be used as local binding names
            return js_parse_error(s, "string export name only allowed with 'from' clause");
        }
        break;
    case '*':
        if (token_is_pseudo_keyword(s, JS_ATOM_as)) {
            /* export ns from */
            if (next_token(s))
                return -1;
            if (token_is_ident(s->token.val)) {
                export_name = JS_DupAtom(ctx, s->token.u.ident.atom);
            } else if (s->token.val == TOK_STRING) {
                export_name = JS_ValueToAtom(ctx, s->token.u.str.str);
                if (export_name == JS_ATOM_NULL) {
                    return -1;
                }
            } else {
                return js_parse_error(s, "identifier or string expected");
            }
            if (next_token(s))
                goto fail1;
            idx = js_parse_from_clause(s, m);
            if (idx < 0)
                goto fail1;
            me = add_export_entry(s, m, JS_ATOM__star_, export_name,
                                  JS_EXPORT_TYPE_INDIRECT);
            JS_FreeAtom(ctx, export_name);
            if (!me)
                return -1;
            me->u.req_module_idx = idx;
        } else {
            idx = js_parse_from_clause(s, m);
            if (idx < 0)
                return -1;
            if (add_star_export_entry(ctx, m, idx) < 0)
                return -1;
        }
        break;
    case TOK_DEFAULT:
        if (s->token.val == TOK_CLASS) {
            return js_parse_class(s, false, JS_PARSE_EXPORT_DEFAULT);
        } else if (s->token.val == TOK_FUNCTION ||
                   (token_is_pseudo_keyword(s, JS_ATOM_async) &&
                    peek_token(s, true) == TOK_FUNCTION)) {
            return js_parse_function_decl2(s, JS_PARSE_FUNC_STATEMENT,
                                           JS_FUNC_NORMAL, JS_ATOM_NULL,
                                           s->token.ptr,
                                           s->token.line_num,
                                           s->token.col_num,
                                           JS_PARSE_EXPORT_DEFAULT, NULL);
        } else {
            if (js_parse_assign_expr(s))
                return -1;
        }
        /* set the name of anonymous functions */
        set_object_name(s, JS_ATOM_default);

        /* store the value in the _default_ global variable and export
           it */
        local_name = JS_ATOM__default_;
        if (define_var(s, s->cur_func, local_name, JS_VAR_DEF_LET) < 0)
            return -1;
        emit_op(s, OP_scope_put_var_init);
        emit_atom(s, local_name);
        emit_u16(s, 0);

        if (!add_export_entry(s, m, local_name, JS_ATOM_default,
                              JS_EXPORT_TYPE_LOCAL))
            return -1;
        break;
    case TOK_VAR:
    case TOK_LET:
    case TOK_CONST:
    case TOK_USING:
        return js_parse_var(s, PF_IN_ACCEPTED, tok, /*export_flag*/true);
    default:
        return js_parse_error(s, "invalid export syntax");
    }
    return js_parse_expect_semi(s);
}

static int add_closure_var(JSContext *ctx, JSFunctionDef *s,
                           JSClosureTypeEnum closure_type,
                           int var_idx, JSAtom var_name,
                           bool is_const, bool is_lexical,
                           JSVarKindEnum var_kind);

static int add_import(JSParseState *s, JSModuleDef *m,
                      JSAtom local_name, JSAtom import_name)
{
    JSContext *ctx = s->ctx;
    int i, var_idx;
    JSImportEntry *mi;
    JSClosureTypeEnum closure_type;

    if (local_name == JS_ATOM_arguments || local_name == JS_ATOM_eval)
        return js_parse_error(s, "invalid import binding");

    if (local_name != JS_ATOM_default) {
        for (i = 0; i < s->cur_func->closure_var_count; i++) {
            if (s->cur_func->closure_var[i].var_name == local_name)
                return js_parse_error(s, "duplicate import binding");
        }
    }

    if (import_name == JS_ATOM__star_)
        closure_type = JS_CLOSURE_MODULE_DECL;
    else
        closure_type = JS_CLOSURE_MODULE_IMPORT;
    var_idx = add_closure_var(ctx, s->cur_func, closure_type,
                              m->import_entries_count,
                              local_name, true, true, JS_VAR_NORMAL);
    if (var_idx < 0)
        return -1;
    if (js_resize_array(ctx, (void **)&m->import_entries,
                        sizeof(JSImportEntry),
                        &m->import_entries_size,
                        m->import_entries_count + 1))
        return -1;
    mi = &m->import_entries[m->import_entries_count++];
    mi->import_name = JS_DupAtom(ctx, import_name);
    mi->var_idx = var_idx;
    return 0;
}

static __exception int js_parse_import(JSParseState *s)
{
    JSContext *ctx = s->ctx;
    JSModuleDef *m = s->cur_func->module;
    JSAtom local_name, import_name, module_name;
    int first_import, i, idx;

    if (next_token(s))
        return -1;

    first_import = m->import_entries_count;
    if (s->token.val == TOK_STRING) {
        module_name = JS_ValueToAtom(ctx, s->token.u.str.str);
        if (module_name == JS_ATOM_NULL)
            return -1;
        if (next_token(s)) {
            JS_FreeAtom(ctx, module_name);
            return -1;
        }
        idx = add_req_module_entry(ctx, m, module_name);
        JS_FreeAtom(ctx, module_name);
        if (idx < 0)
            return -1;
        if (s->token.val == TOK_WITH) {
            if (js_parse_with_clause(s, &m->req_module_entries[idx]))
                return -1;
        }
    } else {
        if (s->token.val == TOK_IDENT) {
            if (s->token.u.ident.is_reserved) {
                return js_parse_error_reserved_identifier(s);
            }
            /* "default" import */
            local_name = JS_DupAtom(ctx, s->token.u.ident.atom);
            import_name = JS_ATOM_default;
            if (next_token(s))
                goto fail;
            if (add_import(s, m, local_name, import_name))
                goto fail;
            JS_FreeAtom(ctx, local_name);

            if (s->token.val != ',')
                goto end_import_clause;
            if (next_token(s))
                return -1;
        }

        if (s->token.val == '*') {
            /* name space import */
            if (next_token(s))
                return -1;
            if (!token_is_pseudo_keyword(s, JS_ATOM_as))
                return js_parse_error(s, "expecting 'as'");
            if (next_token(s))
                return -1;
            if (!token_is_ident(s->token.val)) {
                js_parse_error(s, "identifier expected");
                return -1;
            }
            local_name = JS_DupAtom(ctx, s->token.u.ident.atom);
            import_name = JS_ATOM__star_;
            if (next_token(s))
                goto fail;
            if (add_import(s, m, local_name, import_name))
                goto fail;
            JS_FreeAtom(ctx, local_name);
        } else if (s->token.val == '{') {
            if (next_token(s))
                return -1;

            while (s->token.val != '}') {
                if (token_is_ident(s->token.val)) {
                    import_name = JS_DupAtom(ctx, s->token.u.ident.atom);
                } else if (s->token.val == TOK_STRING) {
                    import_name = JS_ValueToAtom(ctx, s->token.u.str.str);
                    if (import_name == JS_ATOM_NULL)
                        return -1;
                } else {
                    return js_parse_error(s, "identifier or string expected expected");
                }
                local_name = JS_ATOM_NULL;
                if (next_token(s))
                    goto fail;
                if (token_is_pseudo_keyword(s, JS_ATOM_as)) {
                    if (next_token(s))
                        goto fail;
                    if (!token_is_ident(s->token.val)) {
                        js_parse_error(s, "identifier expected");
                        goto fail;
                    }
                    local_name = JS_DupAtom(ctx, s->token.u.ident.atom);
                    if (next_token(s)) {
                    fail:
                        JS_FreeAtom(ctx, local_name);
                        JS_FreeAtom(ctx, import_name);
                        return -1;
                    }
                } else {
                    local_name = JS_DupAtom(ctx, import_name);
                }
                if (add_import(s, m, local_name, import_name))
                    goto fail;
                JS_FreeAtom(ctx, local_name);
                JS_FreeAtom(ctx, import_name);
                if (s->token.val != ',')
                    break;
                if (next_token(s))
                    return -1;
            }
            if (js_parse_expect(s, '}'))
                return -1;
        }
    end_import_clause:
        idx = js_parse_from_clause(s, m);
        if (idx < 0)
            return -1;
    }
    for(i = first_import; i < m->import_entries_count; i++)
        m->import_entries[i].req_module_idx = idx;

    return js_parse_expect_semi(s);
}

static __exception int js_parse_source_element(JSParseState *s)
{
    JSFunctionDef *fd = s->cur_func;
    int tok;

    if (s->token.val == TOK_FUNCTION ||
        (token_is_pseudo_keyword(s, JS_ATOM_async) &&
         peek_token(s, true) == TOK_FUNCTION)) {
        if (js_parse_function_decl(s, JS_PARSE_FUNC_STATEMENT,
                                   JS_FUNC_NORMAL, JS_ATOM_NULL,
                                   s->token.ptr,
                                   s->token.line_num,
                                   s->token.col_num))
            return -1;
    } else if (s->token.val == TOK_EXPORT && fd->module) {
        if (js_parse_export(s))
            return -1;
    } else if (s->token.val == TOK_IMPORT && fd->module &&
               ((tok = peek_token(s, false)) != '(' && tok != '.'))  {
        /* the peek_token is needed to avoid confusion with ImportCall
           (dynamic import) or import.meta */
        if (js_parse_import(s))
            return -1;
    } else {
        if (js_parse_statement_or_decl(s, DECL_MASK_ALL))
            return -1;
    }
    return 0;
}

/* `filename` may be pure ASCII or UTF-8 encoded */
static JSFunctionDef *js_new_function_def(JSContext *ctx,
                                          JSFunctionDef *parent,
                                          bool is_eval,
                                          bool is_func_expr,
                                          const char *filename,
                                          int line_num,
                                          int col_num)
{
    JSFunctionDef *fd;

    fd = js_mallocz(ctx, sizeof(*fd));
    if (!fd)
        return NULL;

    fd->ctx = ctx;
    init_list_head(&fd->child_list);

    /* insert in parent list */
    fd->parent = parent;
    fd->parent_cpool_idx = -1;
    if (parent) {
        list_add_tail(&fd->link, &parent->child_list);
        fd->is_strict_mode = parent->is_strict_mode;
        fd->parent_scope_level = parent->scope_level;
    }

    fd->is_eval = is_eval;
    fd->is_func_expr = is_func_expr;
    js_dbuf_init(ctx, &fd->byte_code);
    fd->last_opcode_pos = -1;
    fd->func_name = JS_ATOM_NULL;
    fd->var_object_idx = -1;
    fd->arg_var_object_idx = -1;
    fd->arguments_var_idx = -1;
    fd->arguments_arg_idx = -1;
    fd->func_var_idx = -1;
    fd->eval_ret_idx = -1;
    fd->this_var_idx = -1;
    fd->new_target_var_idx = -1;
    fd->this_active_func_var_idx = -1;
    fd->home_object_var_idx = -1;

    /* XXX: should distinguish arg, var and var object and body scopes */
    fd->scopes = fd->def_scope_array;
    fd->scope_size = countof(fd->def_scope_array);
    fd->scope_count = 1;
    fd->scopes[0].first = -1;
    fd->scopes[0].parent = -1;
    fd->scopes[0].has_using = 0;
    fd->scopes[0].is_await_using = 0;
    fd->scopes[0].using_label_catch = -1;
    fd->scopes[0].using_label_end = -1;
    fd->scope_level = 0;  /* 0: var/arg scope */
    fd->scope_first = -1;
    fd->body_scope = -1;

    fd->filename = JS_NewAtom(ctx, filename);
    fd->line_num = line_num;
    fd->col_num = col_num;

    js_dbuf_init(ctx, &fd->pc2line);
    //fd->pc2line_last_line_num = line_num;
    //fd->pc2line_last_pc = 0;

    return fd;
}

#endif // TURBOJS_DISABLE_PARSER

