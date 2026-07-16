/* Engine domain source: modules/eval_modules.inc -> function_compiler.
 * Ownership: modules subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static void free_function_bytecode(JSRuntime *rt, JSFunctionBytecode *b)
{
    int i;

    if (rt->jit_code_cache)
        TurboJS_CodeCacheInvalidate((TurboJSCodeCache *)rt->jit_code_cache, b);

    if (b->byte_code_buf)
        free_bytecode_atoms(rt, b->byte_code_buf, b->byte_code_len, true);

    if (b->vardefs) {
        for(i = 0; i < b->arg_count + b->var_count; i++) {
            JS_FreeAtomRT(rt, b->vardefs[i].var_name);
        }
    }
    for(i = 0; i < b->cpool_count; i++)
        JS_FreeValueRT(rt, b->cpool[i]);

    for(i = 0; i < b->closure_var_count; i++) {
        JSClosureVar *cv = &b->closure_var[i];
        JS_FreeAtomRT(rt, cv->var_name);
    }
    if (b->realm)
        JS_FreeContext(b->realm);

    JS_FreeAtomRT(rt, b->func_name);
    JS_FreeAtomRT(rt, b->filename);
    js_free_rt(rt, b->pc2line_buf);
    js_free_rt(rt, b->source);

    remove_gc_object(&b->header);
    if (rt->gc_phase == JS_GC_PHASE_REMOVE_CYCLES && JS_REF_COUNT(b) != 0) {
        list_add_tail(&b->header.link, &rt->gc_zero_ref_count_list);
    } else {
        js_free_rt(rt, b);
    }
}

#ifndef QJS_DISABLE_PARSER

static __exception int js_parse_directives(JSParseState *s)
{
    char str[20];
    JSParsePos pos;
    bool has_semi;

    if (s->token.val != TOK_STRING)
        return 0;

    js_parse_get_pos(s, &pos);

    while(s->token.val == TOK_STRING) {
        /* Copy actual source string representation */
        snprintf(str, sizeof str, "%.*s",
                 (int)(s->buf_ptr - s->token.ptr - 2), s->token.ptr + 1);

        if (next_token(s))
            return -1;

        has_semi = false;
        switch (s->token.val) {
        case ';':
            if (next_token(s))
                return -1;
            has_semi = true;
            break;
        case '}':
        case TOK_EOF:
            has_semi = true;
            break;
        case TOK_NUMBER:
        case TOK_STRING:
        case TOK_TEMPLATE:
        case TOK_IDENT:
        case TOK_REGEXP:
        case TOK_DEC:
        case TOK_INC:
        case TOK_NULL:
        case TOK_FALSE:
        case TOK_TRUE:
        case TOK_IF:
        case TOK_RETURN:
        case TOK_VAR:
        case TOK_USING:
        case TOK_THIS:
        case TOK_DELETE:
        case TOK_TYPEOF:
        case TOK_NEW:
        case TOK_DO:
        case TOK_WHILE:
        case TOK_FOR:
        case TOK_SWITCH:
        case TOK_THROW:
        case TOK_TRY:
        case TOK_FUNCTION:
        case TOK_DEBUGGER:
        case TOK_WITH:
        case TOK_CLASS:
        case TOK_CONST:
        case TOK_ENUM:
        case TOK_EXPORT:
        case TOK_IMPORT:
        case TOK_SUPER:
        case TOK_INTERFACE:
        case TOK_LET:
        case TOK_PACKAGE:
        case TOK_PRIVATE:
        case TOK_PROTECTED:
        case TOK_PUBLIC:
        case TOK_STATIC:
            /* automatic insertion of ';' */
            if (s->got_lf)
                has_semi = true;
            break;
        default:
            break;
        }
        if (!has_semi)
            break;
        if (!strcmp(str, "use strict")) {
            s->cur_func->has_use_strict = true;
            s->cur_func->is_strict_mode = true;
        }
    }
    return js_parse_seek_token(s, &pos);
}

static bool js_invalid_strict_name(JSAtom name) {
    switch (name) {
    case JS_ATOM_eval:
    case JS_ATOM_arguments:
    case JS_ATOM_implements: // future strict reserved words
    case JS_ATOM_interface:
    case JS_ATOM_let:
    case JS_ATOM_package:
    case JS_ATOM_private:
    case JS_ATOM_protected:
    case JS_ATOM_public:
    case JS_ATOM_static:
    case JS_ATOM_yield:
        return true;
    default:
        return false;
    }
}

static int js_parse_function_check_names(JSParseState *s, JSFunctionDef *fd,
                                         JSAtom func_name)
{
    JSAtom name;
    int i, idx;

    if (fd->is_strict_mode) {
        if (!fd->has_simple_parameter_list && fd->has_use_strict) {
            return js_parse_error(s, "\"use strict\" not allowed in function with default or destructuring parameter");
        }
        if (js_invalid_strict_name(func_name)) {
            return js_parse_error(s, "invalid function name in strict code");
        }
        for (idx = 0; idx < fd->arg_count; idx++) {
            name = fd->args[idx].var_name;
            if (js_invalid_strict_name(name)) {
                return js_parse_error(s, "invalid argument name in strict code");
            }
        }
    }
    /* check async_generator case */
    if (fd->is_strict_mode
    ||  !fd->has_simple_parameter_list
    ||  (fd->func_type == JS_PARSE_FUNC_METHOD && fd->func_kind == JS_FUNC_ASYNC)
    ||  fd->func_type == JS_PARSE_FUNC_ARROW
    ||  fd->func_type == JS_PARSE_FUNC_METHOD) {
        for (idx = 0; idx < fd->arg_count; idx++) {
            name = fd->args[idx].var_name;
            if (name != JS_ATOM_NULL) {
                for (i = 0; i < idx; i++) {
                    if (fd->args[i].var_name == name)
                        goto duplicate;
                }
                /* Check if argument name duplicates a destructuring parameter */
                /* XXX: should have a flag for such variables */
                for (i = 0; i < fd->var_count; i++) {
                    if (fd->vars[i].var_name == name &&
                        fd->vars[i].scope_level == 0)
                        goto duplicate;
                }
            }
        }
    }
    return 0;

duplicate:
    return js_parse_error(s, "Duplicate parameter name not allowed in this context");
}

/* create a function to initialize class fields */
static JSFunctionDef *js_parse_function_class_fields_init(JSParseState *s)
{
    JSFunctionDef *fd;

    fd = js_new_function_def(s->ctx, s->cur_func, false, false,
                             s->filename, 0, 0);
    if (!fd)
        return NULL;
    fd->func_name = JS_ATOM_NULL;
    fd->has_prototype = false;
    fd->has_home_object = true;

    fd->has_arguments_binding = false;
    fd->has_this_binding = true;
    fd->is_derived_class_constructor = false;
    fd->new_target_allowed = true;
    fd->super_call_allowed = false;
    fd->super_allowed = fd->has_home_object;
    fd->arguments_allowed = false;

    fd->func_kind = JS_FUNC_NORMAL;
    fd->func_type = JS_PARSE_FUNC_METHOD;
    return fd;
}

/* func_name must be JS_ATOM_NULL for JS_PARSE_FUNC_STATEMENT and
   JS_PARSE_FUNC_EXPR, JS_PARSE_FUNC_ARROW and JS_PARSE_FUNC_VAR */
static __exception int js_parse_function_decl2(JSParseState *s,
                                               JSParseFunctionEnum func_type,
                                               JSFunctionKindEnum func_kind,
                                               JSAtom func_name,
                                               const uint8_t *ptr,
                                               int function_line_num,
                                               int function_col_num,
                                               JSParseExportEnum export_flag,
                                               JSFunctionDef **pfd)
{
    JSContext *ctx = s->ctx;
    JSFunctionDef *fd = s->cur_func;
    bool is_expr;
    int func_idx, lexical_func_idx = -1;
    bool has_opt_arg;
    bool create_func_var = false;

    is_expr = (func_type != JS_PARSE_FUNC_STATEMENT &&
               func_type != JS_PARSE_FUNC_VAR);

    if (func_type == JS_PARSE_FUNC_STATEMENT ||
        func_type == JS_PARSE_FUNC_VAR ||
        func_type == JS_PARSE_FUNC_EXPR) {
        if (func_kind == JS_FUNC_NORMAL &&
            token_is_pseudo_keyword(s, JS_ATOM_async) &&
            peek_token(s, true) != '\n') {
            if (next_token(s))
                return -1;
            func_kind = JS_FUNC_ASYNC;
        }
        if (next_token(s))
            return -1;
        if (s->token.val == '*') {
            if (next_token(s))
                return -1;
            func_kind |= JS_FUNC_GENERATOR;
        }

        if (s->token.val == TOK_IDENT) {
            if (s->token.u.ident.is_reserved ||
                (s->token.u.ident.atom == JS_ATOM_yield &&
                 func_type == JS_PARSE_FUNC_EXPR &&
                 (func_kind & JS_FUNC_GENERATOR)) ||
                (s->token.u.ident.atom == JS_ATOM_await &&
                 ((func_type == JS_PARSE_FUNC_EXPR &&
                   (func_kind & JS_FUNC_ASYNC)) ||
                  func_type == JS_PARSE_FUNC_CLASS_STATIC_INIT))) {
                return js_parse_error_reserved_identifier(s);
            }
        }
        if (s->token.val == TOK_IDENT ||
            (((s->token.val == TOK_YIELD && !fd->is_strict_mode) ||
             (s->token.val == TOK_AWAIT && !s->is_module)) &&
             func_type == JS_PARSE_FUNC_EXPR)) {
            func_name = JS_DupAtom(ctx, s->token.u.ident.atom);
            if (next_token(s)) {
                JS_FreeAtom(ctx, func_name);
                return -1;
            }
        } else {
            if (func_type != JS_PARSE_FUNC_EXPR &&
                export_flag != JS_PARSE_EXPORT_DEFAULT) {
                return js_parse_error(s, "function name expected");
            }
        }
    } else if (func_type != JS_PARSE_FUNC_ARROW) {
        func_name = JS_DupAtom(ctx, func_name);
    }

    if (fd->is_eval && fd->eval_type == JS_EVAL_TYPE_MODULE &&
        (func_type == JS_PARSE_FUNC_STATEMENT || func_type == JS_PARSE_FUNC_VAR)) {
        JSGlobalVar *hf;
        hf = find_global_var(fd, func_name);
        /* XXX: should check scope chain */
        if (hf && hf->scope_level == fd->scope_level) {
            js_parse_error(s, "invalid redefinition of global identifier in module code");
            JS_FreeAtom(ctx, func_name);
            return -1;
        }
    }

    if (func_type == JS_PARSE_FUNC_VAR) {
        if (!fd->is_strict_mode
        && func_kind == JS_FUNC_NORMAL
        &&  find_lexical_decl(ctx, fd, func_name, fd->scope_first, false) < 0
        &&  !((func_idx = find_var(ctx, fd, func_name)) >= 0 && (func_idx & ARGUMENT_VAR_OFFSET))
        &&  !(func_name == JS_ATOM_arguments && fd->has_arguments_binding)) {
            create_func_var = true;
        }
        /* Create the lexical name here so that the function closure
           contains it */
        if (fd->is_eval &&
            (fd->eval_type == JS_EVAL_TYPE_GLOBAL ||
             fd->eval_type == JS_EVAL_TYPE_MODULE) &&
            fd->scope_level == fd->body_scope) {
            /* avoid creating a lexical variable in the global
               scope. XXX: check annex B */
            JSGlobalVar *hf;
            hf = find_global_var(fd, func_name);
            /* XXX: should check scope chain */
            if (hf && hf->scope_level == fd->scope_level) {
                js_parse_error(s, "invalid redefinition of global identifier");
                JS_FreeAtom(ctx, func_name);
                return -1;
            }
        } else {
            /* Always create a lexical name, fail if at the same scope as
               existing name */
            /* Lexical variable will be initialized upon entering scope */
            lexical_func_idx = define_var(s, fd, func_name,
                                          func_kind != JS_FUNC_NORMAL ?
                                          JS_VAR_DEF_NEW_FUNCTION_DECL :
                                          JS_VAR_DEF_FUNCTION_DECL);
            if (lexical_func_idx < 0) {
                JS_FreeAtom(ctx, func_name);
                return -1;
            }
        }
    }

    fd = js_new_function_def(ctx, fd, false, is_expr, s->filename,
                             function_line_num, function_col_num);
    if (!fd) {
        JS_FreeAtom(ctx, func_name);
        return -1;
    }
    if (pfd)
        *pfd = fd;
    s->cur_func = fd;
    fd->func_name = func_name;
    /* XXX: test !fd->is_generator is always false */
    fd->has_prototype = (func_type == JS_PARSE_FUNC_STATEMENT ||
                         func_type == JS_PARSE_FUNC_VAR ||
                         func_type == JS_PARSE_FUNC_EXPR) &&
                        func_kind == JS_FUNC_NORMAL;
    fd->has_home_object = (func_type == JS_PARSE_FUNC_METHOD ||
                           func_type == JS_PARSE_FUNC_GETTER ||
                           func_type == JS_PARSE_FUNC_SETTER ||
                           func_type == JS_PARSE_FUNC_CLASS_CONSTRUCTOR ||
                           func_type == JS_PARSE_FUNC_DERIVED_CLASS_CONSTRUCTOR);
    fd->has_arguments_binding = (func_type != JS_PARSE_FUNC_ARROW &&
                                 func_type != JS_PARSE_FUNC_CLASS_STATIC_INIT);
    fd->has_this_binding = fd->has_arguments_binding;
    fd->is_derived_class_constructor = (func_type == JS_PARSE_FUNC_DERIVED_CLASS_CONSTRUCTOR);
    if (func_type == JS_PARSE_FUNC_ARROW) {
        fd->new_target_allowed = fd->parent->new_target_allowed;
        fd->super_call_allowed = fd->parent->super_call_allowed;
        fd->super_allowed = fd->parent->super_allowed;
        fd->arguments_allowed = fd->parent->arguments_allowed;
    } else if (func_type == JS_PARSE_FUNC_CLASS_STATIC_INIT) {
        fd->new_target_allowed = true; // although new.target === undefined
        fd->super_call_allowed = false;
        fd->super_allowed = true;
        fd->arguments_allowed = false;
    } else {
        fd->new_target_allowed = true;
        fd->super_call_allowed = fd->is_derived_class_constructor;
        fd->super_allowed = fd->has_home_object;
        fd->arguments_allowed = true;
    }

    /* fd->in_function_body == false prevents yield/await during the parsing
       of the arguments in generator/async functions. They are parsed as
       regular identifiers for other function kinds. */
    fd->func_kind = func_kind;
    fd->func_type = func_type;

    if (func_type == JS_PARSE_FUNC_CLASS_CONSTRUCTOR ||
        func_type == JS_PARSE_FUNC_DERIVED_CLASS_CONSTRUCTOR) {
        /* error if not invoked as a constructor */
        emit_op(s, OP_check_ctor);
    }

    if (func_type == JS_PARSE_FUNC_CLASS_CONSTRUCTOR) {
        emit_class_field_init(s);
    }

    /* parse arguments */
    fd->has_simple_parameter_list = true;
    fd->has_parameter_expressions = false;
    has_opt_arg = false;
    if (func_type == JS_PARSE_FUNC_ARROW && s->token.val == TOK_IDENT) {
        JSAtom name;
        if (s->token.u.ident.is_reserved) {
            js_parse_error_reserved_identifier(s);
            goto fail;
        }
        name = s->token.u.ident.atom;
        if (add_arg(ctx, fd, name) < 0)
            goto fail;
        fd->defined_arg_count = 1;
    } else if (func_type != JS_PARSE_FUNC_CLASS_STATIC_INIT) {
        if (s->token.val == '(') {
            int skip_bits;
            /* if there is an '=' inside the parameter list, we
               consider there is a parameter expression inside */
            js_parse_skip_parens_token(s, &skip_bits, false);
            if (skip_bits & SKIP_HAS_ASSIGNMENT)
                fd->has_parameter_expressions = true;
            if (next_token(s))
                goto fail;
        } else {
            if (js_parse_expect(s, '('))
                goto fail;
        }

        if (fd->has_parameter_expressions) {
            fd->scope_level = -1; /* force no parent scope */
            if (push_scope(s) < 0)
                return -1;
        }

        while (s->token.val != ')') {
            JSAtom name;
            bool rest = false;
            int idx, has_initializer;

            if (s->token.val == TOK_ELLIPSIS) {
                fd->has_simple_parameter_list = false;
                rest = true;
                if (next_token(s))
                    goto fail;
            }
            if (s->token.val == '[' || s->token.val == '{') {
                fd->has_simple_parameter_list = false;
                if (rest) {
                    emit_op(s, OP_rest);
                    emit_u16(s, fd->arg_count);
                } else {
                    /* unnamed arg for destructuring */
                    idx = add_arg(ctx, fd, JS_ATOM_NULL);
                    emit_op(s, OP_get_arg);
                    emit_u16(s, idx);
                }
                has_initializer = js_parse_destructuring_element(s, fd->has_parameter_expressions ? TOK_LET : TOK_VAR, true, true, -1, true, false);
                if (has_initializer < 0)
                    goto fail;
                if (has_initializer)
                    has_opt_arg = true;
                if (!has_opt_arg)
                    fd->defined_arg_count++;
            } else if (s->token.val == TOK_IDENT) {
                if (s->token.u.ident.is_reserved) {
                    js_parse_error_reserved_identifier(s);
                    goto fail;
                }
                name = s->token.u.ident.atom;
                if (name == JS_ATOM_yield && fd->func_kind == JS_FUNC_GENERATOR) {
                    js_parse_error_reserved_identifier(s);
                    goto fail;
                }
                if (fd->has_parameter_expressions) {
                    if (js_parse_check_duplicate_parameter(s, name))
                        goto fail;
                    if (define_var(s, fd, name, JS_VAR_DEF_LET) < 0)
                        goto fail;
                }
                /* XXX: could avoid allocating an argument if rest is true */
                idx = add_arg(ctx, fd, name);
                if (idx < 0)
                    goto fail;
                if (next_token(s))
                    goto fail;
                if (rest) {
                    emit_op(s, OP_rest);
                    emit_u16(s, idx);
                    if (fd->has_parameter_expressions) {
                        emit_op(s, OP_dup);
                        emit_op(s, OP_scope_put_var_init);
                        emit_atom(s, name);
                        emit_u16(s, fd->scope_level);
                    }
                    emit_op(s, OP_put_arg);
                    emit_u16(s, idx);
                    fd->has_simple_parameter_list = false;
                    has_opt_arg = true;
                } else if (s->token.val == '=') {
                    int label;

                    fd->has_simple_parameter_list = false;
                    has_opt_arg = true;

                    if (next_token(s))
                        goto fail;

                    label = new_label(s);
                    emit_op(s, OP_get_arg);
                    emit_u16(s, idx);
                    emit_op(s, OP_dup);
                    emit_op(s, OP_undefined);
                    emit_op(s, OP_strict_eq);
                    emit_goto(s, OP_if_false, label);
                    emit_op(s, OP_drop);
                    if (js_parse_assign_expr(s))
                        goto fail;
                    set_object_name(s, name);
                    emit_op(s, OP_dup);
                    emit_op(s, OP_put_arg);
                    emit_u16(s, idx);
                    emit_label(s, label);
                    emit_op(s, OP_scope_put_var_init);
                    emit_atom(s, name);
                    emit_u16(s, fd->scope_level);
                } else {
                    if (!has_opt_arg) {
                        fd->defined_arg_count++;
                    }
                    if (fd->has_parameter_expressions) {
                        /* copy the argument to the argument scope */
                        emit_op(s, OP_get_arg);
                        emit_u16(s, idx);
                        emit_op(s, OP_scope_put_var_init);
                        emit_atom(s, name);
                        emit_u16(s, fd->scope_level);
                    }
                }
            } else {
                js_parse_error(s, "missing formal parameter");
                goto fail;
            }
            if (rest && s->token.val != ')') {
                js_parse_expect(s, ')');
                goto fail;
            }
            if (s->token.val == ')')
                break;
            if (js_parse_expect(s, ','))
                goto fail;
        }
        if ((func_type == JS_PARSE_FUNC_GETTER && fd->arg_count != 0) ||
            (func_type == JS_PARSE_FUNC_SETTER && fd->arg_count != 1)) {
            js_parse_error(s, "invalid number of arguments for getter or setter");
            goto fail;
        }
    }

    if (fd->has_parameter_expressions) {
        int idx;

        /* Copy the variables in the argument scope to the variable
           scope (see FunctionDeclarationInstantiation() in spec). The
           normal arguments are already present, so no need to copy
           them. */
        idx = fd->scopes[fd->scope_level].first;
        while (idx >= 0) {
            JSVarDef *vd = &fd->vars[idx];
            if (vd->scope_level != fd->scope_level)
                break;
            if (find_var(ctx, fd, vd->var_name) < 0) {
                if (add_var(ctx, fd, vd->var_name) < 0)
                    goto fail;
                vd = &fd->vars[idx]; /* fd->vars may have been reallocated */
                emit_op(s, OP_scope_get_var);
                emit_atom(s, vd->var_name);
                emit_u16(s, fd->scope_level);
                emit_op(s, OP_scope_put_var);
                emit_atom(s, vd->var_name);
                emit_u16(s, 0);
            }
            idx = vd->scope_next;
        }

        /* the argument scope has no parent, hence we don't use pop_scope(s) */
        emit_op(s, OP_leave_scope);
        emit_u16(s, fd->scope_level);

        /* set the variable scope as the current scope */
        fd->scope_level = 0;
        fd->scope_first = fd->scopes[fd->scope_level].first;
    }

    if (next_token(s))
        goto fail;

    /* generator function: yield after the parameters are evaluated */
    if (func_kind == JS_FUNC_GENERATOR ||
        func_kind == JS_FUNC_ASYNC_GENERATOR)
        emit_op(s, OP_initial_yield);

    /* in generators, yield expression is forbidden during the parsing
       of the arguments */
    fd->in_function_body = true;
    push_scope(s);  /* enter body scope */
    fd->body_scope = fd->scope_level;

    if (s->token.val == TOK_ARROW) {
        if (next_token(s))
            goto fail;

        if (s->token.val != '{') {
            if (js_parse_function_check_names(s, fd, func_name))
                goto fail;

            if (js_parse_assign_expr(s))
                goto fail;

            if (func_kind != JS_FUNC_NORMAL)
                emit_op(s, OP_return_async);
            else
                emit_op(s, OP_return);

            /* save the function source code */
            /* the end of the function source code is after the last
                token of the function source stored into s->last_ptr */
            fd->source_len = s->last_ptr - ptr;
            fd->source = js_strndup(ctx, (const char *)ptr, fd->source_len);
            if (!fd->source)
                goto fail;

            goto done;
        }
    }

    // js_parse_class() already consumed the '{'
    if (func_type != JS_PARSE_FUNC_CLASS_STATIC_INIT)
        if (js_parse_expect(s, '{'))
            goto fail;

    if (js_parse_directives(s))
        goto fail;

    /* in strict_mode, check function and argument names */
    if (js_parse_function_check_names(s, fd, func_name))
        goto fail;

    {
        BlockEnv using_be;
        int has_using_be = 0;

        while (s->token.val != '}') {
            if (js_parse_source_element(s))
                goto fail;
            /* Check if a 'using' was encountered in the body scope */
            if (!has_using_be && fd->scopes[fd->body_scope].has_using) {
                has_using_be = 1;
                push_break_entry(fd, &using_be, JS_ATOM_NULL, -1, -1, 1);
                using_be.has_using = true;
                using_be.using_scope_level = fd->body_scope;
            }
        }

        /* save the function source code */
        fd->source_len = s->buf_ptr - ptr;
        fd->source = js_strndup(ctx, (const char *)ptr, fd->source_len);
        if (!fd->source)
            goto fail;

        if (next_token(s)) {
            /* consume the '}' */
            goto fail;
        }

        if (has_using_be) {
            int label_catch = fd->scopes[fd->body_scope].using_label_catch;
            int label_end = fd->scopes[fd->body_scope].using_label_end;

            pop_break_entry(fd);

            if (js_is_live_code(s)) {
                emit_op(s, OP_drop);  /* drop catch_offset */
                emit_op(s, OP_using_dispose_init);  /* initial error_state */
                emit_op(s, OP_dispose_scope);
                emit_u16(s, fd->body_scope);
                emit_op(s, OP_using_dispose_end);
                emit_return(s, false);
            }

            emit_label(s, label_catch);
            emit_op(s, OP_dispose_scope);
            emit_u16(s, fd->body_scope);
            emit_op(s, OP_throw);

            emit_label(s, label_end);
        } else {
            if (js_is_live_code(s)) {
                emit_return(s, false);
            }
        }
    }
done:
    s->cur_func = fd->parent;

    /* Reparse identifiers after the function is terminated so that
       the token is parsed in the englobing function. It could be done
       by just using next_token() here for normal functions, but it is
       necessary for arrow functions with an expression body. */
    reparse_ident_token(s);

    /* create the function object */
    {
        int idx;
        JSAtom func_name = fd->func_name;

        /* the real object will be set at the end of the compilation */
        idx = cpool_add(s, JS_NULL);
        fd->parent_cpool_idx = idx;

        if (is_expr) {
            /* for constructors, no code needs to be generated here */
            if (func_type != JS_PARSE_FUNC_CLASS_CONSTRUCTOR &&
                func_type != JS_PARSE_FUNC_DERIVED_CLASS_CONSTRUCTOR) {
                /* OP_fclosure creates the function object from the bytecode
                   and adds the scope information */
                emit_op(s, OP_fclosure);
                emit_u32(s, idx);
                if (func_name == JS_ATOM_NULL) {
                    emit_op(s, OP_set_name);
                    emit_u32(s, JS_ATOM_NULL);
                }
            }
        } else if (func_type == JS_PARSE_FUNC_VAR) {
            emit_op(s, OP_fclosure);
            emit_u32(s, idx);
            if (create_func_var) {
                if (s->cur_func->is_global_var) {
                    JSGlobalVar *hf;
                    /* the global variable must be defined at the start of the
                       function */
                    hf = add_global_var(ctx, s->cur_func, func_name);
                    if (!hf)
                        goto fail;
                    /* it is considered as defined at the top level
                       (needed for annex B.3.3.4 and B.3.3.5
                       checks) */
                    hf->scope_level = 0;
                    hf->force_init = s->cur_func->is_strict_mode;
                    /* store directly into global var, bypass lexical scope */
                    emit_op(s, OP_dup);
                    emit_op(s, OP_scope_put_var);
                    emit_atom(s, func_name);
                    emit_u16(s, 0);
                } else {
                    /* do not call define_var to bypass lexical scope check */
                    func_idx = find_var(ctx, s->cur_func, func_name);
                    if (func_idx < 0) {
                        func_idx = add_var(ctx, s->cur_func, func_name);
                        if (func_idx < 0)
                            goto fail;
                    }
                    /* store directly into local var, bypass lexical catch scope */
                    emit_op(s, OP_dup);
                    emit_op(s, OP_scope_put_var);
                    emit_atom(s, func_name);
                    emit_u16(s, 0);
                }
            }
            if (lexical_func_idx >= 0) {
                /* lexical variable will be initialized upon entering scope */
                s->cur_func->vars[lexical_func_idx].func_pool_idx = idx;
                emit_op(s, OP_drop);
            } else {
                /* store function object into its lexical name */
                /* XXX: could use OP_put_loc directly */
                emit_op(s, OP_scope_put_var_init);
                emit_atom(s, func_name);
                emit_u16(s, s->cur_func->scope_level);
            }
        } else {
            if (!s->cur_func->is_global_var) {
                int var_idx = define_var(s, s->cur_func, func_name, JS_VAR_DEF_VAR);

                if (var_idx < 0)
                    goto fail;
                /* the variable will be assigned at the top of the function */
                if (var_idx & ARGUMENT_VAR_OFFSET) {
                    s->cur_func->args[var_idx - ARGUMENT_VAR_OFFSET].func_pool_idx = idx;
                } else {
                    s->cur_func->vars[var_idx].func_pool_idx = idx;
                }
            } else {
                JSAtom func_var_name;
                JSGlobalVar *hf;
                if (func_name == JS_ATOM_NULL)
                    func_var_name = JS_ATOM__default_; /* export default */
                else
                    func_var_name = func_name;
                /* the variable will be assigned at the top of the function */
                hf = add_global_var(ctx, s->cur_func, func_var_name);
                if (!hf)
                    goto fail;
                hf->cpool_idx = idx;
                if (export_flag != JS_PARSE_EXPORT_NONE) {
                    if (!add_export_entry(s, s->cur_func->module, func_var_name,
                                          export_flag == JS_PARSE_EXPORT_NAMED ? func_var_name : JS_ATOM_default, JS_EXPORT_TYPE_LOCAL))
                        goto fail;
                }
            }
        }
    }
    return 0;
 fail:
    s->cur_func = fd->parent;
    js_free_function_def(ctx, fd);
    if (pfd)
        *pfd = NULL;
    return -1;
}

static __exception int js_parse_function_decl(JSParseState *s,
                                              JSParseFunctionEnum func_type,
                                              JSFunctionKindEnum func_kind,
                                              JSAtom func_name,
                                              const uint8_t *ptr,
                                              int start_line,
                                              int start_col)
{
    return js_parse_function_decl2(s, func_type, func_kind, func_name, ptr,
                                   start_line, start_col,
                                   JS_PARSE_EXPORT_NONE, NULL);
}

static __exception int js_parse_program(JSParseState *s)
{
    JSFunctionDef *fd = s->cur_func;
    int idx;
    BlockEnv using_be;
    int has_using_be = 0;

    if (next_token(s))
        return -1;

    if (js_parse_directives(s))
        return -1;

    fd->is_global_var = (fd->eval_type == JS_EVAL_TYPE_GLOBAL) ||
        (fd->eval_type == JS_EVAL_TYPE_MODULE) ||
        !fd->is_strict_mode;

    if (!s->is_module) {
        /* hidden variable for the return value */
        fd->eval_ret_idx = idx = add_var(s->ctx, fd, JS_ATOM__ret_);
        if (idx < 0)
            return -1;
    }

    while (s->token.val != TOK_EOF) {
        if (js_parse_source_element(s))
            return -1;
        /* Check if a 'using' was encountered at the body scope level */
        if (!has_using_be && fd->scopes[fd->body_scope].has_using) {
            has_using_be = 1;
            push_break_entry(fd, &using_be, JS_ATOM_NULL, -1, -1, 1);
            using_be.has_using = true;
            using_be.using_scope_level = fd->body_scope;
        }
    }

    if (has_using_be) {
        int label_catch = fd->scopes[fd->body_scope].using_label_catch;
        int label_end = fd->scopes[fd->body_scope].using_label_end;

        pop_break_entry(fd);

        if (js_is_live_code(s)) {
            /* Normal path: drop catch_offset, dispose, then return */
            emit_op(s, OP_drop);  /* drop catch_offset */
            emit_op(s, OP_using_dispose_init);  /* initial error_state */
            emit_op(s, OP_dispose_scope);
            emit_u16(s, fd->body_scope);
            emit_op(s, OP_using_dispose_end);
        }

        if (!s->is_module) {
            if (fd->func_kind == JS_FUNC_ASYNC) {
                emit_op(s, OP_object);
                emit_op(s, OP_dup);
                emit_op(s, OP_get_loc);
                emit_u16(s, fd->eval_ret_idx);
                emit_op(s, OP_put_field);
                emit_atom(s, JS_ATOM_value);
            } else {
                emit_op(s, OP_get_loc);
                emit_u16(s, fd->eval_ret_idx);
            }
            emit_return(s, true);
        } else {
            emit_return(s, false);
        }

        /* Catch handler */
        emit_label(s, label_catch);
        emit_op(s, OP_dispose_scope);
        emit_u16(s, fd->body_scope);
        emit_op(s, OP_throw);

        emit_label(s, label_end);
    } else {
        if (!s->is_module) {
            if (fd->func_kind == JS_FUNC_ASYNC) {
                emit_op(s, OP_object);
                emit_op(s, OP_dup);
                emit_op(s, OP_get_loc);
                emit_u16(s, fd->eval_ret_idx);
                emit_op(s, OP_put_field);
                emit_atom(s, JS_ATOM_value);
            } else {
                emit_op(s, OP_get_loc);
                emit_u16(s, fd->eval_ret_idx);
            }
            emit_return(s, true);
        } else {
            emit_return(s, false);
        }
    }

    return 0;
}

#endif // QJS_DISABLE_PARSER

