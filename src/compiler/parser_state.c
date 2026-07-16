/* Engine domain source: compiler/parser_compiler.inc -> parser_state.
 * Ownership: compiler subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static __exception int json_next_token(JSParseState *s)
{
    const uint8_t *p, *p_next;
    int c;
    JSAtom atom;

    if (js_check_stack_overflow(s->ctx->rt, 1000)) {
        JS_ThrowStackOverflow(s->ctx);
        return -1;
    }

    json_free_token(s, &s->token);

    p = s->last_ptr = s->buf_ptr;
    s->last_line_num = s->token.line_num;
    s->last_col_num = s->token.col_num;
 redo:
    s->token.line_num = s->line_num;
    s->token.col_num = s->col_num;
    s->token.ptr = p;
    c = *p;
    switch(c) {
    case 0:
        if (p >= s->buf_end) {
            s->token.val = TOK_EOF;
        } else {
            goto def_token;
        }
        break;
    case '\'':
        /* JSON does not accept single quoted strings */
        goto def_token;
    case '\"':
        p++;
        if (json_parse_string(s, &p))
            goto fail;
        break;
    case '\r':  /* accept DOS and MAC newline sequences */
        if (p[1] == '\n') {
            p++;
        }
        /* fall thru */
    case '\n':
        s->line_num++;
        s->eol = p++;
        s->mark = p;
        goto redo;
    case '\f':
    case '\v':
        /* JSONWhitespace does not match <VT>, nor <FF> */
        goto def_token;
    case ' ':
    case '\t':
        p++;
        s->mark = p;
        goto redo;
    case '/':
        /* JSON does not accept comments */
        goto def_token;
    case 'a': case 'b': case 'c': case 'd':
    case 'e': case 'f': case 'g': case 'h':
    case 'i': case 'j': case 'k': case 'l':
    case 'm': case 'n': case 'o': case 'p':
    case 'q': case 'r': case 's': case 't':
    case 'u': case 'v': case 'w': case 'x':
    case 'y': case 'z':
    case 'A': case 'B': case 'C': case 'D':
    case 'E': case 'F': case 'G': case 'H':
    case 'I': case 'J': case 'K': case 'L':
    case 'M': case 'N': case 'O': case 'P':
    case 'Q': case 'R': case 'S': case 'T':
    case 'U': case 'V': case 'W': case 'X':
    case 'Y': case 'Z':
    case '_':
    case '$':
        /* identifier : only pure ascii characters are accepted */
        p++;
        atom = json_parse_ident(s, &p, c);
        if (atom == JS_ATOM_NULL)
            goto fail;
        s->token.u.ident.atom = atom;
        s->token.u.ident.has_escape = false;
        s->token.u.ident.is_reserved = false;
        s->token.val = TOK_IDENT;
        break;
    case '-':
        if (!is_digit(p[1])) {
            json_parse_error(s, p, "No number after minus sign");
            goto fail;
        }
        goto parse_number;
    case '0':
        if (is_digit(p[1])) {
            json_parse_error(s, p, "Unexpected number");
            goto fail;
        }
        goto parse_number;
    case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8':
    case '9':
        /* number */
    parse_number:
        if (json_parse_number(s, &p))
            goto fail;
        break;
    default:
        if (c >= 0x80) {
            c = utf8_decode(p, &p_next);
            if (p_next == p + 1) {
                js_parse_error(s, "Unexpected token '\\x%02x' in JSON", *p);
            } else {
                if (c > 0xFFFF) {
                    c = get_hi_surrogate(c);
                }
                js_parse_error(s, "Unexpected token '\\u%04x' in JSON", c);
            }
            goto fail;
        }
    def_token:
        s->token.val = c;
        p++;
        break;
    }
    s->token.col_num = s->mark - s->eol;
    s->buf_ptr = p;

    //    dump_token(s, &s->token);
    return 0;

 fail:
    s->token.val = TOK_ERROR;
    return -1;
}

#ifndef QJS_DISABLE_PARSER

/* only used for ':' and '=>', 'let' or 'function' look-ahead. *pp is
   only set if TOK_IMPORT is returned */
/* XXX: handle all unicode cases */
static int simple_next_token(const uint8_t **pp, bool no_line_terminator)
{
    const uint8_t *p;
    uint32_t c;

    /* skip spaces and comments */
    p = *pp;
    for (;;) {
        switch(c = *p++) {
        case '\r':
        case '\n':
            if (no_line_terminator)
                return '\n';
            continue;
        case ' ':
        case '\t':
        case '\v':
        case '\f':
            continue;
        case '/':
            if (*p == '/') {
                if (no_line_terminator)
                    return '\n';
                while (*p && *p != '\r' && *p != '\n')
                    p++;
                continue;
            }
            if (*p == '*') {
                while (*++p) {
                    if ((*p == '\r' || *p == '\n') && no_line_terminator)
                        return '\n';
                    if (*p == '*' && p[1] == '/') {
                        p += 2;
                        break;
                    }
                }
                continue;
            }
            break;
        case '=':
            if (*p == '>')
                return TOK_ARROW;
            break;
        default:
            if (lre_js_is_ident_first(c)) {
                if (c == 'i') {
                    if (p[0] == 'n' && !lre_js_is_ident_next(p[1])) {
                        return TOK_IN;
                    }
                    if (p[0] == 'm' && p[1] == 'p' && p[2] == 'o' &&
                        p[3] == 'r' && p[4] == 't' &&
                        !lre_js_is_ident_next(p[5])) {
                        *pp = p + 5;
                        return TOK_IMPORT;
                    }
                } else if (c == 'o' && *p == 'f' && !lre_js_is_ident_next(p[1])) {
                    return TOK_OF;
                } else if (c == 'e' &&
                           p[0] == 'x' && p[1] == 'p' && p[2] == 'o' &&
                           p[3] == 'r' && p[4] == 't' &&
                           !lre_js_is_ident_next(p[5])) {
                    *pp = p + 5;
                    return TOK_EXPORT;
                } else if (c == 'f' && p[0] == 'u' && p[1] == 'n' &&
                         p[2] == 'c' && p[3] == 't' && p[4] == 'i' &&
                         p[5] == 'o' && p[6] == 'n' && !lre_js_is_ident_next(p[7])) {
                    return TOK_FUNCTION;
                } else if (c == 'a' && p[0] == 'w' && p[1] == 'a' &&
                         p[2] == 'i' && p[3] == 't' && !lre_js_is_ident_next(p[4])) {
                    return TOK_AWAIT;
                } else if (c == 'u' && p[0] == 's' && p[1] == 'i' &&
                         p[2] == 'n' && p[3] == 'g' && !lre_js_is_ident_next(p[4])) {
                    return TOK_USING;
                }
                return TOK_IDENT;
            }
            break;
        }
        return c;
    }
}

static int peek_token(JSParseState *s, bool no_line_terminator)
{
    const uint8_t *p = s->buf_ptr;
    return simple_next_token(&p, no_line_terminator);
}

static void skip_shebang(const uint8_t **pp, const uint8_t *buf_end)
{
    const uint8_t *p = *pp;
    int c;

    if (p[0] == '#' && p[1] == '!') {
        p += 2;
        while (p < buf_end) {
            if (*p == '\n' || *p == '\r') {
                break;
            } else if (*p >= 0x80) {
                c = utf8_decode(p, &p);
                /* purposely ignore UTF-8 encoding errors in this comment line */
                if (c == CP_LS || c == CP_PS)
                    break;
            } else {
                p++;
            }
        }
        *pp = p;
    }
}

static inline int get_prev_opcode(JSFunctionDef *fd) {
    if (fd->last_opcode_pos < 0 || dbuf_error(&fd->byte_code))
        return OP_invalid;
    else
        return fd->byte_code.buf[fd->last_opcode_pos];
}

static bool js_is_live_code(JSParseState *s) {
    switch (get_prev_opcode(s->cur_func)) {
    case OP_tail_call:
    case OP_tail_call_method:
    case OP_return:
    case OP_return_undef:
    case OP_return_async:
    case OP_throw:
    case OP_throw_error:
    case OP_goto:
    case OP_goto8:
    case OP_goto16:
    case OP_ret:
        return false;
    default:
        return true;
    }
}

static void emit_u8(JSParseState *s, uint8_t val)
{
    dbuf_putc(&s->cur_func->byte_code, val);
}

static void emit_u16(JSParseState *s, uint16_t val)
{
    dbuf_put_u16(&s->cur_func->byte_code, val);
}

static void emit_u32(JSParseState *s, uint32_t val)
{
    dbuf_put_u32(&s->cur_func->byte_code, val);
}

static void emit_source_loc_at(JSParseState *s, int line_num, int col_num)
{
    JSFunctionDef *fd = s->cur_func;
    DynBuf *bc = &fd->byte_code;

    dbuf_putc(bc, OP_source_loc);
    dbuf_put_u32(bc, line_num);
    dbuf_put_u32(bc, col_num);
}

static void emit_source_loc(JSParseState *s)
{
    emit_source_loc_at(s, s->token.line_num, s->token.col_num);
}

static void emit_op(JSParseState *s, uint8_t val)
{
    JSFunctionDef *fd = s->cur_func;
    DynBuf *bc = &fd->byte_code;

    fd->last_opcode_pos = bc->size;
    dbuf_putc(bc, val);
}

static void emit_atom(JSParseState *s, JSAtom name)
{
    DynBuf *bc = &s->cur_func->byte_code;
    if (dbuf_claim(bc, 4))
        return; /* not enough memory : don't duplicate the atom */
    put_u32(bc->buf + bc->size, JS_DupAtom(s->ctx, name));
    bc->size += 4;
}

static int update_label(JSFunctionDef *s, int label, int delta)
{
    LabelSlot *ls;

    assert(label >= 0 && label < s->label_count);
    ls = &s->label_slots[label];
    ls->ref_count += delta;
    assert(ls->ref_count >= 0);
    return ls->ref_count;
}

static int new_label_fd(JSFunctionDef *fd)
{
    int label;
    LabelSlot *ls;

    if (js_resize_array(fd->ctx, (void *)&fd->label_slots,
                            sizeof(fd->label_slots[0]),
                            &fd->label_size, fd->label_count + 1))
        return -1;
    label = fd->label_count++;
    ls = &fd->label_slots[label];
    ls->ref_count = 0;
    ls->pos = -1;
    ls->pos2 = -1;
    ls->addr = -1;
    ls->first_reloc = NULL;
    return label;
}

static int new_label(JSParseState *s)
{
    int label;
    label = new_label_fd(s->cur_func);
    if (unlikely(label < 0)) {
        dbuf_set_error(&s->cur_func->byte_code);
    }
    return label;
}

/* don't update the last opcode and don't emit line number info */
static void emit_label_raw(JSParseState *s, int label)
{
    emit_u8(s, OP_label);
    emit_u32(s, label);
    s->cur_func->label_slots[label].pos = s->cur_func->byte_code.size;
}

/* return the label ID offset */
static int emit_label(JSParseState *s, int label)
{
    if (label >= 0) {
        emit_op(s, OP_label);
        emit_u32(s, label);
        s->cur_func->label_slots[label].pos = s->cur_func->byte_code.size;
        return s->cur_func->byte_code.size - 4;
    } else {
        return -1;
    }
}

/* return label or -1 if dead code */
static int emit_goto(JSParseState *s, int opcode, int label)
{
    if (js_is_live_code(s)) {
        if (label < 0) {
            label = new_label(s);
            if (label < 0)
                return -1;
        }
        emit_op(s, opcode);
        emit_u32(s, label);
        s->cur_func->label_slots[label].ref_count++;
        return label;
    }
    return -1;
}

/* return the constant pool index. 'val' is not duplicated. */
static int cpool_add(JSParseState *s, JSValue val)
{
    JSFunctionDef *fd = s->cur_func;

    if (js_resize_array(s->ctx, (void *)&fd->cpool, sizeof(fd->cpool[0]),
                        &fd->cpool_size, fd->cpool_count + 1))
        return -1;
    fd->cpool[fd->cpool_count++] = val;
    return fd->cpool_count - 1;
}

static __exception int emit_push_const(JSParseState *s, JSValue val,
                                       bool as_atom)
{
    int idx;

    if (JS_VALUE_GET_TAG(val) == JS_TAG_STRING && as_atom) {
        JSAtom atom;
        /* warning: JS_NewAtomStr frees the string value */
        js_dup(val);
        atom = JS_NewAtomStr(s->ctx, JS_VALUE_GET_STRING(val));
        if (atom != JS_ATOM_NULL && !__JS_AtomIsTaggedInt(atom)) {
            emit_op(s, OP_push_atom_value);
            emit_u32(s, atom);
            return 0;
        }
    }

    idx = cpool_add(s, js_dup(val));
    if (idx < 0)
        return -1;
    emit_op(s, OP_push_const);
    emit_u32(s, idx);
    return 0;
}

// caveat emptor: the table size must be a power of two in order for
// masking to work, and the load factor constant must be an odd number (5)
//
// f(n)=n+n/t is used to estimate the load factor but changing t to an
// even number introduces gaps in the output of f, sometimes "jumping"
// over the next power of two; it's at powers of two when the hash table
// must be resized
static int update_var_htab(JSContext *ctx, JSFunctionDef *fd)
{
    uint32_t i, j, k, m, *p;

    if (fd->var_count < 27) // 27 + 27/5 == 32
        return 0;
    k = fd->var_count - 1;
    m = fd->var_count + fd->var_count/5;
    if (m & (m - 1)) // unless power of two
        goto insert;
    m *= 2;
    p = js_realloc(ctx, fd->vars_htab, m * sizeof(*fd->vars_htab));
    if (!p)
        return -1;
    for (i = 0; i < m; i++)
        p[i] = UINT32_MAX;
    fd->vars_htab = p;
    k = 0;
    m--;
insert:
    m = UINT32_MAX >> clz32(m);
    do {
        i = hash32(fd->vars[k].var_name);
        j = 1;
        for (;;) {
            p = &fd->vars_htab[i & m];
            if (*p == UINT32_MAX)
                break;
            i += j;
            j += 1; // quadratic probing
        }
        *p = k++;
    } while (k < (uint32_t)fd->var_count);
    return 0;
}

static int find_var_htab(JSFunctionDef *fd, JSAtom var_name)
{
    uint32_t i, j, m, *p;

    i = hash32(var_name);
    j = 1;
    m = fd->var_count + fd->var_count/5;
    m = UINT32_MAX >> clz32(m);
    for (;;) {
        p = &fd->vars_htab[i & m];
        if (*p == UINT32_MAX)
            return -1;
        if (fd->vars[*p].var_name == var_name)
            return *p;
        i += j;
        j += 1; // quadratic probing
    }
    return -1; // pacify compiler
}

/* return the variable index or -1 if not found,
   add ARGUMENT_VAR_OFFSET for argument variables */
static int find_arg(JSContext *ctx, JSFunctionDef *fd, JSAtom name)
{
    int i;
    for(i = fd->arg_count; i-- > 0;) {
        if (fd->args[i].var_name == name)
            return i | ARGUMENT_VAR_OFFSET;
    }
    return -1;
}

static int find_var(JSContext *ctx, JSFunctionDef *fd, JSAtom name)
{
    JSVarDef *vd;
    int i;

    if (fd->vars_htab) {
        i = find_var_htab(fd, name);
        if (i == -1)
            goto not_found;
        vd = &fd->vars[i];
        if (vd->scope_level == 0)
            return i;
    }
    for(i = fd->var_count; i-- > 0;) {
        vd = &fd->vars[i];
        if (vd->var_name == name)
            if (vd->scope_level == 0)
                return i;
    }
not_found:
    return find_arg(ctx, fd, name);
}

/* find a variable declaration in a given scope */
static int find_var_in_scope(JSContext *ctx, JSFunctionDef *fd,
                             JSAtom name, int scope_level)
{
    int scope_idx;
    for(scope_idx = fd->scopes[scope_level].first; scope_idx >= 0;
        scope_idx = fd->vars[scope_idx].scope_next) {
        if (fd->vars[scope_idx].scope_level != scope_level)
            break;
        if (fd->vars[scope_idx].var_name == name)
            return scope_idx;
    }
    return -1;
}

/* return true if scope == parent_scope or if scope is a child of
   parent_scope */
static bool is_child_scope(JSContext *ctx, JSFunctionDef *fd,
                           int scope, int parent_scope)
{
    while (scope >= 0) {
        if (scope == parent_scope)
            return true;
        scope = fd->scopes[scope].parent;
    }
    return false;
}

/* find a 'var' declaration in the same scope or a child scope */
static int find_var_in_child_scope(JSContext *ctx, JSFunctionDef *fd,
                                   JSAtom name, int scope_level)
{
    int i;
    for(i = 0; i < fd->var_count; i++) {
        JSVarDef *vd = &fd->vars[i];
        if (vd->var_name == name && vd->scope_level == 0) {
            if (is_child_scope(ctx, fd, vd->scope_next,
                               scope_level))
                return i;
        }
    }
    return -1;
}


static JSGlobalVar *find_global_var(JSFunctionDef *fd, JSAtom name)
{
    int i;
    for(i = 0; i < fd->global_var_count; i++) {
        JSGlobalVar *hf = &fd->global_vars[i];
        if (hf->var_name == name)
            return hf;
    }
    return NULL;

}

static JSGlobalVar *find_lexical_global_var(JSFunctionDef *fd, JSAtom name)
{
    JSGlobalVar *hf = find_global_var(fd, name);
    if (hf && hf->is_lexical)
        return hf;
    else
        return NULL;
}

static int find_lexical_decl(JSContext *ctx, JSFunctionDef *fd, JSAtom name,
                             int scope_idx, bool check_catch_var)
{
    while (scope_idx >= 0) {
        JSVarDef *vd = &fd->vars[scope_idx];
        if (vd->var_name == name &&
            (vd->is_lexical || (vd->var_kind == JS_VAR_CATCH &&
                                check_catch_var)))
            return scope_idx;
        scope_idx = vd->scope_next;
    }

    if (fd->is_eval && fd->eval_type == JS_EVAL_TYPE_GLOBAL) {
        if (find_lexical_global_var(fd, name))
            return GLOBAL_VAR_OFFSET;
    }
    return -1;
}

static int push_scope(JSParseState *s) {
    if (s->cur_func) {
        JSFunctionDef *fd = s->cur_func;
        int scope = fd->scope_count;
        /* XXX: should check for scope overflow */
        if ((fd->scope_count + 1) > fd->scope_size) {
            int new_size;
            size_t slack;
            JSVarScope *new_buf;
            /* XXX: potential arithmetic overflow */
            new_size = max_int(fd->scope_count + 1, fd->scope_size * 3 / 2);
            if (fd->scopes == fd->def_scope_array) {
                new_buf = js_realloc2(s->ctx, NULL, new_size * sizeof(*fd->scopes), &slack);
                if (!new_buf)
                    return -1;
                memcpy(new_buf, fd->scopes, fd->scope_count * sizeof(*fd->scopes));
            } else {
                new_buf = js_realloc2(s->ctx, fd->scopes, new_size * sizeof(*fd->scopes), &slack);
                if (!new_buf)
                    return -1;
            }
            new_size += slack / sizeof(*new_buf);
            fd->scopes = new_buf;
            fd->scope_size = new_size;
        }
        fd->scope_count++;
        fd->scopes[scope].parent = fd->scope_level;
        fd->scopes[scope].first = fd->scope_first;
        fd->scopes[scope].has_using = 0;
        fd->scopes[scope].is_await_using = 0;
        fd->scopes[scope].using_label_catch = -1;
        fd->scopes[scope].using_label_end = -1;
        emit_op(s, OP_enter_scope);
        emit_u16(s, scope);
        return fd->scope_level = scope;
    }
    return 0;
}

static int get_first_lexical_var(JSFunctionDef *fd, int scope)
{
    while (scope >= 0) {
        int scope_idx = fd->scopes[scope].first;
        if (scope_idx >= 0)
            return scope_idx;
        scope = fd->scopes[scope].parent;
    }
    return -1;
}

static void pop_scope(JSParseState *s) {
    if (s->cur_func) {
        /* disable scoped variables */
        JSFunctionDef *fd = s->cur_func;
        int scope = fd->scope_level;
        emit_op(s, OP_leave_scope);
        emit_u16(s, scope);
        fd->scope_level = fd->scopes[scope].parent;
        fd->scope_first = get_first_lexical_var(fd, fd->scope_level);
    }
}

static void close_scopes(JSParseState *s, int scope, int scope_stop)
{
    while (scope > scope_stop) {
        emit_op(s, OP_leave_scope);
        emit_u16(s, scope);
        scope = s->cur_func->scopes[scope].parent;
    }
}

/* return the variable index or -1 if error */
static int add_var(JSContext *ctx, JSFunctionDef *fd, JSAtom name)
{
    JSVarDef *vd;

    /* the local variable indexes are currently stored on 16 bits */
    if (fd->var_count >= JS_MAX_LOCAL_VARS) {
        // XXX: add_var() should take JSParseState *s and use js_parse_error
        JS_ThrowSyntaxError(ctx, "too many variables declared (only %d allowed)",
                            JS_MAX_LOCAL_VARS - 1);
        return -1;
    }
    if (js_resize_array(ctx, (void **)&fd->vars, sizeof(fd->vars[0]),
                        &fd->var_size, fd->var_count + 1))
        return -1;
    vd = &fd->vars[fd->var_count++];
    memset(vd, 0, sizeof(*vd));
    vd->var_name = JS_DupAtom(ctx, name);
    vd->func_pool_idx = -1;
    if (update_var_htab(ctx, fd))
        return -1;
    return fd->var_count - 1;
}

static int add_scope_var(JSContext *ctx, JSFunctionDef *fd, JSAtom name,
                         JSVarKindEnum var_kind)
{
    int idx = add_var(ctx, fd, name);
    if (idx >= 0) {
        JSVarDef *vd = &fd->vars[idx];
        vd->var_kind = var_kind;
        vd->scope_level = fd->scope_level;
        vd->scope_next = fd->scope_first;
        fd->scopes[fd->scope_level].first = idx;
        fd->scope_first = idx;
    }
    return idx;
}

static int add_func_var(JSContext *ctx, JSFunctionDef *fd, JSAtom name)
{
    int idx = fd->func_var_idx;
    if (idx < 0 && (idx = add_var(ctx, fd, name)) >= 0) {
        fd->func_var_idx = idx;
        fd->vars[idx].var_kind = JS_VAR_FUNCTION_NAME;
        if (fd->is_strict_mode)
            fd->vars[idx].is_const = true;
    }
    return idx;
}

static int add_arguments_var(JSContext *ctx, JSFunctionDef *fd)
{
    int idx = fd->arguments_var_idx;
    if (idx < 0 && (idx = add_var(ctx, fd, JS_ATOM_arguments)) >= 0) {
        fd->arguments_var_idx = idx;
    }
    return idx;
}

/* add an argument definition in the argument scope. Only needed when
   "eval()" may be called in the argument scope. Return 0 if OK. */
static int add_arguments_arg(JSContext *ctx, JSFunctionDef *fd)
{
    int idx;
    if (fd->arguments_arg_idx < 0) {
        idx = find_var_in_scope(ctx, fd, JS_ATOM_arguments, ARG_SCOPE_INDEX);
        if (idx < 0) {
            /* XXX: the scope links are not fully updated. May be an
               issue if there are child scopes of the argument
               scope */
            idx = add_var(ctx, fd, JS_ATOM_arguments);
            if (idx < 0)
                return -1;
            fd->vars[idx].scope_next = fd->scopes[ARG_SCOPE_INDEX].first;
            fd->scopes[ARG_SCOPE_INDEX].first = idx;
            fd->vars[idx].scope_level = ARG_SCOPE_INDEX;
            fd->vars[idx].is_lexical = true;

            fd->arguments_arg_idx = idx;
        }
    }
    return 0;
}

static int add_arg(JSContext *ctx, JSFunctionDef *fd, JSAtom name)
{
    JSVarDef *vd;

    /* the local variable indexes are currently stored on 16 bits */
    if (fd->arg_count >= JS_MAX_LOCAL_VARS) {
        // XXX: add_arg() should take JSParseState *s and use js_parse_error
        JS_ThrowSyntaxError(ctx, "too many parameters in function definition (only %d allowed)",
                            JS_MAX_LOCAL_VARS - 1);
        return -1;
    }
    if (js_resize_array(ctx, (void **)&fd->args, sizeof(fd->args[0]),
                        &fd->arg_size, fd->arg_count + 1))
        return -1;
    vd = &fd->args[fd->arg_count++];
    memset(vd, 0, sizeof(*vd));
    vd->var_name = JS_DupAtom(ctx, name);
    vd->func_pool_idx = -1;
    return fd->arg_count - 1;
}

/* add a global variable definition */
static JSGlobalVar *add_global_var(JSContext *ctx, JSFunctionDef *s,
                                     JSAtom name)
{
    JSGlobalVar *hf;

    if (js_resize_array(ctx, (void **)&s->global_vars,
                        sizeof(s->global_vars[0]),
                        &s->global_var_size, s->global_var_count + 1))
        return NULL;
    hf = &s->global_vars[s->global_var_count++];
    hf->cpool_idx = -1;
    hf->force_init = false;
    hf->is_lexical = false;
    hf->is_const = false;
    hf->scope_level = s->scope_level;
    hf->var_name = JS_DupAtom(ctx, name);
    return hf;
}

typedef enum {
    JS_VAR_DEF_WITH,
    JS_VAR_DEF_LET,
    JS_VAR_DEF_CONST,
    JS_VAR_DEF_FUNCTION_DECL, /* function declaration */
    JS_VAR_DEF_NEW_FUNCTION_DECL, /* async/generator function declaration */
    JS_VAR_DEF_CATCH,
    JS_VAR_DEF_VAR,
    JS_VAR_DEF_USING,
} JSVarDefEnum;

static int define_var(JSParseState *s, JSFunctionDef *fd, JSAtom name,
                      JSVarDefEnum var_def_type)
{
    JSContext *ctx = s->ctx;
    JSVarDef *vd;
    int idx;

    switch (var_def_type) {
    case JS_VAR_DEF_WITH:
        idx = add_scope_var(ctx, fd, name, JS_VAR_NORMAL);
        break;

    case JS_VAR_DEF_LET:
    case JS_VAR_DEF_CONST:
    case JS_VAR_DEF_USING:
    case JS_VAR_DEF_FUNCTION_DECL:
    case JS_VAR_DEF_NEW_FUNCTION_DECL:
        idx = find_lexical_decl(ctx, fd, name, fd->scope_first, true);
        if (idx >= 0) {
            if (idx < GLOBAL_VAR_OFFSET) {
                if (fd->vars[idx].scope_level == fd->scope_level) {
                    /* same scope: in non strict mode, functions
                       can be redefined (annex B.3.3.4). */
                    if (!(!fd->is_strict_mode &&
                          var_def_type == JS_VAR_DEF_FUNCTION_DECL &&
                          fd->vars[idx].var_kind == JS_VAR_FUNCTION_DECL)) {
                        goto redef_lex_error;
                    }
                } else if (fd->vars[idx].var_kind == JS_VAR_CATCH && (fd->vars[idx].scope_level + 2) == fd->scope_level) {
                    goto redef_lex_error;
                }
            } else {
                if (fd->scope_level == fd->body_scope) {
                redef_lex_error:
                    /* redefining a scoped var in the same scope: error */
                    return js_parse_error(s, "invalid redefinition of lexical identifier");
                }
            }
        }
        if (var_def_type != JS_VAR_DEF_FUNCTION_DECL &&
            var_def_type != JS_VAR_DEF_NEW_FUNCTION_DECL &&
            fd->scope_level == fd->body_scope &&
            find_arg(ctx, fd, name) >= 0) {
            /* lexical variable redefines a parameter name */
            return js_parse_error(s, "invalid redefinition of parameter name");
        }

        if (find_var_in_child_scope(ctx, fd, name, fd->scope_level) >= 0) {
            return js_parse_error(s, "invalid redefinition of a variable");
        }

        if (fd->is_global_var) {
            JSGlobalVar *hf;
            hf = find_global_var(fd, name);
            if (hf && is_child_scope(ctx, fd, hf->scope_level,
                                     fd->scope_level)) {
                return js_parse_error(s, "invalid redefinition of global identifier");
            }
        }

        if (fd->is_eval &&
                (fd->eval_type == JS_EVAL_TYPE_GLOBAL ||
                fd->eval_type == JS_EVAL_TYPE_MODULE) &&
                fd->scope_level == fd->body_scope &&
                var_def_type != JS_VAR_DEF_USING) {
            JSGlobalVar *hf;
            hf = add_global_var(s->ctx, fd, name);
            if (!hf)
                return -1;
            hf->is_lexical = true;
            hf->is_const = (var_def_type == JS_VAR_DEF_CONST);
            idx = GLOBAL_VAR_OFFSET;
        } else {
            JSVarKindEnum var_kind;
            if (var_def_type == JS_VAR_DEF_FUNCTION_DECL)
                var_kind = JS_VAR_FUNCTION_DECL;
            else if (var_def_type == JS_VAR_DEF_NEW_FUNCTION_DECL)
                var_kind = JS_VAR_NEW_FUNCTION_DECL;
            else if (var_def_type == JS_VAR_DEF_USING)
                var_kind = JS_VAR_USING;
            else
                var_kind = JS_VAR_NORMAL;
            idx = add_scope_var(ctx, fd, name, var_kind);
            if (idx >= 0) {
                vd = &fd->vars[idx];
                vd->is_lexical = 1;
                vd->is_const = (var_def_type == JS_VAR_DEF_CONST ||
                                var_def_type == JS_VAR_DEF_USING);
            }
        }
        break;

    case JS_VAR_DEF_CATCH:
        idx = add_scope_var(ctx, fd, name, JS_VAR_CATCH);
        break;

    case JS_VAR_DEF_VAR:
        if (find_lexical_decl(ctx, fd, name, fd->scope_first,
                              false) >= 0) {
       invalid_lexical_redefinition:
            /* error to redefine a var that inside a lexical scope */
            return js_parse_error(s, "invalid redefinition of lexical identifier");
        }
        if (fd->is_global_var) {
            JSGlobalVar *hf;
            hf = find_global_var(fd, name);
            if (hf && hf->is_lexical && hf->scope_level == fd->scope_level &&
                fd->eval_type == JS_EVAL_TYPE_MODULE) {
                goto invalid_lexical_redefinition;
            }
            hf = add_global_var(s->ctx, fd, name);
            if (!hf)
                return -1;
            idx = GLOBAL_VAR_OFFSET;
        } else {
            /* if the variable already exists, don't add it again  */
            idx = find_var(ctx, fd, name);
            if (idx >= 0)
                break;
            idx = add_var(ctx, fd, name);
            if (idx >= 0) {
                if (name == JS_ATOM_arguments && fd->has_arguments_binding)
                    fd->arguments_var_idx = idx;
                fd->vars[idx].scope_next = fd->scope_level;
            }
        }
        break;
    default:
        abort();
    }
    return idx;
}

/* add a private field variable in the current scope */
static int add_private_class_field(JSParseState *s, JSFunctionDef *fd,
                                   JSAtom name, JSVarKindEnum var_kind, bool is_static)
{
    JSContext *ctx = s->ctx;
    JSVarDef *vd;
    int idx;

    idx = add_scope_var(ctx, fd, name, var_kind);
    if (idx < 0)
        return idx;
    vd = &fd->vars[idx];
    vd->is_lexical = 1;
    vd->is_const = 1;
    vd->is_static_private = is_static;
    return idx;
}

static __exception int js_parse_expr(JSParseState *s);
static __exception int js_parse_function_decl(JSParseState *s,
                                              JSParseFunctionEnum func_type,
                                              JSFunctionKindEnum func_kind,
                                              JSAtom func_name, const uint8_t *ptr,
                                              int start_line, int start_col);
static JSFunctionDef *js_parse_function_class_fields_init(JSParseState *s);
static __exception int js_parse_function_decl2(JSParseState *s,
                                               JSParseFunctionEnum func_type,
                                               JSFunctionKindEnum func_kind,
                                               JSAtom func_name,
                                               const uint8_t *ptr,
                                               int function_line_num,
                                               int function_col_num,
                                               JSParseExportEnum export_flag,
                                               JSFunctionDef **pfd);
static __exception int js_parse_assign_expr2(JSParseState *s, int parse_flags);
static __exception int js_parse_assign_expr(JSParseState *s);
static __exception int js_parse_unary(JSParseState *s, int parse_flags);
static void push_break_entry(JSFunctionDef *fd, BlockEnv *be,
                             JSAtom label_name,
                             int label_break, int label_cont,
                             int drop_count);
static void pop_break_entry(JSFunctionDef *fd);
static JSExportEntry *add_export_entry(JSParseState *s, JSModuleDef *m,
                                       JSAtom local_name, JSAtom export_name,
                                       JSExportTypeEnum export_type);

/* Note: all the fields are already sealed except length */
static int seal_template_obj(JSContext *ctx, JSValue obj)
{
    JSObject *p;
    JSShapeProperty *prs;

    p = JS_VALUE_GET_OBJ(obj);
    prs = find_own_property1(p, JS_ATOM_length);
    if (prs) {
        if (js_update_property_flags(ctx, p, &prs,
                                     prs->flags & ~(JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE)))
            return -1;
    }
    p->extensible = false;
    return 0;
}

static __exception int js_parse_template(JSParseState *s, int call, int *argc)
{
    JSContext *ctx = s->ctx;
    JSValue raw_array, template_object;
    JSToken cooked;
    int depth, ret;

    raw_array = JS_UNDEFINED; /* avoid warning */
    template_object = JS_UNDEFINED; /* avoid warning */
    if (call) {
        /* Create a template object: an array of cooked strings */
        /* Create an array of raw strings and store it to the raw property */
        template_object = JS_NewArray(ctx);
        if (JS_IsException(template_object))
            return -1;
        //        pool_idx = s->cur_func->cpool_count;
        ret = emit_push_const(s, template_object, 0);
        JS_FreeValue(ctx, template_object);
        if (ret)
            return -1;
        raw_array = JS_NewArray(ctx);
        if (JS_IsException(raw_array))
            return -1;
        if (JS_DefinePropertyValue(ctx, template_object, JS_ATOM_raw,
                                   raw_array, JS_PROP_THROW) < 0) {
            return -1;
        }
    }

    depth = 0;
    while (s->token.val == TOK_TEMPLATE) {
        const uint8_t *p = s->token.ptr + 1;
        cooked = s->token;
        if (call) {
            if (JS_DefinePropertyValueUint32(ctx, raw_array, depth,
                                             js_dup(s->token.u.str.str),
                                             JS_PROP_ENUMERABLE | JS_PROP_THROW) < 0) {
                return -1;
            }
            /* re-parse the string with escape sequences but do not throw a
               syntax error if it contains invalid sequences
             */
            if (js_parse_string(s, '`', false, p, &cooked, &p)) {
                cooked.u.str.str = JS_UNDEFINED;
            }
            if (JS_DefinePropertyValueUint32(ctx, template_object, depth,
                                             cooked.u.str.str,
                                             JS_PROP_ENUMERABLE | JS_PROP_THROW) < 0) {
                return -1;
            }
        } else {
            JSString *str;
            /* re-parse the string with escape sequences and throw a
               syntax error if it contains invalid sequences
             */
            JS_FreeValue(ctx, s->token.u.str.str);
            s->token.u.str.str = JS_UNDEFINED;
            if (js_parse_string(s, '`', true, p, &cooked, &p))
                return -1;
            str = JS_VALUE_GET_STRING(cooked.u.str.str);
            if (str->len != 0 || depth == 0) {
                ret = emit_push_const(s, cooked.u.str.str, 1);
                JS_FreeValue(s->ctx, cooked.u.str.str);
                if (ret)
                    return -1;
                if (depth == 0) {
                    if (s->token.u.str.sep == '`')
                        goto done1;
                    emit_op(s, OP_get_field2);
                    emit_atom(s, JS_ATOM_concat);
                }
                depth++;
            } else {
                JS_FreeValue(s->ctx, cooked.u.str.str);
            }
        }
        if (s->token.u.str.sep == '`')
            goto done;
        if (next_token(s))
            return -1;
        if (js_parse_expr(s))
            return -1;
        depth++;
        if (s->token.val != '}') {
            return js_parse_error(s, "expected '}' after template expression");
        }
        /* XXX: should convert to string at this stage? */
        free_token(s, &s->token);
        /* Resume TOK_TEMPLATE parsing (s->token.line_num and
         * s->token.ptr are OK) */
        s->got_lf = false;
        s->last_line_num = s->token.line_num;
        s->last_col_num = s->token.col_num;
        if (js_parse_template_part(s, s->buf_ptr))
            return -1;
    }
    return js_parse_expect(s, TOK_TEMPLATE);

 done:
    if (call) {
        /* Seal the objects */
        seal_template_obj(ctx, raw_array);
        seal_template_obj(ctx, template_object);
        *argc = depth + 1;
    } else {
        emit_op(s, OP_call_method);
        emit_u16(s, depth - 1);
    }
 done1:
    return next_token(s);
}


#define PROP_TYPE_IDENT 0
#define PROP_TYPE_VAR   1
#define PROP_TYPE_GET   2
#define PROP_TYPE_SET   3
#define PROP_TYPE_STAR  4
#define PROP_TYPE_ASYNC 5
#define PROP_TYPE_ASYNC_STAR 6

#define PROP_TYPE_PRIVATE (1 << 4)

static bool token_is_ident(int tok)
{
    /* Accept keywords and reserved words as property names */
    return (tok == TOK_IDENT ||
            (tok >= TOK_FIRST_KEYWORD &&
             tok <= TOK_LAST_KEYWORD));
}

/* if the property is an expression, name = JS_ATOM_NULL */
static int __exception js_parse_property_name(JSParseState *s,
                                              JSAtom *pname,
                                              bool allow_method, bool allow_var,
                                              bool allow_private)
{
    int is_private = 0;
    bool is_non_reserved_ident;
    JSAtom name;
    int prop_type;

    prop_type = PROP_TYPE_IDENT;
    if (allow_method) {
        if (token_is_pseudo_keyword(s, JS_ATOM_get)
        ||  token_is_pseudo_keyword(s, JS_ATOM_set)) {
            /* get x(), set x() */
            name = JS_DupAtom(s->ctx, s->token.u.ident.atom);
            if (next_token(s))
                goto fail1;
            if (s->token.val == ':' || s->token.val == ',' ||
                s->token.val == '}' || s->token.val == '(' ||
                s->token.val == '=' || s->token.val == ';') {
                is_non_reserved_ident = true;
                goto ident_found;
            }
            prop_type = PROP_TYPE_GET + (name == JS_ATOM_set);
            JS_FreeAtom(s->ctx, name);
        } else if (s->token.val == '*') {
            if (next_token(s))
                goto fail;
            prop_type = PROP_TYPE_STAR;
        } else if (token_is_pseudo_keyword(s, JS_ATOM_async) &&
                   peek_token(s, true) != '\n') {
            name = JS_DupAtom(s->ctx, s->token.u.ident.atom);
            if (next_token(s))
                goto fail1;
            if (s->token.val == ':' || s->token.val == ',' ||
                s->token.val == '}' || s->token.val == '(' ||
                s->token.val == '=' || s->token.val == ';') {
                is_non_reserved_ident = true;
                goto ident_found;
            }
            JS_FreeAtom(s->ctx, name);
            if (s->token.val == '*') {
                if (next_token(s))
                    goto fail;
                prop_type = PROP_TYPE_ASYNC_STAR;
            } else {
                prop_type = PROP_TYPE_ASYNC;
            }
        }
    }

    if (token_is_ident(s->token.val)) {
        /* variable can only be a non-reserved identifier */
        is_non_reserved_ident =
            (s->token.val == TOK_IDENT && !s->token.u.ident.is_reserved);
        /* keywords and reserved words have a valid atom */
        name = JS_DupAtom(s->ctx, s->token.u.ident.atom);
        if (next_token(s))
            goto fail1;
    ident_found:
        if (is_non_reserved_ident &&
            prop_type == PROP_TYPE_IDENT && allow_var) {
            if (!(s->token.val == ':' ||
                  (s->token.val == '(' && allow_method))) {
                prop_type = PROP_TYPE_VAR;
            }
        }
    } else if (s->token.val == TOK_STRING) {
        name = JS_ValueToAtom(s->ctx, s->token.u.str.str);
        if (name == JS_ATOM_NULL)
            goto fail;
        if (next_token(s))
            goto fail1;
    } else if (s->token.val == TOK_NUMBER) {
        JSValue val;
        val = s->token.u.num.val;
        name = JS_ValueToAtom(s->ctx, val);
        if (name == JS_ATOM_NULL)
            goto fail;
        if (next_token(s))
            goto fail1;
    } else if (s->token.val == '[') {
        if (next_token(s))
            goto fail;
        if (js_parse_expr(s))
            goto fail;
        if (js_parse_expect(s, ']'))
            goto fail;
        name = JS_ATOM_NULL;
    } else if (s->token.val == TOK_PRIVATE_NAME && allow_private) {
        name = JS_DupAtom(s->ctx, s->token.u.ident.atom);
        if (next_token(s))
            goto fail1;
        is_private = PROP_TYPE_PRIVATE;
    } else {
        goto invalid_prop;
    }
    if (prop_type != PROP_TYPE_IDENT && prop_type != PROP_TYPE_VAR &&
        s->token.val != '(') {
        JS_FreeAtom(s->ctx, name);
    invalid_prop:
        js_parse_error(s, "invalid property name");
        goto fail;
    }
    *pname = name;
    return prop_type | is_private;
 fail1:
    JS_FreeAtom(s->ctx, name);
 fail:
    *pname = JS_ATOM_NULL;
    return -1;
}

typedef struct JSParsePos {
    int last_line_num;
    int last_col_num;
    int line_num;
    int col_num;
    bool got_lf;
    const uint8_t *ptr;
    const uint8_t *eol;
    const uint8_t *mark;
} JSParsePos;

static int js_parse_get_pos(JSParseState *s, JSParsePos *sp)
{
    sp->last_line_num = s->last_line_num;
    sp->last_col_num = s->last_col_num;
    sp->line_num = s->token.line_num;
    sp->col_num = s->token.col_num;
    sp->ptr = s->token.ptr;
    sp->eol = s->eol;
    sp->mark = s->mark;
    sp->got_lf = s->got_lf;
    return 0;
}

static __exception int js_parse_seek_token(JSParseState *s, const JSParsePos *sp)
{
    s->token.line_num = sp->last_line_num;
    s->token.col_num = sp->last_col_num;
    s->line_num = sp->line_num;
    s->col_num = sp->col_num;
    s->buf_ptr = sp->ptr;
    s->eol = sp->eol;
    s->mark = sp->mark;
    s->got_lf = sp->got_lf;
    return next_token(s);
}

/* return true if a regexp literal is allowed after this token */
static bool is_regexp_allowed(int tok)
{
    switch (tok) {
    case TOK_NUMBER:
    case TOK_STRING:
    case TOK_REGEXP:
    case TOK_DEC:
    case TOK_INC:
    case TOK_NULL:
    case TOK_FALSE:
    case TOK_TRUE:
    case TOK_THIS:
    case TOK_PRIVATE_NAME:
    case ')':
    case ']':
    case '}': /* XXX: regexp may occur after */
    case TOK_IDENT:
        return false;
    default:
        return true;
    }
}

#define SKIP_HAS_SEMI       (1 << 0)
#define SKIP_HAS_ELLIPSIS   (1 << 1)
#define SKIP_HAS_ASSIGNMENT (1 << 2)

/* XXX: improve speed with early bailout */
/* XXX: no longer works if regexps are present. Could use previous
   regexp parsing heuristics to handle most cases */
static int js_parse_skip_parens_token(JSParseState *s, int *pbits, bool no_line_terminator)
{
    char state[256];
    size_t level = 0;
    JSParsePos pos;
    int last_tok, tok = TOK_EOF;
    int c, tok_len, bits = 0;

    /* protect from underflow */
    state[level++] = 0;

    js_parse_get_pos(s, &pos);
    last_tok = 0;
    for (;;) {
        switch(s->token.val) {
        case '(':
        case '[':
        case '{':
            if (level >= sizeof(state))
                goto done;
            state[level++] = s->token.val;
            break;
        case ')':
            if (state[--level] != '(')
                goto done;
            break;
        case ']':
            if (state[--level] != '[')
                goto done;
            break;
        case '}':
            c = state[--level];
            if (c == '`') {
                /* continue the parsing of the template */
                free_token(s, &s->token);
                /* Resume TOK_TEMPLATE parsing (s->token.line_num and
                 * s->token.ptr are OK) */
                s->got_lf = false;
                s->last_line_num = s->token.line_num;
                s->last_col_num = s->token.col_num;
                if (js_parse_template_part(s, s->buf_ptr))
                    goto done;
                goto handle_template;
            } else if (c != '{') {
                goto done;
            }
            break;
        case TOK_TEMPLATE:
        handle_template:
            if (s->token.u.str.sep != '`') {
                /* '${' inside the template : closing '}' and continue
                   parsing the template */
                if (level >= sizeof(state))
                    goto done;
                state[level++] = '`';
            }
            break;
        case TOK_EOF:
            goto done;
        case ';':
            if (level == 2) {
                bits |= SKIP_HAS_SEMI;
            }
            break;
        case TOK_ELLIPSIS:
            if (level == 2) {
                bits |= SKIP_HAS_ELLIPSIS;
            }
            break;
        case '=':
            bits |= SKIP_HAS_ASSIGNMENT;
            break;

        case TOK_DIV_ASSIGN:
            tok_len = 2;
            goto parse_regexp;
        case '/':
            tok_len = 1;
        parse_regexp:
            if (is_regexp_allowed(last_tok)) {
                s->buf_ptr -= tok_len;
                if (js_parse_regexp(s)) {
                    /* XXX: should clear the exception */
                    goto done;
                }
            }
            break;
        }
        /* last_tok is only used to recognize regexps */
        if (s->token.val == TOK_IDENT &&
            (token_is_pseudo_keyword(s, JS_ATOM_of) ||
             token_is_pseudo_keyword(s, JS_ATOM_yield))) {
            last_tok = TOK_OF;
        } else {
            last_tok = s->token.val;
        }
        if (next_token(s)) {
            /* XXX: should clear the exception generated by next_token() */
            break;
        }
        if (level <= 1) {
            tok = s->token.val;
            if (token_is_pseudo_keyword(s, JS_ATOM_of))
                tok = TOK_OF;
            if (no_line_terminator && s->last_line_num != s->token.line_num)
                tok = '\n';
            break;
        }
    }
 done:
    if (pbits) {
        *pbits = bits;
    }
    if (js_parse_seek_token(s, &pos))
        return -1;
    return tok;
}

static void set_object_name(JSParseState *s, JSAtom name)
{
    JSFunctionDef *fd = s->cur_func;
    int opcode;

    opcode = get_prev_opcode(fd);
    if (opcode == OP_set_name) {
        /* XXX: should free atom after OP_set_name? */
        fd->byte_code.size = fd->last_opcode_pos;
        fd->last_opcode_pos = -1;
        emit_op(s, OP_set_name);
        emit_atom(s, name);
    } else if (opcode == OP_set_class_name) {
        int define_class_pos;
        JSAtom atom;
        define_class_pos = fd->last_opcode_pos + 1 -
            get_u32(fd->byte_code.buf + fd->last_opcode_pos + 1);
        assert(fd->byte_code.buf[define_class_pos] == OP_define_class);
        /* for consistency we free the previous atom which is
           JS_ATOM_empty_string */
        atom = get_u32(fd->byte_code.buf + define_class_pos + 1);
        JS_FreeAtom(s->ctx, atom);
        put_u32(fd->byte_code.buf + define_class_pos + 1,
                JS_DupAtom(s->ctx, name));
        fd->last_opcode_pos = -1;
    }
}

static void set_object_name_computed(JSParseState *s)
{
    JSFunctionDef *fd = s->cur_func;
    int opcode;

    opcode = get_prev_opcode(fd);
    if (opcode == OP_set_name) {
        /* XXX: should free atom after OP_set_name? */
        fd->byte_code.size = fd->last_opcode_pos;
        fd->last_opcode_pos = -1;
        emit_op(s, OP_set_name_computed);
    } else if (opcode == OP_set_class_name) {
        int define_class_pos;
        define_class_pos = fd->last_opcode_pos + 1 -
            get_u32(fd->byte_code.buf + fd->last_opcode_pos + 1);
        assert(fd->byte_code.buf[define_class_pos] == OP_define_class);
        fd->byte_code.buf[define_class_pos] = OP_define_class_computed;
        fd->last_opcode_pos = -1;
    }
}

static __exception int js_parse_object_literal(JSParseState *s)
{
    JSAtom name = JS_ATOM_NULL;
    const uint8_t *start_ptr;
    int start_line, start_col, prop_type;
    bool has_proto;

    if (next_token(s))
        goto fail;
    /* XXX: add an initial length that will be patched back */
    emit_op(s, OP_object);
    has_proto = false;
    while (s->token.val != '}') {
        /* specific case for getter/setter */
        start_ptr = s->token.ptr;
        start_line = s->token.line_num;
        start_col = s->token.col_num;

        if (s->token.val == TOK_ELLIPSIS) {
            if (next_token(s))
                return -1;
            if (js_parse_assign_expr(s))
                return -1;
            emit_op(s, OP_null);  /* dummy excludeList */
            emit_op(s, OP_copy_data_properties);
            emit_u8(s, 2 | (1 << 2) | (0 << 5));
            emit_op(s, OP_drop); /* pop excludeList */
            emit_op(s, OP_drop); /* pop src object */
            goto next;
        }

        prop_type = js_parse_property_name(s, &name, true, true, false);
        if (prop_type < 0)
            goto fail;

        if (prop_type == PROP_TYPE_VAR) {
            /* shortcut for x: x */
            emit_op(s, OP_scope_get_var);
            emit_atom(s, name);
            emit_u16(s, s->cur_func->scope_level);
            emit_op(s, OP_define_field);
            emit_atom(s, name);
        } else if (s->token.val == '(') {
            bool is_getset = (prop_type == PROP_TYPE_GET ||
                              prop_type == PROP_TYPE_SET);
            JSParseFunctionEnum func_type;
            JSFunctionKindEnum func_kind;
            int op_flags;

            func_kind = JS_FUNC_NORMAL;
            if (is_getset) {
                func_type = JS_PARSE_FUNC_GETTER + prop_type - PROP_TYPE_GET;
            } else {
                func_type = JS_PARSE_FUNC_METHOD;
                if (prop_type == PROP_TYPE_STAR)
                    func_kind = JS_FUNC_GENERATOR;
                else if (prop_type == PROP_TYPE_ASYNC)
                    func_kind = JS_FUNC_ASYNC;
                else if (prop_type == PROP_TYPE_ASYNC_STAR)
                    func_kind = JS_FUNC_ASYNC_GENERATOR;
            }
            if (js_parse_function_decl(s, func_type, func_kind, JS_ATOM_NULL,
                                       start_ptr, start_line, start_col))
                goto fail;
            if (name == JS_ATOM_NULL) {
                emit_op(s, OP_define_method_computed);
            } else {
                emit_op(s, OP_define_method);
                emit_atom(s, name);
            }
            if (is_getset) {
                op_flags = OP_DEFINE_METHOD_GETTER +
                    prop_type - PROP_TYPE_GET;
            } else {
                op_flags = OP_DEFINE_METHOD_METHOD;
            }
            emit_u8(s, op_flags | OP_DEFINE_METHOD_ENUMERABLE);
        } else {
            if (js_parse_expect(s, ':'))
                goto fail;
            if (js_parse_assign_expr(s))
                goto fail;
            if (name == JS_ATOM_NULL) {
                set_object_name_computed(s);
                emit_op(s, OP_define_array_el);
                emit_op(s, OP_drop);
            } else if (name == JS_ATOM___proto__) {
                if (has_proto) {
                    js_parse_error(s, "duplicate __proto__ property name");
                    goto fail;
                }
                emit_op(s, OP_set_proto);
                has_proto = true;
            } else {
                set_object_name(s, name);
                emit_op(s, OP_define_field);
                emit_atom(s, name);
            }
        }
        JS_FreeAtom(s->ctx, name);
    next:
        name = JS_ATOM_NULL;
        if (s->token.val != ',')
            break;
        if (next_token(s))
            goto fail;
    }
    if (js_parse_expect(s, '}'))
        goto fail;
    return 0;
 fail:
    JS_FreeAtom(s->ctx, name);
    return -1;
}

/* allow the 'in' binary operator */
#define PF_IN_ACCEPTED  (1 << 0)
/* allow function calls parsing in js_parse_postfix_expr() */
#define PF_POSTFIX_CALL (1 << 1)
/* allow the exponentiation operator in js_parse_unary() */
#define PF_POW_ALLOWED  (1 << 2)
/* forbid the exponentiation operator in js_parse_unary() */
#define PF_POW_FORBIDDEN (1 << 3)
#define PF_AWAIT_USING   (1 << 4)

static __exception int js_parse_postfix_expr(JSParseState *s, int parse_flags);

static __exception int js_parse_left_hand_side_expr(JSParseState *s)
{
    return js_parse_postfix_expr(s, PF_POSTFIX_CALL);
}

/* find field in the current scope */
static int find_private_class_field(JSContext *ctx, JSFunctionDef *fd,
                                    JSAtom name, int scope_level)
{
    int idx;
    idx = fd->scopes[scope_level].first;
    while (idx != -1) {
        if (fd->vars[idx].scope_level != scope_level)
            break;
        if (fd->vars[idx].var_name == name)
            return idx;
        idx = fd->vars[idx].scope_next;
    }
    return -1;
}

/* initialize the class fields, called by the constructor. Note:
   super() can be called in an arrow function, so <this> and
   <class_fields_init> can be variable references */
static void emit_class_field_init(JSParseState *s)
{
    int label_next;

    emit_op(s, OP_scope_get_var);
    emit_atom(s, JS_ATOM_class_fields_init);
    emit_u16(s, s->cur_func->scope_level);

    /* no need to call the class field initializer if not defined */
    emit_op(s, OP_dup);
    label_next = emit_goto(s, OP_if_false, -1);

    emit_op(s, OP_scope_get_var);
    emit_atom(s, JS_ATOM_this);
    emit_u16(s, 0);

    emit_op(s, OP_swap);

    emit_op(s, OP_call_method);
    emit_u16(s, 0);

    emit_label(s, label_next);
    emit_op(s, OP_drop);
}

/* build a private setter function name from the private getter name */
static JSAtom get_private_setter_name(JSContext *ctx, JSAtom name)
{
    return js_atom_concat_str(ctx, name, "<set>");
}

typedef struct {
    JSFunctionDef *fields_init_fd;
    int computed_fields_count;
    bool need_brand;
    int brand_push_pos;
    bool is_static;
} ClassFieldsDef;

static __exception int emit_class_init_start(JSParseState *s,
                                             ClassFieldsDef *cf)
{
    int label_add_brand;

    cf->fields_init_fd = js_parse_function_class_fields_init(s);
    if (!cf->fields_init_fd)
        return -1;

    s->cur_func = cf->fields_init_fd;

    if (!cf->is_static) {
        /* add the brand to the newly created instance */
        /* XXX: would be better to add the code only if needed, maybe in a
           later pass */
        emit_op(s, OP_push_false); /* will be patched later */
        cf->brand_push_pos = cf->fields_init_fd->last_opcode_pos;
        label_add_brand = emit_goto(s, OP_if_false, -1);

        emit_op(s, OP_scope_get_var);
        emit_atom(s, JS_ATOM_this);
        emit_u16(s, 0);

        emit_op(s, OP_scope_get_var);
        emit_atom(s, JS_ATOM_home_object);
        emit_u16(s, 0);

        emit_op(s, OP_add_brand);

        emit_label(s, label_add_brand);
    }
    s->cur_func = s->cur_func->parent;
    return 0;
}

static void emit_class_init_end(JSParseState *s, ClassFieldsDef *cf)
{
    int cpool_idx;

    s->cur_func = cf->fields_init_fd;
    emit_op(s, OP_return_undef);
    s->cur_func = s->cur_func->parent;

    cpool_idx = cpool_add(s, JS_NULL);
    cf->fields_init_fd->parent_cpool_idx = cpool_idx;
    emit_op(s, OP_fclosure);
    emit_u32(s, cpool_idx);
    emit_op(s, OP_set_home_object);
}

static void emit_return(JSParseState *s, bool hasval);

static JSFunctionDef *js_new_function_def(JSContext *ctx,
                                          JSFunctionDef *parent,
                                          bool is_eval,
                                          bool is_func_expr,
                                          const char *filename,
                                          int line_num,
                                          int col_num);

static __exception int js_parse_class_default_ctor(JSParseState *s,
                                                   bool has_super,
                                                   JSFunctionDef **pfd)
{
    JSParseFunctionEnum func_type;
    JSFunctionDef *fd = s->cur_func;

    fd = js_new_function_def(s->ctx, fd, false, false, s->filename,
                             s->token.line_num, s->token.col_num);
    if (!fd)
        return -1;

    s->cur_func = fd;
    fd->has_home_object = true;
    fd->super_allowed = true;
    fd->has_prototype = false;
    fd->has_this_binding = true;
    fd->new_target_allowed = true;

    push_scope(s);  /* enter body scope */
    fd->body_scope = fd->scope_level;
    if (has_super) {
        fd->is_derived_class_constructor = true;
        fd->super_call_allowed = true;
        fd->arguments_allowed = true;
        fd->has_arguments_binding = true;
        func_type = JS_PARSE_FUNC_DERIVED_CLASS_CONSTRUCTOR;
        emit_op(s, OP_init_ctor);
        // TODO(bnoordhuis) roll into OP_init_ctor
        emit_op(s, OP_scope_put_var_init);
        emit_atom(s, JS_ATOM_this);
        emit_u16(s, 0);
        emit_class_field_init(s);
    } else {
        func_type = JS_PARSE_FUNC_CLASS_CONSTRUCTOR;
        /* error if not invoked as a constructor */
        emit_op(s, OP_check_ctor);
        emit_class_field_init(s);
    }

    fd->func_kind = JS_FUNC_NORMAL;
    fd->func_type = func_type;
    emit_return(s, false);

    s->cur_func = fd->parent;
    if (pfd)
        *pfd = fd;

    int idx;
    /* the real object will be set at the end of the compilation */
    idx = cpool_add(s, JS_NULL);
    fd->parent_cpool_idx = idx;

    return 0;
}


static __exception int js_parse_class(JSParseState *s, bool is_class_expr,
                                      JSParseExportEnum export_flag)
{
    JSContext *ctx = s->ctx;
    JSFunctionDef *fd = s->cur_func;
    JSAtom name = JS_ATOM_NULL, class_name = JS_ATOM_NULL, class_name1;
    JSAtom class_var_name = JS_ATOM_NULL;
    JSFunctionDef *method_fd, *ctor_fd;
    int class_name_var_idx, prop_type, ctor_cpool_offset;
    int class_flags = 0, i, define_class_offset;
    bool is_static, is_private, is_strict_mode;
    const uint8_t *class_start_ptr = s->token.ptr;
    const uint8_t *start_ptr;
    ClassFieldsDef class_fields[2];

    /* classes are parsed and executed in strict mode */
    is_strict_mode = fd->is_strict_mode;
    fd->is_strict_mode = true;
    if (next_token(s))
        goto fail;
    if (s->token.val == TOK_IDENT) {
        if (s->token.u.ident.is_reserved) {
            js_parse_error_reserved_identifier(s);
            goto fail;
        }
        class_name = JS_DupAtom(ctx, s->token.u.ident.atom);
        if (next_token(s))
            goto fail;
    } else if (!is_class_expr && export_flag != JS_PARSE_EXPORT_DEFAULT) {
        js_parse_error(s, "class statement requires a name");
        goto fail;
    }
    if (!is_class_expr) {
        if (class_name == JS_ATOM_NULL)
            class_var_name = JS_ATOM__default_; /* export default */
        else
            class_var_name = class_name;
        class_var_name = JS_DupAtom(ctx, class_var_name);
    }

    push_scope(s);

    if (s->token.val == TOK_EXTENDS) {
        class_flags = JS_DEFINE_CLASS_HAS_HERITAGE;
        if (next_token(s))
            goto fail;
        if (js_parse_left_hand_side_expr(s))
            goto fail;
    } else {
        emit_op(s, OP_undefined);
    }

    /* add a 'const' definition for the class name */
    if (class_name != JS_ATOM_NULL) {
        class_name_var_idx = define_var(s, fd, class_name, JS_VAR_DEF_CONST);
        if (class_name_var_idx < 0)
            goto fail;
    }

    if (js_parse_expect(s, '{'))
        goto fail;

    /* this scope contains the private fields */
    push_scope(s);

    emit_op(s, OP_push_const);
    ctor_cpool_offset = fd->byte_code.size;
    emit_u32(s, 0); /* will be patched at the end of the class parsing */

    if (class_name == JS_ATOM_NULL) {
        if (class_var_name != JS_ATOM_NULL)
            class_name1 = JS_ATOM_default;
        else
            class_name1 = JS_ATOM_empty_string;
    } else {
        class_name1 = class_name;
    }

    emit_op(s, OP_define_class);
    emit_atom(s, class_name1);
    emit_u8(s, class_flags);
    define_class_offset = fd->last_opcode_pos;

    for(i = 0; i < 2; i++) {
        ClassFieldsDef *cf = &class_fields[i];
        cf->fields_init_fd = NULL;
        cf->computed_fields_count = 0;
        cf->need_brand = false;
        cf->is_static = i;
    }

    ctor_fd = NULL;
    while (s->token.val != '}') {
        if (s->token.val == ';') {
            if (next_token(s))
                goto fail;
            continue;
        }
        is_static = false;
        if (s->token.val == TOK_STATIC) {
            int next = peek_token(s, true);
            if (!(next == ';' || next == '}' || next == '(' || next == '='))
                is_static = true;
        }
        prop_type = -1;
        if (is_static) {
            if (next_token(s))
                goto fail;
            if (s->token.val == '{') {
                ClassFieldsDef *cf = &class_fields[is_static];
                if (!cf->fields_init_fd)
                    if (emit_class_init_start(s, cf))
                        goto fail;
                s->cur_func = cf->fields_init_fd;
                // stack is now: <empty>
                JSFunctionDef *init;
                if (js_parse_function_decl2(s, JS_PARSE_FUNC_CLASS_STATIC_INIT,
                                            JS_FUNC_NORMAL, JS_ATOM_NULL,
                                            s->token.ptr,
                                            s->token.line_num,
                                            s->token.col_num,
                                            JS_PARSE_EXPORT_NONE, &init) < 0) {
                    goto fail;
                }
                // stack is now: fclosure
                push_scope(s);
                emit_op(s, OP_scope_get_var);
                emit_atom(s, JS_ATOM_this);
                emit_u16(s, 0);
                // stack is now: fclosure this
                if (class_name != JS_ATOM_NULL) {
                    // TODO(bnoordhuis) pass as argument to init method?
                    emit_op(s, OP_dup);
                    emit_op(s, OP_scope_put_var_init);
                    emit_atom(s, class_name);
                    emit_u16(s, s->cur_func->scope_level);
                }
                emit_op(s, OP_swap);
                // stack is now: this fclosure
                emit_op(s, OP_call_method);
                emit_u16(s, 0);
                // stack is now: returnvalue
                emit_op(s, OP_drop);
                // stack is now: <empty>
                pop_scope(s);
                s->cur_func = s->cur_func->parent;
                continue;
            }
            /* allow "static" field name */
            if (s->token.val == ';' || s->token.val == '=') {
                is_static = false;
                name = JS_DupAtom(ctx, JS_ATOM_static);
                prop_type = PROP_TYPE_IDENT;
            }
        }
        if (is_static)
            emit_op(s, OP_swap);
        start_ptr = s->token.ptr;
        if (prop_type < 0) {
            prop_type = js_parse_property_name(s, &name, true, false, true);
            if (prop_type < 0)
                goto fail;
        }
        is_private = prop_type & PROP_TYPE_PRIVATE;
        prop_type &= ~PROP_TYPE_PRIVATE;

        if ((name == JS_ATOM_constructor && !is_static &&
             prop_type != PROP_TYPE_IDENT) ||
            (name == JS_ATOM_prototype && is_static) ||
            name == JS_ATOM_hash_constructor) {
            js_parse_error(s, "invalid method name");
            goto fail;
        }
        if (prop_type == PROP_TYPE_GET || prop_type == PROP_TYPE_SET) {
            bool is_set = prop_type - PROP_TYPE_GET;
            JSFunctionDef *method_fd;

            if (is_private) {
                int idx, var_kind, is_static1;
                idx = find_private_class_field(ctx, fd, name, fd->scope_level);
                if (idx >= 0) {
                    var_kind = fd->vars[idx].var_kind;
                    is_static1 = fd->vars[idx].is_static_private;
                    if (var_kind == JS_VAR_PRIVATE_FIELD ||
                        var_kind == JS_VAR_PRIVATE_METHOD ||
                        var_kind == JS_VAR_PRIVATE_GETTER_SETTER ||
                        var_kind == (JS_VAR_PRIVATE_GETTER + is_set) ||
                        (var_kind == (JS_VAR_PRIVATE_GETTER + 1 - is_set) &&
                         is_static != is_static1)) {
                        goto private_field_already_defined;
                    }
                    fd->vars[idx].var_kind = JS_VAR_PRIVATE_GETTER_SETTER;
                } else {
                    if (add_private_class_field(s, fd, name,
                                                JS_VAR_PRIVATE_GETTER + is_set, is_static) < 0)
                        goto fail;
                }
                class_fields[is_static].need_brand = true;
            }

            if (js_parse_function_decl2(s, JS_PARSE_FUNC_GETTER + is_set,
                                        JS_FUNC_NORMAL, JS_ATOM_NULL,
                                        start_ptr,
                                        s->token.line_num,
                                        s->token.col_num,
                                        JS_PARSE_EXPORT_NONE, &method_fd))
                goto fail;
            if (is_private) {
                method_fd->need_home_object = true; /* needed for brand check */
                emit_op(s, OP_set_home_object);
                /* XXX: missing function name */
                emit_op(s, OP_scope_put_var_init);
                if (is_set) {
                    JSAtom setter_name;
                    int ret;

                    setter_name = get_private_setter_name(ctx, name);
                    if (setter_name == JS_ATOM_NULL)
                        goto fail;
                    emit_atom(s, setter_name);
                    ret = add_private_class_field(s, fd, setter_name,
                                                  JS_VAR_PRIVATE_SETTER, is_static);
                    JS_FreeAtom(ctx, setter_name);
                    if (ret < 0)
                        goto fail;
                } else {
                    emit_atom(s, name);
                }
                emit_u16(s, s->cur_func->scope_level);
            } else {
                if (name == JS_ATOM_NULL) {
                    emit_op(s, OP_define_method_computed);
                } else {
                    emit_op(s, OP_define_method);
                    emit_atom(s, name);
                }
                emit_u8(s, OP_DEFINE_METHOD_GETTER + is_set);
            }
        } else if (prop_type == PROP_TYPE_IDENT && s->token.val != '(') {
            ClassFieldsDef *cf = &class_fields[is_static];
            JSAtom field_var_name = JS_ATOM_NULL;

            /* class field */

            /* XXX: spec: not consistent with method name checks */
            if (name == JS_ATOM_constructor || name == JS_ATOM_prototype) {
                js_parse_error(s, "invalid field name");
                goto fail;
            }

            if (is_private) {
                if (find_private_class_field(ctx, fd, name,
                                             fd->scope_level) >= 0) {
                    goto private_field_already_defined;
                }
                if (add_private_class_field(s, fd, name,
                                            JS_VAR_PRIVATE_FIELD, is_static) < 0)
                    goto fail;
                emit_op(s, OP_private_symbol);
                emit_atom(s, name);
                emit_op(s, OP_scope_put_var_init);
                emit_atom(s, name);
                emit_u16(s, s->cur_func->scope_level);
            }

            if (!cf->fields_init_fd) {
                if (emit_class_init_start(s, cf))
                    goto fail;
            }
            if (name == JS_ATOM_NULL ) {
                /* save the computed field name into a variable */
                field_var_name = js_atom_concat_num(ctx, JS_ATOM_computed_field + is_static, cf->computed_fields_count);
                if (field_var_name == JS_ATOM_NULL)
                    goto fail;
                if (define_var(s, fd, field_var_name, JS_VAR_DEF_CONST) < 0) {
                    JS_FreeAtom(ctx, field_var_name);
                    goto fail;
                }
                emit_op(s, OP_to_propkey);
                emit_op(s, OP_scope_put_var_init);
                emit_atom(s, field_var_name);
                emit_u16(s, s->cur_func->scope_level);
            }
            s->cur_func = cf->fields_init_fd;
            emit_op(s, OP_scope_get_var);
            emit_atom(s, JS_ATOM_this);
            emit_u16(s, 0);

            // expose class name to static initializers
            if (is_static && class_name != JS_ATOM_NULL) {
                emit_op(s, OP_dup);
                emit_op(s, OP_scope_put_var_init);
                emit_atom(s, class_name);
                emit_u16(s, s->cur_func->scope_level);
            }

            if (name == JS_ATOM_NULL) {
                emit_op(s, OP_scope_get_var);
                emit_atom(s, field_var_name);
                emit_u16(s, s->cur_func->scope_level);
                cf->computed_fields_count++;
                JS_FreeAtom(ctx, field_var_name);
            } else if (is_private) {
                emit_op(s, OP_scope_get_var);
                emit_atom(s, name);
                emit_u16(s, s->cur_func->scope_level);
            }

            if (s->token.val == '=') {
                if (next_token(s))
                    goto fail;
                if (js_parse_assign_expr(s))
                    goto fail;
            } else {
                emit_op(s, OP_undefined);
            }
            if (is_private) {
                set_object_name_computed(s);
                emit_op(s, OP_define_private_field);
            } else if (name == JS_ATOM_NULL) {
                set_object_name_computed(s);
                emit_op(s, OP_define_array_el);
                emit_op(s, OP_drop);
            } else {
                set_object_name(s, name);
                emit_op(s, OP_define_field);
                emit_atom(s, name);
            }
            s->cur_func = s->cur_func->parent;
            if (js_parse_expect_semi(s))
                goto fail;
        } else {
            JSParseFunctionEnum func_type;
            JSFunctionKindEnum func_kind;

            func_type = JS_PARSE_FUNC_METHOD;
            func_kind = JS_FUNC_NORMAL;
            if (prop_type == PROP_TYPE_STAR) {
                func_kind = JS_FUNC_GENERATOR;
            } else if (prop_type == PROP_TYPE_ASYNC) {
                func_kind = JS_FUNC_ASYNC;
            } else if (prop_type == PROP_TYPE_ASYNC_STAR) {
                func_kind = JS_FUNC_ASYNC_GENERATOR;
            } else if (name == JS_ATOM_constructor && !is_static) {
                if (ctor_fd) {
                    js_parse_error(s, "property constructor appears more than once");
                    goto fail;
                }
                if (class_flags & JS_DEFINE_CLASS_HAS_HERITAGE)
                    func_type = JS_PARSE_FUNC_DERIVED_CLASS_CONSTRUCTOR;
                else
                    func_type = JS_PARSE_FUNC_CLASS_CONSTRUCTOR;
            }
            if (is_private) {
                class_fields[is_static].need_brand = true;
            }
            if (js_parse_function_decl2(s, func_type, func_kind, JS_ATOM_NULL,
                                        start_ptr,
                                        s->token.line_num,
                                        s->token.col_num,
                                        JS_PARSE_EXPORT_NONE, &method_fd))
                goto fail;
            if (func_type == JS_PARSE_FUNC_DERIVED_CLASS_CONSTRUCTOR ||
                func_type == JS_PARSE_FUNC_CLASS_CONSTRUCTOR) {
                ctor_fd = method_fd;
            } else if (is_private) {
                method_fd->need_home_object = true; /* needed for brand check */
                if (find_private_class_field(ctx, fd, name,
                                             fd->scope_level) >= 0) {
                private_field_already_defined:
                    js_parse_error(s, "private class field is already defined");
                    goto fail;
                }
                if (add_private_class_field(s, fd, name,
                                            JS_VAR_PRIVATE_METHOD, is_static) < 0)
                    goto fail;
                emit_op(s, OP_set_home_object);
                emit_op(s, OP_set_name);
                emit_atom(s, name);
                emit_op(s, OP_scope_put_var_init);
                emit_atom(s, name);
                emit_u16(s, s->cur_func->scope_level);
            } else {
                if (name == JS_ATOM_NULL) {
                    emit_op(s, OP_define_method_computed);
                } else {
                    emit_op(s, OP_define_method);
                    emit_atom(s, name);
                }
                emit_u8(s, OP_DEFINE_METHOD_METHOD);
            }
        }
        if (is_static)
            emit_op(s, OP_swap);
        JS_FreeAtom(ctx, name);
        name = JS_ATOM_NULL;
    }

    if (s->token.val != '}') {
        js_parse_error(s, "expecting '%c'", '}');
        goto fail;
    }

    if (!ctor_fd) {
        if (js_parse_class_default_ctor(s, class_flags & JS_DEFINE_CLASS_HAS_HERITAGE, &ctor_fd))
            goto fail;
    }
    /* patch the constant pool index for the constructor */
    put_u32(fd->byte_code.buf + ctor_cpool_offset, ctor_fd->parent_cpool_idx);

    /* store the class source code in the constructor. */
    js_free(ctx, ctor_fd->source);
    ctor_fd->source_len = s->buf_ptr - class_start_ptr;
    ctor_fd->source = js_strndup(ctx, (const char *)class_start_ptr, ctor_fd->source_len);
    if (!ctor_fd->source)
        goto fail;

    /* consume the '}' */
    if (next_token(s))
        goto fail;

    {
        ClassFieldsDef *cf = &class_fields[0];
        int var_idx;

        if (cf->need_brand) {
            /* add a private brand to the prototype */
            emit_op(s, OP_dup);
            emit_op(s, OP_null);
            emit_op(s, OP_swap);
            emit_op(s, OP_add_brand);

            /* define the brand field in 'this' of the initializer */
            if (!cf->fields_init_fd) {
                if (emit_class_init_start(s, cf))
                    goto fail;
            }
            /* patch the start of the function to enable the
               OP_add_brand_instance code */
            cf->fields_init_fd->byte_code.buf[cf->brand_push_pos] = OP_push_true;
        }

        /* store the function to initialize the fields to that it can be
           referenced by the constructor */
        var_idx = define_var(s, fd, JS_ATOM_class_fields_init,
                             JS_VAR_DEF_CONST);
        if (var_idx < 0)
            goto fail;
        if (cf->fields_init_fd) {
            emit_class_init_end(s, cf);
        } else {
            emit_op(s, OP_undefined);
        }
        emit_op(s, OP_scope_put_var_init);
        emit_atom(s, JS_ATOM_class_fields_init);
        emit_u16(s, s->cur_func->scope_level);
    }

    /* drop the prototype */
    emit_op(s, OP_drop);

    if (class_fields[1].need_brand) {
        /* add a private brand to the class */
        emit_op(s, OP_dup);
        emit_op(s, OP_dup);
        emit_op(s, OP_add_brand);
    }

    /* initialize the static fields */
    if (class_fields[1].fields_init_fd != NULL) {
        ClassFieldsDef *cf = &class_fields[1];
        emit_op(s, OP_dup);
        emit_class_init_end(s, cf);
        emit_op(s, OP_call_method);
        emit_u16(s, 0);
        emit_op(s, OP_drop);
    }

    if (class_name != JS_ATOM_NULL) {
        /* store the class name in the scoped class name variable (it
           is independent from the class statement variable
           definition) */
        emit_op(s, OP_dup);
        emit_op(s, OP_scope_put_var_init);
        emit_atom(s, class_name);
        emit_u16(s, fd->scope_level);
    }
    pop_scope(s);
    pop_scope(s);

    /* the class statements have a block level scope */
    if (class_var_name != JS_ATOM_NULL) {
        if (define_var(s, fd, class_var_name, JS_VAR_DEF_LET) < 0)
            goto fail;
        emit_op(s, OP_scope_put_var_init);
        emit_atom(s, class_var_name);
        emit_u16(s, fd->scope_level);
    } else {
        if (class_name == JS_ATOM_NULL) {
            /* cannot use OP_set_name because the name of the class
               must be defined before the static initializers are
               executed */
            emit_op(s, OP_set_class_name);
            emit_u32(s, fd->last_opcode_pos + 1 - define_class_offset);
        }
    }

    if (export_flag != JS_PARSE_EXPORT_NONE) {
        if (!add_export_entry(s, fd->module,
                              class_var_name,
                              export_flag == JS_PARSE_EXPORT_NAMED ? class_var_name : JS_ATOM_default,
                              JS_EXPORT_TYPE_LOCAL))
            goto fail;
    }

    JS_FreeAtom(ctx, class_name);
    JS_FreeAtom(ctx, class_var_name);
    fd->is_strict_mode = is_strict_mode;
    return 0;
 fail:
    JS_FreeAtom(ctx, name);
    JS_FreeAtom(ctx, class_name);
    JS_FreeAtom(ctx, class_var_name);
    fd->is_strict_mode = is_strict_mode;
    return -1;
}

static __exception int js_parse_array_literal(JSParseState *s)
{
    uint32_t idx;
    bool need_length;

    if (next_token(s))
        return -1;
    /* small regular arrays are created on the stack */
    idx = 0;
    while (s->token.val != ']' && idx < 32) {
        if (s->token.val == ',' || s->token.val == TOK_ELLIPSIS)
            break;
        if (js_parse_assign_expr(s))
            return -1;
        idx++;
        /* accept trailing comma */
        if (s->token.val == ',') {
            if (next_token(s))
                return -1;
        } else
        if (s->token.val != ']')
            goto done;
    }
    emit_op(s, OP_array_from);
    emit_u16(s, idx);

    /* larger arrays and holes are handled with explicit indices */
    need_length = false;
    while (s->token.val != ']' && idx < 0x7fffffff) {
        if (s->token.val == TOK_ELLIPSIS)
            break;
        need_length = true;
        if (s->token.val != ',') {
            if (js_parse_assign_expr(s))
                return -1;
            emit_op(s, OP_define_field);
            emit_u32(s, __JS_AtomFromUInt32(idx));
            need_length = false;
        }
        idx++;
        /* accept trailing comma */
        if (s->token.val == ',') {
            if (next_token(s))
                return -1;
        }
    }
    if (s->token.val == ']') {
        if (need_length) {
            /* Set the length: Cannot use OP_define_field because
               length is not configurable */
            emit_op(s, OP_dup);
            emit_op(s, OP_push_i32);
            emit_u32(s, idx);
            emit_op(s, OP_put_field);
            emit_atom(s, JS_ATOM_length);
        }
        goto done;
    }

    /* huge arrays and spread elements require a dynamic index on the stack */
    emit_op(s, OP_push_i32);
    emit_u32(s, idx);

    /* stack has array, index */
    while (s->token.val != ']') {
        if (s->token.val == TOK_ELLIPSIS) {
            if (next_token(s))
                return -1;
            if (js_parse_assign_expr(s))
                return -1;
            emit_op(s, OP_append);
        } else {
            need_length = true;
            if (s->token.val != ',') {
                if (js_parse_assign_expr(s))
                    return -1;
                /* a idx val */
                emit_op(s, OP_define_array_el);
                need_length = false;
            }
            emit_op(s, OP_inc);
        }
        if (s->token.val != ',')
            break;
        if (next_token(s))
            return -1;
    }
    if (need_length) {
        /* Set the length: cannot use OP_define_field because
           length is not configurable */
        emit_op(s, OP_dup1);    /* array length - array array length */
        emit_op(s, OP_put_field);
        emit_atom(s, JS_ATOM_length);
    } else {
        emit_op(s, OP_drop);    /* array length - array */
    }
done:
    return js_parse_expect(s, ']');
}

/* check if scope chain contains a with statement */
static bool has_with_scope(JSFunctionDef *s, int scope_level)
{
    while (s) {
        /* no with in strict mode */
        if (!s->is_strict_mode) {
            int scope_idx = s->scopes[scope_level].first;
            while (scope_idx >= 0) {
                JSVarDef *vd = &s->vars[scope_idx];
                if (vd->var_name == JS_ATOM__with_)
                    return true;
                scope_idx = vd->scope_next;
            }
        }
        /* check parent scopes */
        scope_level = s->parent_scope_level;
        s = s->parent;
    }
    return false;
}

static __exception int get_lvalue(JSParseState *s, int *popcode, int *pscope,
                                  JSAtom *pname, int *plabel, int *pdepth, bool keep,
                                  int tok)
{
    JSFunctionDef *fd;
    int opcode, scope, label, depth;
    JSAtom name;

    /* we check the last opcode to get the lvalue type */
    fd = s->cur_func;
    scope = 0;
    name = JS_ATOM_NULL;
    label = -1;
    depth = 0;
    switch(opcode = get_prev_opcode(fd)) {
    case OP_scope_get_var:
        name = get_u32(fd->byte_code.buf + fd->last_opcode_pos + 1);
        scope = get_u16(fd->byte_code.buf + fd->last_opcode_pos + 5);
        if ((name == JS_ATOM_arguments || name == JS_ATOM_eval) &&
            fd->is_strict_mode) {
            return js_parse_error(s, "invalid lvalue in strict mode");
        }
        if (name == JS_ATOM_this || name == JS_ATOM_new_target)
            goto invalid_lvalue;
        if (has_with_scope(fd, scope)) {
            depth = 2;  /* will generate OP_get_ref_value */
        } else {
            depth = 0;
        }
        break;
    case OP_get_field:
        name = get_u32(fd->byte_code.buf + fd->last_opcode_pos + 1);
        depth = 1;
        break;
    case OP_scope_get_private_field:
        name = get_u32(fd->byte_code.buf + fd->last_opcode_pos + 1);
        scope = get_u16(fd->byte_code.buf + fd->last_opcode_pos + 5);
        depth = 1;
        break;
    case OP_get_array_el:
        depth = 2;
        break;
    case OP_get_super_value:
        depth = 3;
        break;
    default:
    invalid_lvalue:
        if (tok == TOK_FOR) {
            return js_parse_error(s, "invalid for in/of left hand-side");
        } else if (tok == TOK_INC || tok == TOK_DEC) {
            return js_parse_error(s, "invalid increment/decrement operand");
        } else if (tok == '[' || tok == '{') {
            return js_parse_error(s, "invalid destructuring target");
        } else {
            return js_parse_error(s, "invalid assignment left-hand side");
        }
    }
    /* remove the last opcode */
    fd->byte_code.size = fd->last_opcode_pos;
    fd->last_opcode_pos = -1;

    if (keep) {
        /* get the value but keep the object/fields on the stack */
        switch(opcode) {
        case OP_scope_get_var:
            if (depth != 0) {
                label = new_label(s);
                if (label < 0)
                    return -1;
                emit_op(s, OP_scope_make_ref);
                emit_atom(s, name);
                emit_u32(s, label);
                emit_u16(s, scope);
                update_label(fd, label, 1);
                emit_op(s, OP_get_ref_value);
                opcode = OP_get_ref_value;
            } else {
                emit_op(s, OP_scope_get_var);
                emit_atom(s, name);
                emit_u16(s, scope);
            }
            break;
        case OP_get_field:
            emit_op(s, OP_get_field2);
            emit_atom(s, name);
            break;
        case OP_scope_get_private_field:
            emit_op(s, OP_scope_get_private_field2);
            emit_atom(s, name);
            emit_u16(s, scope);
            break;
        case OP_get_array_el:
            /* XXX: replace by a single opcode ? */
            emit_op(s, OP_to_propkey2);
            emit_op(s, OP_dup2);
            emit_op(s, OP_get_array_el);
            break;
        case OP_get_super_value:
            emit_op(s, OP_to_propkey);
            emit_op(s, OP_dup3);
            emit_op(s, OP_get_super_value);
            break;
        default:
            abort();
        }
    } else {
        switch(opcode) {
        case OP_scope_get_var:
            if (depth != 0) {
                label = new_label(s);
                if (label < 0)
                    return -1;
                emit_op(s, OP_scope_make_ref);
                emit_atom(s, name);
                emit_u32(s, label);
                emit_u16(s, scope);
                update_label(fd, label, 1);
                opcode = OP_get_ref_value;
            }
            break;
        case OP_get_array_el:
            emit_op(s, OP_to_propkey2);
            break;
        case OP_get_super_value:
            emit_op(s, OP_to_propkey);
            break;
        }
    }

    *popcode = opcode;
    *pscope = scope;
    /* name has refcount for OP_get_field and OP_get_ref_value,
       and JS_ATOM_NULL for other opcodes */
    *pname = name;
    *plabel = label;
    if (pdepth)
        *pdepth = depth;
    return 0;
}

typedef enum {
    PUT_LVALUE_NOKEEP, /* [depth] v -> */
    PUT_LVALUE_NOKEEP_DEPTH, /* [depth] v -> , keep depth (currently
                                just disable optimizations) */
    PUT_LVALUE_KEEP_TOP,  /* [depth] v -> v */
    PUT_LVALUE_KEEP_SECOND, /* [depth] v0 v -> v0 */
    PUT_LVALUE_NOKEEP_BOTTOM, /* v [depth] -> */
} PutLValueEnum;

/* name has a live reference. 'is_let' is only used with opcode =
   OP_scope_get_var which is never generated by get_lvalue(). */
static void put_lvalue(JSParseState *s, int opcode, int scope,
                       JSAtom name, int label, PutLValueEnum special,
                       bool is_let)
{
    switch(opcode) {
    case OP_scope_get_var:
        /* depth = 0 */
        switch(special) {
        case PUT_LVALUE_NOKEEP:
        case PUT_LVALUE_NOKEEP_DEPTH:
        case PUT_LVALUE_KEEP_SECOND:
        case PUT_LVALUE_NOKEEP_BOTTOM:
            break;
        case PUT_LVALUE_KEEP_TOP:
            emit_op(s, OP_dup);
            break;
        default:
            abort();
        }
        break;
    case OP_get_field:
    case OP_scope_get_private_field:
        /* depth = 1 */
        switch(special) {
        case PUT_LVALUE_NOKEEP:
        case PUT_LVALUE_NOKEEP_DEPTH:
            break;
        case PUT_LVALUE_KEEP_TOP:
            emit_op(s, OP_insert2); /* obj v -> v obj v */
            break;
        case PUT_LVALUE_KEEP_SECOND:
            emit_op(s, OP_perm3); /* obj v0 v -> v0 obj v */
            break;
        case PUT_LVALUE_NOKEEP_BOTTOM:
            emit_op(s, OP_swap);
            break;
        default:
            abort();
        }
        break;
    case OP_get_array_el:
    case OP_get_ref_value:
        /* depth = 2 */
        if (opcode == OP_get_ref_value) {
            JS_FreeAtom(s->ctx, name);
            emit_label(s, label);
        }
        switch(special) {
        case PUT_LVALUE_NOKEEP:
            emit_op(s, OP_nop); /* will trigger optimization */
            break;
        case PUT_LVALUE_NOKEEP_DEPTH:
            break;
        case PUT_LVALUE_KEEP_TOP:
            emit_op(s, OP_insert3); /* obj prop v -> v obj prop v */
            break;
        case PUT_LVALUE_KEEP_SECOND:
            emit_op(s, OP_perm4); /* obj prop v0 v -> v0 obj prop v */
            break;
        case PUT_LVALUE_NOKEEP_BOTTOM:
            emit_op(s, OP_rot3l);
            break;
        default:
            abort();
        }
        break;
    case OP_get_super_value:
        /* depth = 3 */
        switch(special) {
        case PUT_LVALUE_NOKEEP:
        case PUT_LVALUE_NOKEEP_DEPTH:
            break;
        case PUT_LVALUE_KEEP_TOP:
            emit_op(s, OP_insert4); /* this obj prop v -> v this obj prop v */
            break;
        case PUT_LVALUE_KEEP_SECOND:
            emit_op(s, OP_perm5); /* this obj prop v0 v -> v0 this obj prop v */
            break;
        case PUT_LVALUE_NOKEEP_BOTTOM:
            emit_op(s, OP_rot4l);
            break;
        default:
            abort();
        }
        break;
    default:
        break;
    }

    switch(opcode) {
    case OP_scope_get_var:  /* val -- */
        emit_op(s, is_let ? OP_scope_put_var_init : OP_scope_put_var);
        emit_u32(s, name);  /* has refcount */
        emit_u16(s, scope);
        break;
    case OP_get_field:
        emit_op(s, OP_put_field);
        emit_u32(s, name);  /* name has refcount */
        break;
    case OP_scope_get_private_field:
        emit_op(s, OP_scope_put_private_field);
        emit_u32(s, name);  /* name has refcount */
        emit_u16(s, scope);
        break;
    case OP_get_array_el:
        emit_op(s, OP_put_array_el);
        break;
    case OP_get_ref_value:
        emit_op(s, OP_put_ref_value);
        break;
    case OP_get_super_value:
        emit_op(s, OP_put_super_value);
        break;
    default:
        abort();
    }
}

static __exception int js_parse_expr_paren(JSParseState *s)
{
    if (js_parse_expect(s, '('))
        return -1;
    if (js_parse_expr(s))
        return -1;
    if (js_parse_expect(s, ')'))
        return -1;
    return 0;
}

static int js_unsupported_keyword(JSParseState *s, JSAtom atom)
{
    char buf[ATOM_GET_STR_BUF_SIZE];
    return js_parse_error(s, "unsupported keyword: %s",
                          JS_AtomGetStr(s->ctx, buf, sizeof(buf), atom));
}

static __exception int js_define_var(JSParseState *s, JSAtom name, int tok)
{
    JSFunctionDef *fd = s->cur_func;
    JSVarDefEnum var_def_type;

    if (name == JS_ATOM_yield && fd->func_kind == JS_FUNC_GENERATOR) {
        return js_parse_error(s, "yield is a reserved identifier");
    }
    if ((name == JS_ATOM_arguments || name == JS_ATOM_eval)
    &&  fd->is_strict_mode) {
        return js_parse_error(s, "invalid variable name in strict mode");
    }
    if (tok == TOK_LET || tok == TOK_CONST) {
        if (name == JS_ATOM_let)
            return js_parse_error(s, "invalid lexical variable name 'let'");
        // |undefined| is allowed as an identifier except at the global
        // scope of a classic script; sloppy or strict doesn't matter
        if (name == JS_ATOM_undefined
        && fd->scope_level == 1
        && fd->is_global_var
        && !fd->module) {
            return js_parse_error(s, "'undefined' already declared");
        }
    }
    switch(tok) {
    case TOK_LET:
        var_def_type = JS_VAR_DEF_LET;
        break;
    case TOK_CONST:
        var_def_type = JS_VAR_DEF_CONST;
        break;
    case TOK_VAR:
        var_def_type = JS_VAR_DEF_VAR;
        break;
    case TOK_USING:
        var_def_type = JS_VAR_DEF_USING;
        break;
    case TOK_CATCH:
        var_def_type = JS_VAR_DEF_CATCH;
        break;
    default:
        abort();
    }
    if (define_var(s, fd, name, var_def_type) < 0)
        return -1;
    return 0;
}

static void js_emit_spread_code(JSParseState *s, int depth)
{
    int label_rest_next, label_rest_done;

    /* XXX: could check if enum object is an actual array and optimize
       slice extraction. enumeration record and target array are in a
       different order from OP_append case. */
    /* enum_rec xxx -- enum_rec xxx array 0 */
    emit_op(s, OP_array_from);
    emit_u16(s, 0);
    emit_op(s, OP_push_i32);
    emit_u32(s, 0);
    emit_label(s, label_rest_next = new_label(s));
    emit_op(s, OP_for_of_next);
    emit_u8(s, 2 + depth);
    label_rest_done = emit_goto(s, OP_if_true, -1);
    /* array idx val -- array idx */
    emit_op(s, OP_define_array_el);
    emit_op(s, OP_inc);
    emit_goto(s, OP_goto, label_rest_next);
    emit_label(s, label_rest_done);
    /* enum_rec xxx array idx undef -- enum_rec xxx array */
    emit_op(s, OP_drop);
    emit_op(s, OP_drop);
}

static int js_parse_check_duplicate_parameter(JSParseState *s, JSAtom name)
{
    /* Check for duplicate parameter names */
    JSFunctionDef *fd = s->cur_func;
    int i;
    for (i = 0; i < fd->arg_count; i++) {
        if (fd->args[i].var_name == name)
            goto duplicate;
    }
    for (i = 0; i < fd->var_count; i++) {
        if (fd->vars[i].var_name == name)
            goto duplicate;
    }
    return 0;

duplicate:
    return js_parse_error(s, "Duplicate parameter name not allowed in this context");
}

static JSAtom js_parse_destructuring_var(JSParseState *s, int tok, bool is_arg)
{
    JSAtom name;

    if (!(s->token.val == TOK_IDENT && !s->token.u.ident.is_reserved)
    ||  (s->cur_func->is_strict_mode &&
         (s->token.u.ident.atom == JS_ATOM_eval || s->token.u.ident.atom == JS_ATOM_arguments))) {
        js_parse_error(s, "invalid destructuring target");
        return JS_ATOM_NULL;
    }
    name = JS_DupAtom(s->ctx, s->token.u.ident.atom);
    if (is_arg && js_parse_check_duplicate_parameter(s, name))
        goto fail;
    if (next_token(s))
        goto fail;

    return name;
fail:
    JS_FreeAtom(s->ctx, name);
    return JS_ATOM_NULL;
}

/* Return -1 if error, 0 if no initializer, 1 if an initializer is
   present at the top level. */
static int js_parse_destructuring_element(JSParseState *s, int tok,
                                          bool is_arg, bool hasval,
                                          int has_ellipsis, // tri-state
                                          bool allow_initializer,
                                          bool export_flag)
{
    int label_parse, label_assign, label_done, label_lvalue, depth_lvalue;
    int start_addr, assign_addr;
    JSAtom prop_name, var_name;
    int opcode, scope, tok1, skip_bits;
    bool has_initializer;

    label_lvalue = -1;

    if (has_ellipsis < 0) {
        /* pre-parse destructuration target for spread detection */
        js_parse_skip_parens_token(s, &skip_bits, false);
        has_ellipsis = skip_bits & SKIP_HAS_ELLIPSIS;
    }

    label_parse = new_label(s);
    label_assign = new_label(s);

    start_addr = s->cur_func->byte_code.size;
    if (hasval) {
        /* consume value from the stack */
        emit_op(s, OP_dup);
        emit_op(s, OP_undefined);
        emit_op(s, OP_strict_eq);
        emit_goto(s, OP_if_true, label_parse);
        emit_label(s, label_assign);
    } else {
        emit_goto(s, OP_goto, label_parse);
        emit_label(s, label_assign);
        /* leave value on the stack */
        emit_op(s, OP_dup);
    }
    assign_addr = s->cur_func->byte_code.size;
    if (s->token.val == '{') {
        if (next_token(s))
            return -1;
        /* throw an exception if the value cannot be converted to an object */
        emit_op(s, OP_to_object);
        if (has_ellipsis) {
            /* add excludeList on stack just below src object */
            emit_op(s, OP_object);
            emit_op(s, OP_swap);
        }
        while (s->token.val != '}') {
            int prop_type;
            if (s->token.val == TOK_ELLIPSIS) {
                if (!has_ellipsis) {
                    JS_ThrowInternalError(s->ctx, "unexpected ellipsis token");
                    return -1;
                }
                if (next_token(s))
                    return -1;
                if (tok) {
                    var_name = js_parse_destructuring_var(s, tok, is_arg);
                    if (var_name == JS_ATOM_NULL)
                        return -1;
                    opcode = OP_scope_get_var;
                    scope = s->cur_func->scope_level;
                    label_lvalue = -1;
                    depth_lvalue = 0;
                } else {
                    if (js_parse_left_hand_side_expr(s))
                        return -1;

                    if (get_lvalue(s, &opcode, &scope, &var_name,
                                   &label_lvalue, &depth_lvalue, false, '{'))
                        return -1;
                }
                if (s->token.val != '}') {
                    js_parse_error(s, "assignment rest property must be last");
                    goto var_error;
                }
                emit_op(s, OP_object);  /* target */
                emit_op(s, OP_copy_data_properties);
                emit_u8(s, 0 | ((depth_lvalue + 1) << 2) | ((depth_lvalue + 2) << 5));
                goto set_val;
            }
            prop_type = js_parse_property_name(s, &prop_name, false, true, false);
            if (prop_type < 0)
                return -1;
            var_name = JS_ATOM_NULL;
            opcode = OP_scope_get_var;
            scope = s->cur_func->scope_level;
            label_lvalue = -1;
            depth_lvalue = 0;
            if (prop_type == PROP_TYPE_IDENT) {
                if (next_token(s))
                    goto prop_error;
                if ((s->token.val == '[' || s->token.val == '{')
                    &&  ((tok1 = js_parse_skip_parens_token(s, &skip_bits, false)) == ',' ||
                         tok1 == '=' || tok1 == '}')) {
                    if (prop_name == JS_ATOM_NULL) {
                        /* computed property name on stack */
                        if (has_ellipsis) {
                            /* define the property in excludeList */
                            emit_op(s, OP_to_propkey); /* avoid calling ToString twice */
                            emit_op(s, OP_perm3); /* TOS: src excludeList prop */
                            emit_op(s, OP_null); /* TOS: src excludeList prop null */
                            emit_op(s, OP_define_array_el); /* TOS: src excludeList prop */
                            emit_op(s, OP_perm3); /* TOS: excludeList src prop */
                        }
                        /* get the computed property from the source object */
                        emit_op(s, OP_get_array_el2);
                    } else {
                        /* named property */
                        if (has_ellipsis) {
                            /* define the property in excludeList */
                            emit_op(s, OP_swap); /* TOS: src excludeList */
                            emit_op(s, OP_null); /* TOS: src excludeList null */
                            emit_op(s, OP_define_field); /* TOS: src excludeList */
                            emit_atom(s, prop_name);
                            emit_op(s, OP_swap); /* TOS: excludeList src */
                        }
                        /* get the named property from the source object */
                        emit_op(s, OP_get_field2);
                        emit_u32(s, prop_name);
                    }
                    if (js_parse_destructuring_element(s, tok, is_arg, true, -1, true, export_flag) < 0)
                        return -1;
                    if (s->token.val == '}')
                        break;
                    /* accept a trailing comma before the '}' */
                    if (js_parse_expect(s, ','))
                        return -1;
                    continue;
                }
                if (prop_name == JS_ATOM_NULL) {
                    emit_op(s, OP_to_propkey2);
                    if (has_ellipsis) {
                        /* define the property in excludeList */
                        emit_op(s, OP_perm3);
                        emit_op(s, OP_null);
                        emit_op(s, OP_define_array_el);
                        emit_op(s, OP_perm3);
                    }
                    /* source prop -- source source prop */
                    emit_op(s, OP_dup1);
                } else {
                    if (has_ellipsis) {
                        /* define the property in excludeList */
                        emit_op(s, OP_swap);
                        emit_op(s, OP_null);
                        emit_op(s, OP_define_field);
                        emit_atom(s, prop_name);
                        emit_op(s, OP_swap);
                    }
                    /* source -- source source */
                    emit_op(s, OP_dup);
                }
                if (tok) {
                    var_name = js_parse_destructuring_var(s, tok, is_arg);
                    if (var_name == JS_ATOM_NULL)
                        goto prop_error;
                } else {
                    if (js_parse_left_hand_side_expr(s))
                        goto prop_error;
                lvalue:
                    if (get_lvalue(s, &opcode, &scope, &var_name,
                                   &label_lvalue, &depth_lvalue, false, '{'))
                        goto prop_error;
                    /* swap ref and lvalue object if any */
                    if (prop_name == JS_ATOM_NULL) {
                        switch(depth_lvalue) {
                        case 0:
                            break;
                        case 1:
                            /* source prop x -> x source prop */
                            emit_op(s, OP_rot3r);
                            break;
                        case 2:
                            /* source prop x y -> x y source prop */
                            emit_op(s, OP_swap2);   /* t p2 s p1 */
                            break;
                        case 3:
                            /* source prop x y z -> x y z source prop */
                            emit_op(s, OP_rot5l);
                            emit_op(s, OP_rot5l);
                            break;
                        default:
                            abort();
                        }
                    } else {
                        switch(depth_lvalue) {
                        case 0:
                            break;
                        case 1:
                            /* source x -> x source */
                            emit_op(s, OP_swap);
                            break;
                        case 2:
                            /* source x y -> x y source */
                            emit_op(s, OP_rot3l);
                            break;
                        case 3:
                            /* source x y z -> x y z source */
                            emit_op(s, OP_rot4l);
                            break;
                        default:
                            abort();
                        }
                    }
                }
                if (prop_name == JS_ATOM_NULL) {
                    /* computed property name on stack */
                    /* XXX: should have OP_get_array_el2x with depth */
                    /* source prop -- val */
                    emit_op(s, OP_get_array_el);
                } else {
                    /* named property */
                    /* XXX: should have OP_get_field2x with depth */
                    /* source -- val */
                    emit_op(s, OP_get_field);
                    emit_u32(s, prop_name);
                }
            } else {
                /* prop_type = PROP_TYPE_VAR, cannot be a computed property */
                if (is_arg && js_parse_check_duplicate_parameter(s, prop_name))
                    goto prop_error;
                if (s->cur_func->is_strict_mode &&
                    (prop_name == JS_ATOM_eval || prop_name == JS_ATOM_arguments)) {
                    js_parse_error(s, "invalid destructuring target");
                    goto prop_error;
                }
                if (has_ellipsis) {
                    /* define the property in excludeList */
                    emit_op(s, OP_swap);
                    emit_op(s, OP_null);
                    emit_op(s, OP_define_field);
                    emit_atom(s, prop_name);
                    emit_op(s, OP_swap);
                }
                if (!tok || tok == TOK_VAR) {
                    /* generate reference */
                    /* source -- source source */
                    emit_op(s, OP_dup);
                    emit_op(s, OP_scope_get_var);
                    emit_atom(s, prop_name);
                    emit_u16(s, s->cur_func->scope_level);
                    goto lvalue;
                }
                var_name = JS_DupAtom(s->ctx, prop_name);
                /* source -- source val */
                emit_op(s, OP_get_field2);
                emit_u32(s, prop_name);
            }
        set_val:
            if (tok) {
                if (js_define_var(s, var_name, tok))
                    goto var_error;
                if (export_flag) {
                    if (!add_export_entry(s, s->cur_func->module, var_name, var_name,
                                          JS_EXPORT_TYPE_LOCAL))
                        goto var_error;
                }
                scope = s->cur_func->scope_level;
            }
            if (s->token.val == '=') {  /* handle optional default value */
                int label_hasval;
                emit_op(s, OP_dup);
                emit_op(s, OP_undefined);
                emit_op(s, OP_strict_eq);
                label_hasval = emit_goto(s, OP_if_false, -1);
                if (next_token(s))
                    goto var_error;
                emit_op(s, OP_drop);
                if (js_parse_assign_expr(s))
                    goto var_error;
                if (opcode == OP_scope_get_var || opcode == OP_get_ref_value)
                    set_object_name(s, var_name);
                emit_label(s, label_hasval);
            }
            /* store value into lvalue object */
            put_lvalue(s, opcode, scope, var_name, label_lvalue,
                       PUT_LVALUE_NOKEEP_DEPTH,
                       (tok == TOK_CONST || tok == TOK_LET));
            if (s->token.val == '}')
                break;
            /* accept a trailing comma before the '}' */
            if (js_parse_expect(s, ','))
                return -1;
        }
        /* drop the source object */
        emit_op(s, OP_drop);
        if (has_ellipsis) {
            emit_op(s, OP_drop); /* pop excludeList */
        }
        if (next_token(s))
            return -1;
    } else if (s->token.val == '[') {
        bool has_spread;
        int enum_depth;
        int source_line_num, source_col_num;
        BlockEnv block_env;

        source_line_num = s->token.line_num;
        source_col_num = s->token.col_num;
        if (next_token(s))
            return -1;
        /* the block environment is only needed in generators in case
           'yield' triggers a 'return' */
        push_break_entry(s->cur_func, &block_env,
                         JS_ATOM_NULL, -1, -1, 2);
        block_env.has_iterator = true;
        emit_op(s, OP_for_of_start);
        has_spread = false;
        while (s->token.val != ']') {
            /* get the next value */
            if (s->token.val == TOK_ELLIPSIS) {
                if (next_token(s))
                    return -1;
                if (s->token.val == ',' || s->token.val == ']')
                    return js_parse_error(s, "missing binding pattern...");
                has_spread = true;
            }
            if (s->token.val == ',') {
                /* do nothing, skip the value, has_spread is false */
                emit_op(s, OP_for_of_next);
                emit_u8(s, 0);
                emit_op(s, OP_drop);
                emit_op(s, OP_drop);
            } else if ((s->token.val == '[' || s->token.val == '{')
                   &&  ((tok1 = js_parse_skip_parens_token(s, &skip_bits, false)) == ',' ||
                        tok1 == '=' || tok1 == ']')) {
                if (has_spread) {
                    if (tok1 == '=')
                        return js_parse_error(s, "rest element cannot have a default value");
                    js_emit_spread_code(s, 0);
                } else {
                    emit_op(s, OP_for_of_next);
                    emit_u8(s, 0);
                    emit_op(s, OP_drop);
                }
                if (js_parse_destructuring_element(s, tok, is_arg, true, skip_bits & SKIP_HAS_ELLIPSIS, true, export_flag) < 0)
                    return -1;
            } else {
                var_name = JS_ATOM_NULL;
                enum_depth = 0;
                if (tok) {
                    var_name = js_parse_destructuring_var(s, tok, is_arg);
                    if (var_name == JS_ATOM_NULL)
                        goto var_error;
                    if (js_define_var(s, var_name, tok))
                        goto var_error;
                    opcode = OP_scope_get_var;
                    scope = s->cur_func->scope_level;
                } else {
                    if (js_parse_left_hand_side_expr(s))
                        return -1;
                    if (get_lvalue(s, &opcode, &scope, &var_name,
                                   &label_lvalue, &enum_depth, false, '[')) {
                        return -1;
                    }
                }
                if (has_spread) {
                    js_emit_spread_code(s, enum_depth);
                } else {
                    emit_op(s, OP_for_of_next);
                    emit_u8(s, enum_depth);
                    emit_op(s, OP_drop);
                }
                if (s->token.val == '=' && !has_spread) {
                    /* handle optional default value */
                    int label_hasval;
                    emit_op(s, OP_dup);
                    emit_op(s, OP_undefined);
                    emit_op(s, OP_strict_eq);
                    label_hasval = emit_goto(s, OP_if_false, -1);
                    if (next_token(s))
                        goto var_error;
                    emit_op(s, OP_drop);
                    if (js_parse_assign_expr(s))
                        goto var_error;
                    if (opcode == OP_scope_get_var || opcode == OP_get_ref_value)
                        set_object_name(s, var_name);
                    emit_label(s, label_hasval);
                }
                /* store value into lvalue object */
                put_lvalue(s, opcode, scope, var_name,
                           label_lvalue, PUT_LVALUE_NOKEEP_DEPTH,
                           (tok == TOK_CONST || tok == TOK_LET));
            }
            if (s->token.val == ']')
                break;
            if (has_spread)
                return js_parse_error(s, "rest element must be the last one");
            /* accept a trailing comma before the ']' */
            if (js_parse_expect(s, ','))
                return -1;
        }
        /* close iterator object:
           if completed, enum_obj has been replaced by undefined */
        emit_source_loc_at(s, source_line_num, source_col_num);
        emit_op(s, OP_iterator_close);
        pop_break_entry(s->cur_func);
        if (next_token(s))
            return -1;
    } else {
        return js_parse_error(s, "invalid assignment syntax");
    }
    if (s->token.val == '=' && allow_initializer) {
        label_done = emit_goto(s, OP_goto, -1);
        if (next_token(s))
            return -1;
        emit_label(s, label_parse);
        if (hasval)
            emit_op(s, OP_drop);
        if (js_parse_assign_expr(s))
            return -1;
        emit_goto(s, OP_goto, label_assign);
        emit_label(s, label_done);
        has_initializer = true;
    } else {
        /* normally hasval is true except if
           js_parse_skip_parens_token() was wrong in the parsing */
        //        assert(hasval);
        if (!hasval) {
            js_parse_error(s, "too complicated destructuring expression");
            return -1;
        }
        /* remove test and decrement label ref count */
        memset(s->cur_func->byte_code.buf + start_addr, OP_nop,
               assign_addr - start_addr);
        s->cur_func->label_slots[label_parse].ref_count--;
        has_initializer = false;
    }
    return has_initializer;

 prop_error:
    JS_FreeAtom(s->ctx, prop_name);
 var_error:
    JS_FreeAtom(s->ctx, var_name);
    return -1;
}

typedef enum FuncCallType {
    FUNC_CALL_NORMAL,
    FUNC_CALL_NEW,
    FUNC_CALL_SUPER_CTOR,
    FUNC_CALL_TEMPLATE,
} FuncCallType;

static void optional_chain_test(JSParseState *s, int *poptional_chaining_label,
                                int drop_count)
{
    int label_next, i;
    if (*poptional_chaining_label < 0)
        *poptional_chaining_label = new_label(s);
   /* XXX: could be more efficient with a specific opcode */
    emit_op(s, OP_dup);
    emit_op(s, OP_is_undefined_or_null);
    label_next = emit_goto(s, OP_if_false, -1);
    for(i = 0; i < drop_count; i++)
        emit_op(s, OP_drop);
    emit_op(s, OP_undefined);
    emit_goto(s, OP_goto, *poptional_chaining_label);
    emit_label(s, label_next);
}

/* allowed parse_flags: PF_POSTFIX_CALL */
static __exception int js_parse_postfix_expr(JSParseState *s, int parse_flags)
{
    FuncCallType call_type;
    int optional_chaining_label;
    bool accept_lparen = (parse_flags & PF_POSTFIX_CALL) != 0;

    call_type = FUNC_CALL_NORMAL;
    switch(s->token.val) {
    case TOK_NUMBER:
        {
            JSValue val;
            val = s->token.u.num.val;

            if (JS_VALUE_GET_TAG(val) == JS_TAG_INT) {
                emit_op(s, OP_push_i32);
                emit_u32(s, JS_VALUE_GET_INT(val));
            } else if (JS_VALUE_GET_TAG(val) == JS_TAG_SHORT_BIG_INT) {
                int64_t v;
                v = JS_VALUE_GET_SHORT_BIG_INT(val);
                if (v >= INT32_MIN && v <= INT32_MAX) {
                    emit_op(s, OP_push_bigint_i32);
                    emit_u32(s, v);
                } else {
                    goto large_number;
                }
            } else {
            large_number:
                if (emit_push_const(s, val, 0) < 0)
                    return -1;
            }
        }
        if (next_token(s))
            return -1;
        break;
    case TOK_TEMPLATE:
        if (js_parse_template(s, 0, NULL))
            return -1;
        break;
    case TOK_STRING:
        if (emit_push_const(s, s->token.u.str.str, 1))
            return -1;
        if (next_token(s))
            return -1;
        break;

    case TOK_DIV_ASSIGN:
        s->buf_ptr -= 2;
        goto parse_regexp;
    case '/':
        s->buf_ptr--;
    parse_regexp:
        {
            JSValue str;
            int ret, backtrace_flags;
            if (!s->ctx->compile_regexp)
                return js_parse_error(s, "RegExp are not supported");
            /* the previous token is '/' or '/=', so no need to free */
            if (js_parse_regexp(s))
                return -1;
            ret = emit_push_const(s, s->token.u.regexp.body, 0);
            str = s->ctx->compile_regexp(s->ctx, s->token.u.regexp.body,
                                         s->token.u.regexp.flags);
            if (JS_IsException(str)) {
                /* add the line number info */
                backtrace_flags = 0;
                if (s->cur_func && s->cur_func->backtrace_barrier)
                    backtrace_flags = JS_BACKTRACE_FLAG_SINGLE_LEVEL;
                build_backtrace(s->ctx, s->ctx->rt->current_exception, JS_UNDEFINED,
                                s->filename,
                                s->token.line_num,
                                s->token.col_num,
                                backtrace_flags);
                return -1;
            }
            ret = emit_push_const(s, str, 0);
            JS_FreeValue(s->ctx, str);
            if (ret)
                return -1;
            /* we use a specific opcode to be sure the correct
               function is called (otherwise the bytecode would have
               to be verified by the RegExp constructor) */
            emit_op(s, OP_regexp);
            if (next_token(s))
                return -1;
        }
        break;
    case '(':
        if (js_parse_expr_paren(s))
            return -1;
        break;
    case TOK_FUNCTION:
        if (js_parse_function_decl(s, JS_PARSE_FUNC_EXPR,
                                   JS_FUNC_NORMAL, JS_ATOM_NULL,
                                   s->token.ptr,
                                   s->token.line_num,
                                   s->token.col_num))
            return -1;
        break;
    case TOK_CLASS:
        if (js_parse_class(s, true, JS_PARSE_EXPORT_NONE))
            return -1;
        break;
    case TOK_NULL:
        if (next_token(s))
            return -1;
        emit_op(s, OP_null);
        break;
    case TOK_THIS:
        if (next_token(s))
            return -1;
        emit_op(s, OP_scope_get_var);
        emit_atom(s, JS_ATOM_this);
        emit_u16(s, 0);
        break;
    case TOK_FALSE:
        if (next_token(s))
            return -1;
        emit_op(s, OP_push_false);
        break;
    case TOK_TRUE:
        if (next_token(s))
            return -1;
        emit_op(s, OP_push_true);
        break;
    case TOK_IDENT:
        {
            JSAtom name;
            if (s->token.u.ident.is_reserved) {
                return js_parse_error_reserved_identifier(s);
            }
            if (token_is_pseudo_keyword(s, JS_ATOM_async) &&
                peek_token(s, true) != '\n') {
                const uint8_t *source_ptr;
                int source_line_num;
                int source_col_num;

                source_ptr = s->token.ptr;
                source_line_num = s->token.line_num;
                source_col_num = s->token.col_num;
                if (next_token(s))
                    return -1;
                if (s->token.val == TOK_FUNCTION) {
                    if (js_parse_function_decl(s, JS_PARSE_FUNC_EXPR,
                                               JS_FUNC_ASYNC, JS_ATOM_NULL,
                                               source_ptr,
                                               source_line_num,
                                               source_col_num))
                        return -1;
                } else {
                    name = JS_DupAtom(s->ctx, JS_ATOM_async);
                    goto do_get_var;
                }
            } else {
                if (s->token.u.ident.atom == JS_ATOM_arguments &&
                    !s->cur_func->arguments_allowed) {
                    js_parse_error(s, "'arguments' identifier is not allowed in class field initializer");
                    return -1;
                }
                name = JS_DupAtom(s->ctx, s->token.u.ident.atom);
                if (next_token(s)) { /* update line number before emitting code */
                    JS_FreeAtom(s->ctx, name);
                    return -1;
                }
            do_get_var:
                emit_op(s, OP_scope_get_var);
                emit_u32(s, name);
                emit_u16(s, s->cur_func->scope_level);
            }
        }
        break;
    case '{':
    case '[':
        {
            int skip_bits;
            if (js_parse_skip_parens_token(s, &skip_bits, false) == '=') {
                if (js_parse_destructuring_element(s, 0, false, false, skip_bits & SKIP_HAS_ELLIPSIS, true, false) < 0)
                    return -1;
            } else {
                if (s->token.val == '{') {
                    if (js_parse_object_literal(s))
                        return -1;
                } else {
                    if (js_parse_array_literal(s))
                        return -1;
                }
            }
        }
        break;
    case TOK_NEW:
        if (next_token(s))
            return -1;
        if (s->token.val == '.') {
            if (next_token(s))
                return -1;
            if (!token_is_pseudo_keyword(s, JS_ATOM_target))
                return js_parse_error(s, "expecting target");
            if (!s->cur_func->new_target_allowed)
                return js_parse_error(s, "new.target only allowed within functions");
            if (next_token(s))
                return -1;
            emit_op(s, OP_scope_get_var);
            emit_atom(s, JS_ATOM_new_target);
            emit_u16(s, 0);
        } else {
            emit_source_loc(s);
            if (js_parse_postfix_expr(s, 0))
                return -1;
            accept_lparen = true;
            if (s->token.val != '(') {
                /* new operator on an object */
                emit_op(s, OP_dup);
                emit_op(s, OP_call_constructor);
                emit_u16(s, 0);
            } else {
                call_type = FUNC_CALL_NEW;
            }
        }
        break;
    case TOK_SUPER:
        if (next_token(s))
            return -1;
        if (s->token.val == '(') {
            if (!s->cur_func->super_call_allowed)
                return js_parse_error(s, "super() is only valid in a derived class constructor");
            call_type = FUNC_CALL_SUPER_CTOR;
        } else if (s->token.val == '.' || s->token.val == '[') {
            if (!s->cur_func->super_allowed)
                return js_parse_error(s, "'super' is only valid in a method");
            emit_op(s, OP_scope_get_var);
            emit_atom(s, JS_ATOM_this);
            emit_u16(s, 0);
            emit_op(s, OP_scope_get_var);
            emit_atom(s, JS_ATOM_home_object);
            emit_u16(s, 0);
            emit_op(s, OP_get_super);
        } else {
            return js_parse_error(s, "invalid use of 'super'");
        }
        break;
    case TOK_IMPORT:
        if (next_token(s))
            return -1;
        if (s->token.val == '.') {
            if (next_token(s))
                return -1;
            if (!token_is_pseudo_keyword(s, JS_ATOM_meta))
                return js_parse_error(s, "meta expected");
            if (!s->is_module)
                return js_parse_error(s, "import.meta only valid in module code");
            if (next_token(s))
                return -1;
            emit_op(s, OP_special_object);
            emit_u8(s, OP_SPECIAL_OBJECT_IMPORT_META);
        } else {
            if (js_parse_expect(s, '('))
                return -1;
            if (!accept_lparen)
                return js_parse_error(s, "invalid use of 'import()'");
            if (js_parse_assign_expr(s))
                return -1;
            if (s->token.val == ',') {
                if (next_token(s))
                    return -1;
                if (s->token.val != ')') {
                    if (js_parse_assign_expr(s))
                        return -1;
                    /* accept a trailing comma */
                    if (s->token.val == ',') {
                        if (next_token(s))
                            return -1;
                    }
                } else {
                    emit_op(s, OP_undefined);
                }
            } else {
                emit_op(s, OP_undefined);
            }
            if (js_parse_expect(s, ')'))
                return -1;
            emit_op(s, OP_import);
        }
        break;
    default:
        return js_parse_error(s, "unexpected token in expression: '%.*s'",
                              (int)(s->buf_ptr - s->token.ptr), s->token.ptr);
    }

    optional_chaining_label = -1;
    for(;;) {
        JSFunctionDef *fd = s->cur_func;
        bool has_optional_chain = false;

        if (s->token.val == TOK_QUESTION_MARK_DOT) {
            /* optional chaining */
            if (next_token(s))
                return -1;
            has_optional_chain = true;
            if (s->token.val == '(' && accept_lparen) {
                goto parse_func_call;
            } else if (s->token.val == '[') {
                goto parse_array_access;
            } else {
                goto parse_property;
            }
        } else if (s->token.val == TOK_TEMPLATE &&
                   call_type == FUNC_CALL_NORMAL) {
            if (optional_chaining_label >= 0) {
                return js_parse_error(s, "template literal cannot appear in an optional chain");
            }
            call_type = FUNC_CALL_TEMPLATE;
            goto parse_func_call2;
        } else if (s->token.val == '(' && accept_lparen) {
            int opcode, arg_count, drop_count;

            /* function call */
        parse_func_call:
            if (next_token(s))
                return -1;

            if (call_type == FUNC_CALL_NORMAL) {
            parse_func_call2:
                emit_source_loc(s);
                switch(opcode = get_prev_opcode(fd)) {
                case OP_get_field:
                    /* keep the object on the stack */
                    fd->byte_code.buf[fd->last_opcode_pos] = OP_get_field2;
                    drop_count = 2;
                    break;
                case OP_get_field_opt_chain:
                    {
                        int opt_chain_label, next_label;
                        opt_chain_label = get_u32(fd->byte_code.buf +
                                                  fd->last_opcode_pos + 1 + 4 + 1);
                        /* keep the object on the stack */
                        fd->byte_code.buf[fd->last_opcode_pos] = OP_get_field2;
                        fd->byte_code.size = fd->last_opcode_pos + 1 + 4;
                        next_label = emit_goto(s, OP_goto, -1);
                        emit_label(s, opt_chain_label);
                        /* need an additional undefined value for the
                           case where the optional field does not
                           exists */
                        emit_op(s, OP_undefined);
                        emit_label(s, next_label);
                        drop_count = 2;
                        opcode = OP_get_field;
                    }
                    break;
                case OP_scope_get_private_field:
                    /* keep the object on the stack */
                    fd->byte_code.buf[fd->last_opcode_pos] = OP_scope_get_private_field2;
                    drop_count = 2;
                    break;
                case OP_get_array_el:
                    /* keep the object on the stack */
                    fd->byte_code.buf[fd->last_opcode_pos] = OP_get_array_el2;
                    drop_count = 2;
                    break;
                case OP_get_array_el_opt_chain:
                    {
                        int opt_chain_label, next_label;
                        opt_chain_label = get_u32(fd->byte_code.buf +
                                                  fd->last_opcode_pos + 1 + 1);
                        /* keep the object on the stack */
                        fd->byte_code.buf[fd->last_opcode_pos] = OP_get_array_el2;
                        fd->byte_code.size = fd->last_opcode_pos + 1;
                        next_label = emit_goto(s, OP_goto, -1);
                        emit_label(s, opt_chain_label);
                        /* need an additional undefined value for the
                           case where the optional field does not
                           exists */
                        emit_op(s, OP_undefined);
                        emit_label(s, next_label);
                        drop_count = 2;
                        opcode = OP_get_array_el;
                    }
                    break;
                case OP_scope_get_var:
                    {
                        JSAtom name;
                        int scope;
                        name = get_u32(fd->byte_code.buf + fd->last_opcode_pos + 1);
                        scope = get_u16(fd->byte_code.buf + fd->last_opcode_pos + 5);
                        if (name == JS_ATOM_eval && call_type == FUNC_CALL_NORMAL && !has_optional_chain) {
                            /* direct 'eval' */
                            opcode = OP_eval;
                        } else {
                            /* verify if function name resolves to a simple
                               get_loc/get_arg: a function call inside a `with`
                               statement can resolve to a method call of the
                               `with` context object
                             */
                            /* XXX: always generate the OP_scope_get_ref
                               and remove it in variable resolution
                               pass ? */
                            if (has_with_scope(fd, scope)) {
                                opcode = OP_scope_get_ref;
                                fd->byte_code.buf[fd->last_opcode_pos] = opcode;
                            }
                        }
                        drop_count = 1;
                    }
                    break;
                case OP_get_super_value:
                    fd->byte_code.buf[fd->last_opcode_pos] = OP_get_array_el;
                    /* on stack: this func_obj */
                    opcode = OP_get_array_el;
                    drop_count = 2;
                    break;
                default:
                    opcode = OP_invalid;
                    drop_count = 1;
                    break;
                }
                if (has_optional_chain) {
                    optional_chain_test(s, &optional_chaining_label,
                                        drop_count);
                }
            } else {
                opcode = OP_invalid;
            }

            if (call_type == FUNC_CALL_TEMPLATE) {
                if (js_parse_template(s, 1, &arg_count))
                    return -1;
                goto emit_func_call;
            } else if (call_type == FUNC_CALL_SUPER_CTOR) {
                emit_op(s, OP_scope_get_var);
                emit_atom(s, JS_ATOM_this_active_func);
                emit_u16(s, 0);

                emit_op(s, OP_get_super);

                emit_op(s, OP_scope_get_var);
                emit_atom(s, JS_ATOM_new_target);
                emit_u16(s, 0);
            } else if (call_type == FUNC_CALL_NEW) {
                emit_op(s, OP_dup); /* new.target = function */
            }

            /* parse arguments */
            arg_count = 0;
            while (s->token.val != ')') {
                if (arg_count >= 65535) {
                    return js_parse_error(s, "Too many arguments in function call (only %d allowed)",
                                          65535 - 1);
                }
                if (s->token.val == TOK_ELLIPSIS)
                    break;
                if (js_parse_assign_expr(s))
                    return -1;
                arg_count++;
                if (s->token.val == ')')
                    break;
                /* accept a trailing comma before the ')' */
                if (js_parse_expect(s, ','))
                    return -1;
            }
            if (s->token.val == TOK_ELLIPSIS) {
                emit_op(s, OP_array_from);
                emit_u16(s, arg_count);
                emit_op(s, OP_push_i32);
                emit_u32(s, arg_count);

                /* on stack: array idx */
                while (s->token.val != ')') {
                    if (s->token.val == TOK_ELLIPSIS) {
                        if (next_token(s))
                            return -1;
                        if (js_parse_assign_expr(s))
                            return -1;
                        /* XXX: could pass is_last indicator? */
                        emit_op(s, OP_append);
                    } else {
                        if (js_parse_assign_expr(s))
                            return -1;
                        /* array idx val */
                        emit_op(s, OP_define_array_el);
                        emit_op(s, OP_inc);
                    }
                    if (s->token.val == ')')
                        break;
                    /* accept a trailing comma before the ')' */
                    if (js_parse_expect(s, ','))
                        return -1;
                }
                if (next_token(s))
                    return -1;
                /* drop the index */
                emit_op(s, OP_drop);

                /* apply function call */
                switch(opcode) {
                case OP_get_field:
                case OP_scope_get_private_field:
                case OP_get_array_el:
                case OP_scope_get_ref:
                    /* obj func array -> func obj array */
                    emit_op(s, OP_perm3);
                    emit_op(s, OP_apply);
                    emit_u16(s, call_type == FUNC_CALL_NEW);
                    break;
                case OP_eval:
                    emit_op(s, OP_apply_eval);
                    emit_u16(s, fd->scope_level);
                    fd->has_eval_call = true;
                    break;
                default:
                    if (call_type == FUNC_CALL_SUPER_CTOR) {
                        emit_op(s, OP_apply);
                        emit_u16(s, 1);
                        /* set the 'this' value */
                        emit_op(s, OP_dup);
                        emit_op(s, OP_scope_put_var_init);
                        emit_atom(s, JS_ATOM_this);
                        emit_u16(s, 0);

                        emit_class_field_init(s);
                    } else if (call_type == FUNC_CALL_NEW) {
                        /* obj func array -> func obj array */
                        emit_op(s, OP_perm3);
                        emit_op(s, OP_apply);
                        emit_u16(s, 1);
                    } else {
                        /* func array -> func undef array */
                        emit_op(s, OP_undefined);
                        emit_op(s, OP_swap);
                        emit_op(s, OP_apply);
                        emit_u16(s, 0);
                    }
                    break;
                }
            } else {
                if (next_token(s))
                    return -1;
            emit_func_call:
                switch(opcode) {
                case OP_get_field:
                case OP_scope_get_private_field:
                case OP_get_array_el:
                case OP_scope_get_ref:
                    emit_op(s, OP_call_method);
                    emit_u16(s, arg_count);
                    break;
                case OP_eval:
                    emit_op(s, OP_eval);
                    emit_u16(s, arg_count);
                    emit_u16(s, fd->scope_level);
                    fd->has_eval_call = true;
                    break;
                default:
                    if (call_type == FUNC_CALL_SUPER_CTOR) {
                        emit_op(s, OP_call_constructor);
                        emit_u16(s, arg_count);

                        /* set the 'this' value */
                        emit_op(s, OP_dup);
                        emit_op(s, OP_scope_put_var_init);
                        emit_atom(s, JS_ATOM_this);
                        emit_u16(s, 0);

                        emit_class_field_init(s);
                    } else if (call_type == FUNC_CALL_NEW) {
                        emit_op(s, OP_call_constructor);
                        emit_u16(s, arg_count);
                    } else {
                        emit_op(s, OP_call);
                        emit_u16(s, arg_count);
                    }
                    break;
                }
            }
            call_type = FUNC_CALL_NORMAL;
        } else if (s->token.val == '.') {
            if (next_token(s))
                return -1;
        parse_property:
            if (s->token.val == TOK_PRIVATE_NAME) {
                /* private class field */
                if (get_prev_opcode(fd) == OP_get_super) {
                    return js_parse_error(s, "private class field forbidden after super");
                }
                if (has_optional_chain) {
                    optional_chain_test(s, &optional_chaining_label, 1);
                }
                emit_op(s, OP_scope_get_private_field);
                emit_atom(s, s->token.u.ident.atom);
                emit_u16(s, s->cur_func->scope_level);
            } else {
                if (!token_is_ident(s->token.val)) {
                    return js_parse_error(s, "expecting field name");
                }
                if (get_prev_opcode(fd) == OP_get_super) {
                    JSValue val;
                    int ret;
                    val = JS_AtomToValue(s->ctx, s->token.u.ident.atom);
                    ret = emit_push_const(s, val, 1);
                    JS_FreeValue(s->ctx, val);
                    if (ret)
                        return -1;
                    emit_op(s, OP_get_super_value);
                } else {
                    if (has_optional_chain) {
                        optional_chain_test(s, &optional_chaining_label, 1);
                    }
                    emit_op(s, OP_get_field);
                    emit_atom(s, s->token.u.ident.atom);
                }
            }
            if (next_token(s))
                return -1;
        } else if (s->token.val == '[') {
            int prev_op;

        parse_array_access:
            prev_op = get_prev_opcode(fd);
            if (has_optional_chain) {
                optional_chain_test(s, &optional_chaining_label, 1);
            }
            if (next_token(s))
                return -1;
            if (js_parse_expr(s))
                return -1;
            if (js_parse_expect(s, ']'))
                return -1;
            if (prev_op == OP_get_super) {
                emit_op(s, OP_get_super_value);
            } else {
                emit_op(s, OP_get_array_el);
            }
        } else {
            break;
        }
    }
    if (optional_chaining_label >= 0) {
        JSFunctionDef *fd = s->cur_func;
        int opcode;
        emit_label_raw(s, optional_chaining_label);
        /* modify the last opcode so that it is an indicator of an
           optional chain */
        opcode = get_prev_opcode(fd);
        if (opcode == OP_get_field || opcode == OP_get_array_el) {
            if (opcode == OP_get_field)
                opcode = OP_get_field_opt_chain;
            else
                opcode = OP_get_array_el_opt_chain;
            fd->byte_code.buf[fd->last_opcode_pos] = opcode;
        } else {
            fd->last_opcode_pos = -1;
        }
    }
    return 0;
}

static __exception int js_parse_delete(JSParseState *s)
{
    JSFunctionDef *fd = s->cur_func;
    JSAtom name;
    int opcode;

    if (next_token(s))
        return -1;
    if (js_parse_unary(s, PF_POW_FORBIDDEN))
        return -1;
    switch(opcode = get_prev_opcode(fd)) {
    case OP_get_field:
    case OP_get_field_opt_chain:
        {
            JSValue val;
            int ret, opt_chain_label, next_label;
            if (opcode == OP_get_field_opt_chain) {
                opt_chain_label = get_u32(fd->byte_code.buf +
                                          fd->last_opcode_pos + 1 + 4 + 1);
            } else {
                opt_chain_label = -1;
            }
            name = get_u32(fd->byte_code.buf + fd->last_opcode_pos + 1);
            fd->byte_code.size = fd->last_opcode_pos;
            val = JS_AtomToValue(s->ctx, name);
            ret = emit_push_const(s, val, 1);
            JS_FreeValue(s->ctx, val);
            JS_FreeAtom(s->ctx, name);
            if (ret)
                return ret;
            emit_op(s, OP_delete);
            if (opt_chain_label >= 0) {
                next_label = emit_goto(s, OP_goto, -1);
                emit_label(s, opt_chain_label);
                /* if the optional chain is not taken, return 'true' */
                emit_op(s, OP_drop);
                emit_op(s, OP_push_true);
                emit_label(s, next_label);
            }
            fd->last_opcode_pos = -1;
        }
        break;
    case OP_get_array_el:
        fd->byte_code.size = fd->last_opcode_pos;
        fd->last_opcode_pos = -1;
        emit_op(s, OP_delete);
        break;
    case OP_get_array_el_opt_chain:
        {
            int opt_chain_label, next_label;
            opt_chain_label = get_u32(fd->byte_code.buf +
                                      fd->last_opcode_pos + 1 + 1);
            fd->byte_code.size = fd->last_opcode_pos;
            emit_op(s, OP_delete);
            next_label = emit_goto(s, OP_goto, -1);
            emit_label(s, opt_chain_label);
            /* if the optional chain is not taken, return 'true' */
            emit_op(s, OP_drop);
            emit_op(s, OP_push_true);
            emit_label(s, next_label);
            fd->last_opcode_pos = -1;
        }
        break;
    case OP_scope_get_var:
        /* 'delete this': this is not a reference */
        name = get_u32(fd->byte_code.buf + fd->last_opcode_pos + 1);
        if (name == JS_ATOM_this || name == JS_ATOM_new_target)
            goto ret_true;
        if (fd->is_strict_mode) {
            return js_parse_error(s, "cannot delete a direct reference in strict mode");
        } else {
            fd->byte_code.buf[fd->last_opcode_pos] = OP_scope_delete_var;
        }
        break;
    case OP_scope_get_private_field:
        return js_parse_error(s, "cannot delete a private class field");
    case OP_get_super_value:
        fd->byte_code.size = fd->last_opcode_pos;
        fd->last_opcode_pos = -1;
        emit_op(s, OP_throw_error);
        emit_atom(s, JS_ATOM_NULL);
        emit_u8(s, JS_THROW_ERROR_DELETE_SUPER);
        break;
    default:
    ret_true:
        emit_op(s, OP_drop);
        emit_op(s, OP_push_true);
        break;
    }
    return 0;
}

static __exception int js_parse_var(JSParseState *s, int parse_flags, int tok,
                                    bool export_flag);

/* allowed parse_flags: PF_POW_ALLOWED, PF_POW_FORBIDDEN */
static __exception int js_parse_unary(JSParseState *s, int parse_flags)
{
    int op;

    switch(s->token.val) {
    case '+':
    case '-':
    case '!':
    case '~':
    case TOK_VOID:
        op = s->token.val;
        if (next_token(s))
            return -1;
        if (js_parse_unary(s, PF_POW_FORBIDDEN))
            return -1;
        switch(op) {
        case '-':
            emit_op(s, OP_neg);
            break;
        case '+':
            emit_op(s, OP_plus);
            break;
        case '!':
            emit_op(s, OP_lnot);
            break;
        case '~':
            emit_op(s, OP_not);
            break;
        case TOK_VOID:
            emit_op(s, OP_drop);
            emit_op(s, OP_undefined);
            break;
        default:
            abort();
        }
        parse_flags = 0;
        break;
    case TOK_DEC:
    case TOK_INC:
        {
            int opcode, op, scope, label;
            JSAtom name;
            op = s->token.val;
            if (next_token(s))
                return -1;
            if (js_parse_unary(s, 0))
                return -1;
            if (get_lvalue(s, &opcode, &scope, &name, &label, NULL, true, op))
                return -1;
            emit_op(s, OP_dec + op - TOK_DEC);
            put_lvalue(s, opcode, scope, name, label, PUT_LVALUE_KEEP_TOP,
                       false);
        }
        break;
    case TOK_TYPEOF:
        {
            JSFunctionDef *fd;
            if (next_token(s))
                return -1;
            if (js_parse_unary(s, PF_POW_FORBIDDEN))
                return -1;
            /* reference access should not return an exception, so we
               patch the get_var */
            fd = s->cur_func;
            if (get_prev_opcode(fd) == OP_scope_get_var) {
                fd->byte_code.buf[fd->last_opcode_pos] = OP_scope_get_var_undef;
            }
            emit_op(s, OP_typeof);
            parse_flags = 0;
        }
        break;
    case TOK_DELETE:
        if (js_parse_delete(s))
            return -1;
        parse_flags = 0;
        break;
    case TOK_AWAIT:
        if (!(s->cur_func->func_kind & JS_FUNC_ASYNC))
            return js_parse_error(s, "unexpected 'await' keyword");
        if (!s->cur_func->in_function_body)
            return js_parse_error(s, "await in default expression");
        if (next_token(s))
            return -1;
        if (js_parse_unary(s, PF_POW_FORBIDDEN))
            return -1;
        emit_op(s, OP_await);
        s->cur_func->has_await = true;
        parse_flags = 0;
        break;
    default:
        if (js_parse_postfix_expr(s, PF_POSTFIX_CALL))
            return -1;
        if (!s->got_lf &&
            (s->token.val == TOK_DEC || s->token.val == TOK_INC)) {
            int opcode, op, scope, label;
            JSAtom name;
            op = s->token.val;
            if (get_lvalue(s, &opcode, &scope, &name, &label, NULL, true, op))
                return -1;
            emit_op(s, OP_post_dec + op - TOK_DEC);
            put_lvalue(s, opcode, scope, name, label, PUT_LVALUE_KEEP_SECOND,
                       false);
            if (next_token(s))
                return -1;
        }
        break;
    }
    if (parse_flags & (PF_POW_ALLOWED | PF_POW_FORBIDDEN)) {
        if (s->token.val == TOK_POW) {
            /* Strict ES7 exponentiation syntax rules: To solve
               conficting semantics between different implementations
               regarding the precedence of prefix operators and the
               postifx exponential, ES7 specifies that -2**2 is a
               syntax error. */
            if (parse_flags & PF_POW_FORBIDDEN)
                return js_parse_error(s, "unparenthesized unary expression can't appear on the left-hand side of '**'");
            if (next_token(s))
                return -1;
            if (js_parse_unary(s, PF_POW_ALLOWED))
                return -1;
            emit_op(s, OP_pow);
        }
    }
    return 0;
}

/* allowed parse_flags: PF_IN_ACCEPTED */
static __exception int js_parse_expr_binary(JSParseState *s, int level,
                                            int parse_flags)
{
    int op, opcode;

    if (level == 0) {
        return js_parse_unary(s, PF_POW_ALLOWED);
    } else if (s->token.val == TOK_PRIVATE_NAME &&
               (parse_flags & PF_IN_ACCEPTED) && level == 4 &&
               peek_token(s, false) == TOK_IN) {
        JSAtom atom;
        atom = JS_DupAtom(s->ctx, s->token.u.ident.atom);
        if (next_token(s))
            goto fail_private_in;
        if (s->token.val != TOK_IN)
            goto fail_private_in;
        if (next_token(s))
            goto fail_private_in;
        if (js_parse_expr_binary(s, level - 1, parse_flags)) {
        fail_private_in:
            JS_FreeAtom(s->ctx, atom);
            return -1;
        }
        emit_op(s, OP_scope_in_private_field);
        emit_atom(s, atom);
        emit_u16(s, s->cur_func->scope_level);
        JS_FreeAtom(s->ctx, atom);
        return 0;
    } else {
        if (js_parse_expr_binary(s, level - 1, parse_flags))
            return -1;
    }
    for(;;) {
        op = s->token.val;
        switch(level) {
        case 1:
            switch(op) {
            case '*':
                opcode = OP_mul;
                break;
            case '/':
                opcode = OP_div;
                break;
            case '%':
                opcode = OP_mod;
                break;
            default:
                return 0;
            }
            break;
        case 2:
            switch(op) {
            case '+':
                opcode = OP_add;
                break;
            case '-':
                opcode = OP_sub;
                break;
            default:
                return 0;
            }
            break;
        case 3:
            switch(op) {
            case TOK_SHL:
                opcode = OP_shl;
                break;
            case TOK_SAR:
                opcode = OP_sar;
                break;
            case TOK_SHR:
                opcode = OP_shr;
                break;
            default:
                return 0;
            }
            break;
        case 4:
            switch(op) {
            case '<':
                opcode = OP_lt;
                break;
            case '>':
                opcode = OP_gt;
                break;
            case TOK_LTE:
                opcode = OP_lte;
                break;
            case TOK_GTE:
                opcode = OP_gte;
                break;
            case TOK_INSTANCEOF:
                opcode = OP_instanceof;
                break;
            case TOK_IN:
                if (parse_flags & PF_IN_ACCEPTED) {
                    opcode = OP_in;
                } else {
                    return 0;
                }
                break;
            default:
                return 0;
            }
            break;
        case 5:
            switch(op) {
            case TOK_EQ:
                opcode = OP_eq;
                break;
            case TOK_NEQ:
                opcode = OP_neq;
                break;
            case TOK_STRICT_EQ:
                opcode = OP_strict_eq;
                break;
            case TOK_STRICT_NEQ:
                opcode = OP_strict_neq;
                break;
            default:
                return 0;
            }
            break;
        case 6:
            switch(op) {
            case '&':
                opcode = OP_and;
                break;
            default:
                return 0;
            }
            break;
        case 7:
            switch(op) {
            case '^':
                opcode = OP_xor;
                break;
            default:
                return 0;
            }
            break;
        case 8:
            switch(op) {
            case '|':
                opcode = OP_or;
                break;
            default:
                return 0;
            }
            break;
        default:
            abort();
        }
        if (next_token(s))
            return -1;
        emit_source_loc(s);
        if (js_parse_expr_binary(s, level - 1, parse_flags))
            return -1;
        emit_op(s, opcode);
    }
    return 0;
}

/* allowed parse_flags: PF_IN_ACCEPTED */
static __exception int js_parse_logical_and_or(JSParseState *s, int op,
                                               int parse_flags)
{
    int label1;

    if (op == TOK_LAND) {
        if (js_parse_expr_binary(s, 8, parse_flags))
            return -1;
    } else {
        if (js_parse_logical_and_or(s, TOK_LAND, parse_flags))
            return -1;
    }
    if (s->token.val == op) {
        label1 = new_label(s);

        for(;;) {
            if (next_token(s))
                return -1;
            emit_op(s, OP_dup);
            emit_goto(s, op == TOK_LAND ? OP_if_false : OP_if_true, label1);
            emit_op(s, OP_drop);

            if (op == TOK_LAND) {
                if (js_parse_expr_binary(s, 8, parse_flags))
                    return -1;
            } else {
                if (js_parse_logical_and_or(s, TOK_LAND, parse_flags))
                    return -1;
            }
            if (s->token.val != op) {
                if (s->token.val == TOK_DOUBLE_QUESTION_MARK)
                    return js_parse_error(s, "cannot mix ?? with && or ||");
                break;
            }
        }

        emit_label(s, label1);
    }
    return 0;
}

static __exception int js_parse_coalesce_expr(JSParseState *s, int parse_flags)
{
    int label1;

    if (js_parse_logical_and_or(s, TOK_LOR, parse_flags))
        return -1;
    if (s->token.val == TOK_DOUBLE_QUESTION_MARK) {
        label1 = new_label(s);
        for(;;) {
            if (next_token(s))
                return -1;

            emit_op(s, OP_dup);
            emit_op(s, OP_is_undefined_or_null);
            emit_goto(s, OP_if_false, label1);
            emit_op(s, OP_drop);

            if (js_parse_expr_binary(s, 8, parse_flags))
                return -1;
            if (s->token.val != TOK_DOUBLE_QUESTION_MARK)
                break;
        }
        emit_label(s, label1);
    }
    return 0;
}

/* allowed parse_flags: PF_IN_ACCEPTED */
static __exception int js_parse_cond_expr(JSParseState *s, int parse_flags)
{
    int label1, label2;

    if (js_parse_coalesce_expr(s, parse_flags))
        return -1;
    if (s->token.val == '?') {
        if (next_token(s))
            return -1;
        label1 = emit_goto(s, OP_if_false, -1);

        if (js_parse_assign_expr(s))
            return -1;
        if (js_parse_expect(s, ':'))
            return -1;

        label2 = emit_goto(s, OP_goto, -1);

        emit_label(s, label1);

        if (js_parse_assign_expr2(s, parse_flags & PF_IN_ACCEPTED))
            return -1;

        emit_label(s, label2);
    }
    return 0;
}

/* allowed parse_flags: PF_IN_ACCEPTED */
static __exception int js_parse_assign_expr2(JSParseState *s, int parse_flags)
{
    int opcode, op, scope;
    JSAtom name0 = JS_ATOM_NULL;
    JSAtom name;

    if (s->token.val == TOK_YIELD) {
        bool is_star = false, is_async;

        if (!(s->cur_func->func_kind & JS_FUNC_GENERATOR))
            return js_parse_error(s, "unexpected 'yield' keyword");
        if (!s->cur_func->in_function_body)
            return js_parse_error(s, "yield in default expression");
        if (next_token(s))
            return -1;
        /* XXX: is there a better method to detect 'yield' without
           parameters ? */
        if (s->token.val != ';' && s->token.val != ')' &&
            s->token.val != ']' && s->token.val != '}' &&
            s->token.val != ',' && s->token.val != ':' && !s->got_lf) {
            if (s->token.val == '*') {
                is_star = true;
                if (next_token(s))
                    return -1;
            }
            if (js_parse_assign_expr2(s, parse_flags))
                return -1;
        } else {
            emit_op(s, OP_undefined);
        }
        is_async = (s->cur_func->func_kind == JS_FUNC_ASYNC_GENERATOR);

        if (is_star) {
            int label_loop, label_return, label_next;
            int label_return1, label_yield, label_throw, label_throw1;
            int label_throw2;

            label_loop = new_label(s);
            label_yield = new_label(s);

            emit_op(s, is_async ? OP_for_await_of_start : OP_for_of_start);

            /* remove the catch offset (XXX: could avoid pushing back
               undefined) */
            emit_op(s, OP_drop);
            emit_op(s, OP_undefined);

            emit_op(s, OP_undefined); /* initial value */

            emit_label(s, label_loop);
            emit_op(s, OP_iterator_next);
            if (is_async)
                emit_op(s, OP_await);
            emit_op(s, OP_check_object);
            emit_op(s, OP_get_field2);
            emit_atom(s, JS_ATOM_done);
            label_next = emit_goto(s, OP_if_true, -1); /* end of loop */
            emit_label(s, label_yield);
            if (is_async) {
                /* OP_async_yield_star takes the value as parameter */
                emit_op(s, OP_get_field);
                emit_atom(s, JS_ATOM_value);
                emit_op(s, OP_async_yield_star);
            } else {
                /* OP_yield_star takes (value, done) as parameter */
                emit_op(s, OP_yield_star);
            }
            emit_op(s, OP_dup);
            label_return = emit_goto(s, OP_if_true, -1);
            emit_op(s, OP_drop);
            emit_goto(s, OP_goto, label_loop);

            emit_label(s, label_return);
            emit_op(s, OP_push_i32);
            emit_u32(s, 2);
            emit_op(s, OP_strict_eq);
            label_throw = emit_goto(s, OP_if_true, -1);

            /* return handling */
            if (is_async)
                emit_op(s, OP_await);
            emit_op(s, OP_iterator_call);
            emit_u8(s, 0);
            label_return1 = emit_goto(s, OP_if_true, -1);
            if (is_async)
                emit_op(s, OP_await);
            emit_op(s, OP_check_object);
            emit_op(s, OP_get_field2);
            emit_atom(s, JS_ATOM_done);
            emit_goto(s, OP_if_false, label_yield);

            emit_op(s, OP_get_field);
            emit_atom(s, JS_ATOM_value);

            emit_label(s, label_return1);
            emit_op(s, OP_nip);
            emit_op(s, OP_nip);
            emit_op(s, OP_nip);
            emit_return(s, true);

            /* throw handling */
            emit_label(s, label_throw);
            emit_op(s, OP_iterator_call);
            emit_u8(s, 1);
            label_throw1 = emit_goto(s, OP_if_true, -1);
            if (is_async)
                emit_op(s, OP_await);
            emit_op(s, OP_check_object);
            emit_op(s, OP_get_field2);
            emit_atom(s, JS_ATOM_done);
            emit_goto(s, OP_if_false, label_yield);
            emit_goto(s, OP_goto, label_next);
            /* close the iterator and throw a type error exception */
            emit_label(s, label_throw1);
            emit_op(s, OP_iterator_call);
            emit_u8(s, 2);
            label_throw2 = emit_goto(s, OP_if_true, -1);
            if (is_async)
                emit_op(s, OP_await);
            emit_label(s, label_throw2);

            emit_op(s, OP_throw_error);
            emit_atom(s, JS_ATOM_NULL);
            emit_u8(s, JS_THROW_ERROR_ITERATOR_THROW);

            emit_label(s, label_next);
            emit_op(s, OP_get_field);
            emit_atom(s, JS_ATOM_value);
            emit_op(s, OP_nip); /* keep the value associated with
                                   done = true */
            emit_op(s, OP_nip);
            emit_op(s, OP_nip);
        } else {
            int label_next;

            if (is_async)
                emit_op(s, OP_await);
            emit_op(s, OP_yield);
            label_next = emit_goto(s, OP_if_false, -1);
            emit_return(s, true);
            emit_label(s, label_next);
        }
        return 0;
    } else if (s->token.val == '(' &&
               js_parse_skip_parens_token(s, NULL, true) == TOK_ARROW) {
        return js_parse_function_decl(s, JS_PARSE_FUNC_ARROW,
                                      JS_FUNC_NORMAL, JS_ATOM_NULL,
                                      s->token.ptr, s->token.line_num,
                                      s->token.col_num);
    } else if (token_is_pseudo_keyword(s, JS_ATOM_async)) {
        const uint8_t *source_ptr;
        int tok, source_line_num, source_col_num;
        JSParsePos pos;

        /* fast test */
        tok = peek_token(s, true);
        if (tok == TOK_FUNCTION || tok == '\n')
            goto next;

        source_ptr = s->token.ptr;
        source_line_num = s->token.line_num;
        source_col_num = s->token.col_num;
        js_parse_get_pos(s, &pos);
        if (next_token(s))
            return -1;
        if ((s->token.val == '(' &&
             js_parse_skip_parens_token(s, NULL, true) == TOK_ARROW) ||
            (s->token.val == TOK_IDENT && !s->token.u.ident.is_reserved &&
             peek_token(s, true) == TOK_ARROW)) {
            return js_parse_function_decl(s, JS_PARSE_FUNC_ARROW,
                                          JS_FUNC_ASYNC, JS_ATOM_NULL,
                                          source_ptr, source_line_num,
                                          source_col_num);
        } else {
            /* undo the token parsing */
            if (js_parse_seek_token(s, &pos))
                return -1;
        }
    } else if (s->token.val == TOK_IDENT &&
               peek_token(s, true) == TOK_ARROW) {
        return js_parse_function_decl(s, JS_PARSE_FUNC_ARROW,
                                      JS_FUNC_NORMAL, JS_ATOM_NULL,
                                      s->token.ptr, s->token.line_num,
                                      s->token.col_num);
    }
 next:
    if (s->token.val == TOK_IDENT) {
        /* name0 is used to check for OP_set_name pattern, not duplicated */
        name0 = s->token.u.ident.atom;
    }
    if (js_parse_cond_expr(s, parse_flags))
        return -1;

    op = s->token.val;
    if (op == '=' || (op >= TOK_MUL_ASSIGN && op <= TOK_POW_ASSIGN)) {
        int label;
        if (next_token(s))
            return -1;
        if (get_lvalue(s, &opcode, &scope, &name, &label, NULL, (op != '='), op) < 0)
            return -1;

        // comply with rather obtuse evaluation order of computed properties:
        // obj[key]=val evaluates val->obj->key when obj is null/undefined
        // but key->obj->val when an object
        // FIXME(bnoordhuis) less stack shuffling; don't to_propkey twice in
        // happy path; replace `dup is_undefined_or_null if_true` with new
        // opcode if_undefined_or_null? replace `swap dup` with over?
        if (op == '=' && opcode == OP_get_array_el) {
            int label_next = -1;
            JSFunctionDef *fd = s->cur_func;
            assert(OP_to_propkey2 == fd->byte_code.buf[fd->last_opcode_pos]);
            fd->byte_code.size = fd->last_opcode_pos;
            fd->last_opcode_pos = -1;
            emit_op(s, OP_swap); // obj key -> key obj
            emit_op(s, OP_dup);
            emit_op(s, OP_is_undefined_or_null);
            label_next = emit_goto(s, OP_if_true, -1);
            emit_op(s, OP_swap);
            emit_op(s, OP_to_propkey);
            emit_op(s, OP_swap);
            emit_label(s, label_next);
            emit_op(s, OP_swap);
        }

        if (js_parse_assign_expr2(s, parse_flags)) {
            JS_FreeAtom(s->ctx, name);
            return -1;
        }

        if (op == '=' && opcode == OP_get_array_el) {
            emit_op(s, OP_swap); // obj key val -> obj val key
            emit_op(s, OP_to_propkey);
            emit_op(s, OP_swap);
        }

        if (op == '=') {
            if ((opcode == OP_get_ref_value || opcode == OP_scope_get_var) && name == name0) {
                set_object_name(s, name);
            }
        } else {
            emit_op(s, op - TOK_MUL_ASSIGN + OP_mul);
        }
        put_lvalue(s, opcode, scope, name, label, PUT_LVALUE_KEEP_TOP, false);
    } else if (op >= TOK_LAND_ASSIGN && op <= TOK_DOUBLE_QUESTION_MARK_ASSIGN) {
        int label, label1, depth_lvalue, label2;

        if (next_token(s))
            return -1;
        if (get_lvalue(s, &opcode, &scope, &name, &label,
                       &depth_lvalue, true, op) < 0)
            return -1;

        emit_op(s, OP_dup);
        if (op == TOK_DOUBLE_QUESTION_MARK_ASSIGN)
            emit_op(s, OP_is_undefined_or_null);
        label1 = emit_goto(s, op == TOK_LOR_ASSIGN ? OP_if_true : OP_if_false,
                           -1);
        emit_op(s, OP_drop);

        if (js_parse_assign_expr2(s, parse_flags)) {
            JS_FreeAtom(s->ctx, name);
            return -1;
        }

        if ((opcode == OP_get_ref_value || opcode == OP_scope_get_var) && name == name0) {
            set_object_name(s, name);
        }

        switch(depth_lvalue) {
        case 0:
            emit_op(s, OP_dup);
            break;
        case 1:
            emit_op(s, OP_insert2);
            break;
        case 2:
            emit_op(s, OP_insert3);
            break;
        case 3:
            emit_op(s, OP_insert4);
            break;
        default:
            abort();
        }

        /* XXX: we disable the OP_put_ref_value optimization by not
           using put_lvalue() otherwise depth_lvalue is not correct */
        put_lvalue(s, opcode, scope, name, label, PUT_LVALUE_NOKEEP_DEPTH,
                   false);
        label2 = emit_goto(s, OP_goto, -1);

        emit_label(s, label1);

        /* remove the lvalue stack entries */
        while (depth_lvalue != 0) {
            emit_op(s, OP_nip);
            depth_lvalue--;
        }

        emit_label(s, label2);
    }
    return 0;
}

static __exception int js_parse_assign_expr(JSParseState *s)
{
    return js_parse_assign_expr2(s, PF_IN_ACCEPTED);
}

/* allowed parse_flags: PF_IN_ACCEPTED */
static __exception int js_parse_expr2(JSParseState *s, int parse_flags)
{
    bool comma = false;
    for(;;) {
        if (js_parse_assign_expr2(s, parse_flags))
            return -1;
        if (comma) {
            /* prevent get_lvalue from using the last expression
               as an lvalue. This also prevents the conversion of
               of get_var to get_ref for method lookup in function
               call inside `with` statement.
             */
            s->cur_func->last_opcode_pos = -1;
        }
        if (s->token.val != ',')
            break;
        comma = true;
        if (next_token(s))
            return -1;
        emit_op(s, OP_drop);
    }
    return 0;
}

static __exception int js_parse_expr(JSParseState *s)
{
    return js_parse_expr2(s, PF_IN_ACCEPTED);
}

static void push_break_entry(JSFunctionDef *fd, BlockEnv *be,
                             JSAtom label_name,
                             int label_break, int label_cont,
                             int drop_count)
{
    be->prev = fd->top_break;
    fd->top_break = be;
    be->label_name = label_name;
    be->label_break = label_break;
    be->label_cont = label_cont;
    be->drop_count = drop_count;
    be->label_finally = -1;
    be->scope_level = fd->scope_level;
    be->has_iterator = false;
    be->is_regular_stmt = false;
    be->has_using = false;
    be->using_scope_level = -1;
}

static void pop_break_entry(JSFunctionDef *fd)
{
    BlockEnv *be;
    be = fd->top_break;
    fd->top_break = be->prev;
}

static __exception int emit_break(JSParseState *s, JSAtom name, int is_cont)
{
    BlockEnv *top;
    int i, scope_level;

    scope_level = s->cur_func->scope_level;
    top = s->cur_func->top_break;
    while (top != NULL) {
        close_scopes(s, scope_level, top->scope_level);
        scope_level = top->scope_level;
        if (is_cont &&
            top->label_cont != -1 &&
            (name == JS_ATOM_NULL || top->label_name == name)) {
            /* continue stays inside the same block */
            emit_goto(s, OP_goto, top->label_cont);
            return 0;
        }
        if (!is_cont && top->label_break != -1) {
            if (top->label_name == name ||
                (name == JS_ATOM_NULL && !top->is_regular_stmt)) {
                emit_goto(s, OP_goto, top->label_break);
                return 0;
            }
        }
        i = 0;
        if (top->has_iterator) {
            emit_op(s, OP_iterator_close);
            i += 3;
        }
        for(; i < top->drop_count; i++)
            emit_op(s, OP_drop);
        if (top->has_using) {
            emit_op(s, OP_using_dispose_init);
            emit_op(s, OP_dispose_scope);
            emit_u16(s, top->using_scope_level);
            emit_op(s, OP_using_dispose_end);
        }
        if (top->label_finally != -1) {
            /* must push dummy value to keep same stack depth */
            emit_op(s, OP_undefined);
            emit_goto(s, OP_gosub, top->label_finally);
            emit_op(s, OP_drop);
        }
        top = top->prev;
    }
    if (name == JS_ATOM_NULL) {
        if (is_cont)
            return js_parse_error(s, "continue must be inside loop");
        else
            return js_parse_error(s, "break must be inside loop or switch");
    } else {
        return js_parse_error(s, "break/continue label not found");
    }
}

/* execute the finally blocks before return */
static void emit_return(JSParseState *s, bool hasval)
{
    BlockEnv *top;

    if (s->cur_func->func_kind != JS_FUNC_NORMAL) {
        if (!hasval) {
            /* no value: direct return in case of async generator */
            emit_op(s, OP_undefined);
            hasval = true;
        } else if (s->cur_func->func_kind == JS_FUNC_ASYNC_GENERATOR) {
            /* the await must be done before handling the "finally" in
               case it raises an exception */
            emit_op(s, OP_await);
        }
    }

    top = s->cur_func->top_break;
    while (top != NULL) {
        if (top->has_iterator || top->label_finally != -1 ||
            top->has_using) {
            if (!hasval) {
                emit_op(s, OP_undefined);
                hasval = true;
            }
            /* Remove the stack elements up to and including the catch
               offset. When 'yield' is used in an expression we have
               no easy way to count them, so we use this specific
               instruction instead. */
            emit_op(s, OP_nip_catch);
            /* stack: iter_obj next ret_val */
            if (top->has_iterator) {
                if (s->cur_func->func_kind == JS_FUNC_ASYNC_GENERATOR) {
                    int label_next, label_next2;
                    emit_op(s, OP_nip); /* next */
                    emit_op(s, OP_swap);
                    emit_op(s, OP_get_field2);
                    emit_atom(s, JS_ATOM_return);
                    /* stack: iter_obj return_func */
                    emit_op(s, OP_dup);
                    emit_op(s, OP_is_undefined_or_null);
                    label_next = emit_goto(s, OP_if_true, -1);
                    emit_op(s, OP_call_method);
                    emit_u16(s, 0);
                    emit_op(s, OP_check_object);
                    emit_op(s, OP_await);
                    label_next2 = emit_goto(s, OP_goto, -1);
                    emit_label(s, label_next);
                    emit_op(s, OP_drop);
                    emit_label(s, label_next2);
                    emit_op(s, OP_drop);
                } else {
                    emit_op(s, OP_rot3r);
                    emit_op(s, OP_undefined); /* dummy catch offset */
                    emit_op(s, OP_iterator_close);
                }
            } else if (top->has_using) {
                /* Dispose using variables. The return value is on TOS.
                   stack: ret_val */
                emit_op(s, OP_using_dispose_init);  /* initial error_state */
                emit_op(s, OP_dispose_scope);
                emit_u16(s, top->using_scope_level);
                emit_op(s, OP_using_dispose_end);
                /* stack: ret_val */
            } else {
                /* execute the "finally" block */
                emit_goto(s, OP_gosub, top->label_finally);
            }
        }
        top = top->prev;
    }
    if (s->cur_func->is_derived_class_constructor) {
        int label_return;

        /* 'this' can be uninitialized, so it may be accessed only if
           the derived class constructor does not return an object */
        if (hasval) {
            emit_op(s, OP_check_ctor_return);
            label_return = emit_goto(s, OP_if_false, -1);
            emit_op(s, OP_drop);
        } else {
            label_return = -1;
        }

        /* XXX: if this is not initialized, should throw the
           ReferenceError in the caller realm */
        emit_op(s, OP_scope_get_var);
        emit_atom(s, JS_ATOM_this);
        emit_u16(s, 0);

        emit_label(s, label_return);
        emit_op(s, OP_return);
    } else if (s->cur_func->func_kind != JS_FUNC_NORMAL) {
        emit_op(s, OP_return_async);
    } else {
        emit_op(s, hasval ? OP_return : OP_return_undef);
    }
}

#define DECL_MASK_FUNC  (1 << 0) /* allow normal function declaration */
/* ored with DECL_MASK_FUNC if function declarations are allowed with a label */
#define DECL_MASK_FUNC_WITH_LABEL (1 << 1)
#define DECL_MASK_OTHER (1 << 2) /* all other declarations */
#define DECL_MASK_ALL   (DECL_MASK_FUNC | DECL_MASK_FUNC_WITH_LABEL | DECL_MASK_OTHER)

static __exception int js_parse_statement_or_decl(JSParseState *s,
                                                  int decl_mask);

static __exception int js_parse_statement(JSParseState *s)
{
    return js_parse_statement_or_decl(s, 0);
}

static __exception int js_parse_block(JSParseState *s)
{
    JSFunctionDef *fd = s->cur_func;

    if (js_parse_expect(s, '{'))
        return -1;
    if (s->token.val != '}') {
        BlockEnv using_be;
        int has_using_be = 0;
        int scope_level;

        push_scope(s);
        scope_level = fd->scope_level;
        for(;;) {
            if (js_parse_statement_or_decl(s, DECL_MASK_ALL))
                return -1;
            if (!has_using_be && fd->scopes[scope_level].has_using) {
                has_using_be = 1;
                push_break_entry(fd, &using_be, JS_ATOM_NULL, -1, -1, 1);
                using_be.has_using = true;
                using_be.using_scope_level = scope_level;
            }
            if (s->token.val == '}')
                break;
        }
        if (has_using_be) {
            int label_catch = fd->scopes[scope_level].using_label_catch;
            int label_end = fd->scopes[scope_level].using_label_end;

            pop_break_entry(fd);

            if (js_is_live_code(s)) {
                emit_op(s, OP_drop);  /* drop catch_offset */
                emit_op(s, OP_using_dispose_init);  /* initial error_state: no error */
                emit_op(s, OP_dispose_scope);
                emit_u16(s, scope_level);
                emit_op(s, OP_using_dispose_end);
                emit_goto(s, OP_goto, label_end);
            }

            emit_label(s, label_catch);
            /* Stack: exception_value (= initial error_state) */
            emit_op(s, OP_dispose_scope);
            emit_u16(s, scope_level);
            /* Stack: final_error_state (original or SuppressedError) */
            emit_op(s, OP_throw);

            emit_label(s, label_end);
        }
        pop_scope(s);
    }
    if (next_token(s))
        return -1;
    return 0;
}

/* allowed parse_flags: PF_IN_ACCEPTED */
static __exception int js_parse_var(JSParseState *s, int parse_flags, int tok,
                                    bool export_flag)
{
    JSContext *ctx = s->ctx;
    JSFunctionDef *fd = s->cur_func;
    JSAtom name = JS_ATOM_NULL;

    for (;;) {
        if (s->token.val == TOK_IDENT) {
            if (s->token.u.ident.is_reserved) {
                return js_parse_error_reserved_identifier(s);
            }
            name = JS_DupAtom(ctx, s->token.u.ident.atom);
            if (name == JS_ATOM_let &&
                (tok == TOK_LET || tok == TOK_CONST || tok == TOK_USING)) {
                js_parse_error(s, "'let' is not a valid lexical identifier");
                goto var_error;
            }
            int using_method_idx = -1;
            if (next_token(s))
                goto var_error;
            if (js_define_var(s, name, tok))
                goto var_error;
            if (tok == TOK_USING) {
                /* Allocate a paired hidden local for the cached dispose
                   method. Must be allocated immediately after the value
                   var so OP_using_dispose can locate it at value_idx + 1.
                */
                using_method_idx = add_scope_var(ctx, fd,
                                                 JS_ATOM__using_dispose_,
                                                 JS_VAR_USING_METHOD);
                if (using_method_idx < 0)
                    goto var_error;
                fd->vars[using_method_idx].is_lexical = 1;
                fd->vars[using_method_idx].is_const = 1;
            }
            if (export_flag) {
                if (!add_export_entry(s, s->cur_func->module, name, name,
                                      JS_EXPORT_TYPE_LOCAL))
                    goto var_error;
            }

            if (s->token.val == '=') {
                if (next_token(s))
                    goto var_error;
                if (tok == TOK_VAR) {
                    /* Must make a reference for proper `with` semantics */
                    int opcode, scope, label;
                    JSAtom name1;

                    emit_op(s, OP_scope_get_var);
                    emit_atom(s, name);
                    emit_u16(s, fd->scope_level);
                    if (get_lvalue(s, &opcode, &scope, &name1, &label, NULL, false, '=') < 0)
                        goto var_error;
                    if (js_parse_assign_expr2(s, parse_flags)) {
                        JS_FreeAtom(ctx, name1);
                        goto var_error;
                    }
                    set_object_name(s, name);
                    put_lvalue(s, opcode, scope, name1, label,
                               PUT_LVALUE_NOKEEP, false);
                } else {
                    bool init;

                    if (tok == TOK_USING) {
                        bool is_await = (parse_flags & PF_AWAIT_USING) != 0;

                        if (!fd->scopes[fd->scope_level].has_using) {
                            /* First 'using' in this scope: set up labels
                               for the catch handler and end of disposal */
                            fd->scopes[fd->scope_level].has_using = 1;
                            fd->scopes[fd->scope_level].using_label_catch =
                                new_label(s);
                            fd->scopes[fd->scope_level].using_label_end =
                                new_label(s);

                            /* Emit OP_catch: push catch_offset on the value
                               stack. If an exception occurs, control jumps
                               to catch_label with the exception value on the
                               stack instead of catch_offset. */
                            emit_goto(s, OP_catch,
                                fd->scopes[fd->scope_level].using_label_catch);
                        }
                        if (is_await)
                            fd->scopes[fd->scope_level].is_await_using = 1;
                    }

                    if (js_parse_assign_expr2(s, parse_flags))
                        goto var_error;
                    set_object_name(s, name);

                    if (tok == TOK_USING) {
                        emit_op(s, OP_using_check);
                        emit_u8(s, (parse_flags & PF_AWAIT_USING) != 0);
                        /* Stack: value, method. Store the method first.
                           Emit OP_put_loc directly (bypasses atom lookup)
                           so multiple using decls with the shared hidden
                           atom name don't collide. */
                        emit_op(s, OP_put_loc);
                        emit_u16(s, using_method_idx);
                    }

                    init = (tok == TOK_CONST || tok == TOK_LET || tok == TOK_USING);
                    emit_op(s, init ? OP_scope_put_var_init : OP_scope_put_var);
                    emit_atom(s, name);
                    emit_u16(s, fd->scope_level);
                }
            } else {
                if (tok == TOK_CONST || tok == TOK_USING) {
                    js_parse_error(s, "missing initializer for variable");
                    goto var_error;
                }
                if (tok == TOK_LET) {
                    /* initialize lexical variable upon entering its scope */
                    emit_op(s, OP_undefined);
                    emit_op(s, OP_scope_put_var_init);
                    emit_atom(s, name);
                    emit_u16(s, fd->scope_level);
                }
            }
            JS_FreeAtom(ctx, name);
        } else {
            int skip_bits;
            if ((s->token.val == '[' || s->token.val == '{')
            &&  js_parse_skip_parens_token(s, &skip_bits, false) == '=') {
                /* using declarations do not allow binding patterns */
                if (tok == TOK_USING) {
                    return js_parse_error(s, "binding patterns are not allowed in using declarations");
                }
                emit_op(s, OP_undefined);
                if (js_parse_destructuring_element(s, tok, false, true, skip_bits & SKIP_HAS_ELLIPSIS, true, export_flag) < 0)
                    return -1;
            } else {
                return js_parse_error(s, "variable name expected");
            }
        }
        if (s->token.val != ',')
            break;
        if (next_token(s))
            return -1;
    }
    return 0;

 var_error:
    JS_FreeAtom(ctx, name);
    return -1;
}

/* test if the current token is a label. Use simplistic look-ahead scanner */
static bool is_label(JSParseState *s)
{
    return (s->token.val == TOK_IDENT && !s->token.u.ident.is_reserved &&
            peek_token(s, false) == ':');
}

/* test if the current token is a let keyword. Use simplistic look-ahead scanner */
static int is_let(JSParseState *s, int decl_mask)
{
    int res = false;

    if (token_is_pseudo_keyword(s, JS_ATOM_let)) {
        JSParsePos pos;
        js_parse_get_pos(s, &pos);
        for (;;) {
            if (next_token(s)) {
                res = -1;
                break;
            }
            if (s->token.val == '[') {
                /* let [ is a syntax restriction:
                   it never introduces an ExpressionStatement */
                res = true;
                break;
            }
            if (s->token.val == '{' ||
                (s->token.val == TOK_IDENT && !s->token.u.ident.is_reserved) ||
                s->token.val == TOK_LET ||
                s->token.val == TOK_YIELD ||
                s->token.val == TOK_AWAIT) {
                /* Check for possible ASI if not scanning for Declaration */
                /* XXX: should also check that `{` introduces a BindingPattern,
                   but Firefox does not and rejects eval("let=1;let\n{if(1)2;}") */
                if (s->last_line_num == s->token.line_num || (decl_mask & DECL_MASK_OTHER)) {
                    res = true;
                    break;
                }
                break;
            }
            break;
        }
        if (js_parse_seek_token(s, &pos)) {
            res = -1;
        }
    }
    return res;
}

/* Return 1 if the current token is `using` introducing a UsingDeclaration,
   0 if it is a plain identifier usage, or -1 on error.
   If `is_for_of` is true, `using of` is specifically treated as identifier
   per the for-of lookahead restriction. */
static int is_using(JSParseState *s, bool is_for_of)
{
    int res = false;
    if (token_is_pseudo_keyword(s, JS_ATOM_using)) {
        JSParsePos pos;
        js_parse_get_pos(s, &pos);
        if (next_token(s))
            return -1;
        /* No line terminator allowed between `using` and the binding */
        if (s->last_line_num == s->token.line_num) {
            if (s->token.val == TOK_IDENT && !s->token.u.ident.is_reserved) {
                if (!(is_for_of && s->token.u.ident.atom == JS_ATOM_of)) {
                    res = true;
                }
            }
        }
        if (js_parse_seek_token(s, &pos))
            res = -1;
    }
    return res;
}

/* XXX: handle IteratorClose when exiting the loop before the
   enumeration is done */
static __exception int js_parse_for_in_of(JSParseState *s, int label_name,
                                          bool is_async,
                                          int source_line_num,
                                          int source_col_num)
{
    JSContext *ctx = s->ctx;
    JSFunctionDef *fd = s->cur_func;
    JSAtom var_name;
    bool has_initializer, is_for_of, has_destructuring;
    int tok, tok1, opcode, scope, block_scope_level;
    int label_next, label_expr, label_cont, label_body, label_break;
    int pos_next, pos_expr;
    BlockEnv break_entry;

    has_initializer = false;
    has_destructuring = false;
    is_for_of = false;
    block_scope_level = fd->scope_level;
    label_cont = new_label(s);
    label_body = new_label(s);
    label_break = new_label(s);
    label_next = new_label(s);

    /* create scope for the lexical variables declared in the enumeration
       expressions. XXX: Not completely correct because of weird capturing
       semantics in `for (i of o) a.push(function(){return i})` */
    push_scope(s);

    /* local for_in scope starts here so individual elements
       can be closed in statement. */
    push_break_entry(s->cur_func, &break_entry,
                     label_name, label_break, label_cont, 1);
    break_entry.scope_level = block_scope_level;

    label_expr = emit_goto(s, OP_goto, -1);

    pos_next = s->cur_func->byte_code.size;
    emit_label(s, label_next);

    tok = s->token.val;
    switch (is_let(s, DECL_MASK_OTHER)) {
    case true:
        tok = TOK_LET;
        break;
    case false:
        break;
    default:
        return -1;
    }
    bool is_await_using = false;
    if (tok == TOK_AWAIT) {
        int u;
        if (next_token(s))
            return -1;
        u = is_using(s, false);
        if (u < 0)
            return -1;
        if (!u)
            return js_parse_error(s, "'using' expected");
        tok = TOK_USING;
        is_await_using = true;
        s->cur_func->has_await = true;
    } else if (token_is_pseudo_keyword(s, JS_ATOM_using)) {
        int u = is_using(s, true);
        if (u < 0)
            return -1;
        if (u)
            tok = TOK_USING;
    }
    if (tok == TOK_VAR || tok == TOK_LET || tok == TOK_CONST || tok == TOK_USING) {
        if (next_token(s))
            return -1;

        if (!(s->token.val == TOK_IDENT && !s->token.u.ident.is_reserved)) {
            if (s->token.val == '[' || s->token.val == '{') {
                if (js_parse_destructuring_element(s, tok, false, true, -1, false, false) < 0)
                    return -1;
                has_destructuring = true;
            } else {
                return js_parse_error(s, "variable name expected");
            }
            var_name = JS_ATOM_NULL;
        } else {
            bool init;
            var_name = JS_DupAtom(ctx, s->token.u.ident.atom);
            if (var_name == JS_ATOM_let &&
                (tok == TOK_LET || tok == TOK_CONST || tok == TOK_USING)) {
                JS_FreeAtom(s->ctx, var_name);
                return js_parse_error(s, "'let' is not a valid lexical identifier");
            }
            if (next_token(s)) {
                JS_FreeAtom(s->ctx, var_name);
                return -1;
            }
            if (js_define_var(s, var_name, tok)) {
                JS_FreeAtom(s->ctx, var_name);
                return -1;
            }
            if (tok == TOK_USING) {
                int mi = add_scope_var(ctx, fd, JS_ATOM__using_dispose_,
                                       JS_VAR_USING_METHOD);
                if (mi < 0) {
                    JS_FreeAtom(s->ctx, var_name);
                    return -1;
                }
                fd->vars[mi].is_lexical = 1;
                fd->vars[mi].is_const = 1;
                emit_op(s, OP_using_check);
                emit_u8(s, is_await_using);
                emit_op(s, OP_put_loc);
                emit_u16(s, mi);
            }
            init = (tok == TOK_CONST || tok == TOK_LET || tok == TOK_USING);
            emit_op(s, init ? OP_scope_put_var_init : OP_scope_put_var);
            emit_atom(s, var_name);
            emit_u16(s, fd->scope_level);
            if (tok == TOK_USING) {
                if (!fd->scopes[fd->scope_level].has_using) {
                    fd->scopes[fd->scope_level].has_using = 1;
                    fd->scopes[fd->scope_level].using_label_catch = new_label(s);
                    fd->scopes[fd->scope_level].using_label_end = new_label(s);
                }
                if (is_await_using)
                    fd->scopes[fd->scope_level].is_await_using = 1;
            }
        }
    } else if (!is_async && token_is_pseudo_keyword(s, JS_ATOM_async) && peek_token(s, false) == TOK_OF) {
        return js_parse_error(s, "'for of' expression cannot start with 'async'");
    } else {
        int skip_bits;
        if ((s->token.val == '[' || s->token.val == '{')
        &&  ((tok1 = js_parse_skip_parens_token(s, &skip_bits, false)) == TOK_IN || tok1 == TOK_OF)) {
            if (js_parse_destructuring_element(s, 0, false, true, skip_bits & SKIP_HAS_ELLIPSIS, true, false) < 0)
                return -1;
        } else {
            int lvalue_label;
            if (js_parse_left_hand_side_expr(s))
                return -1;
            if (get_lvalue(s, &opcode, &scope, &var_name, &lvalue_label,
                           NULL, false, TOK_FOR))
                return -1;
            put_lvalue(s, opcode, scope, var_name, lvalue_label,
                       PUT_LVALUE_NOKEEP_BOTTOM, false);
        }
        var_name = JS_ATOM_NULL;
    }
    emit_goto(s, OP_goto, label_body);

    pos_expr = s->cur_func->byte_code.size;
    emit_label(s, label_expr);
    if (s->token.val == '=') {
        /* XXX: potential scoping issue if inside `with` statement */
        has_initializer = true;
        /* parse and evaluate initializer prior to evaluating the
           object (only used with "for in" with a non lexical variable
           in non strict mode */
        if (next_token(s) || js_parse_assign_expr2(s, 0)) {
            JS_FreeAtom(ctx, var_name);
            return -1;
        }
        if (var_name != JS_ATOM_NULL) {
            emit_op(s, OP_scope_put_var);
            emit_atom(s, var_name);
            emit_u16(s, fd->scope_level);
        }
    }
    JS_FreeAtom(ctx, var_name);

    if (token_is_pseudo_keyword(s, JS_ATOM_of)) {
        is_for_of = true;
        if (has_initializer)
            goto initializer_error;
    } else if (s->token.val == TOK_IN) {
        if (is_async)
            return js_parse_error(s, "'for await' loop should be used with 'of'");
        if (tok == TOK_USING)
            return js_parse_error(s, "using declaration not allowed in for-in");
        if (has_initializer &&
            (tok != TOK_VAR || fd->is_strict_mode || has_destructuring)) {
        initializer_error:
            return js_parse_error(s, "a declaration in the head of a for-%s loop can't have an initializer",
                                  is_for_of ? "of" : "in");
        }
    } else {
        return js_parse_error(s, "expected 'of' or 'in' in for control expression");
    }
    if (next_token(s))
        return -1;
    if (is_for_of) {
        if (js_parse_assign_expr(s))
            return -1;
    } else {
        if (js_parse_expr(s))
            return -1;
    }
    /* close the scope after having evaluated the expression so that
       the TDZ values are in the closures */
    close_scopes(s, s->cur_func->scope_level, block_scope_level);
    if (is_for_of) {
        /* set has_iterator after the iterable expression is parsed so
           that a yield in the expression does not try to close a
           not-yet-created iterator */
        break_entry.has_iterator = true;
        break_entry.drop_count += 2;
        if (is_async)
            emit_op(s, OP_for_await_of_start);
        else
            emit_op(s, OP_for_of_start);
        /* on stack: enum_rec */
    } else {
        emit_op(s, OP_for_in_start);
        /* on stack: enum_obj */
    }
    emit_goto(s, OP_goto, label_cont);

    if (js_parse_expect(s, ')'))
        return -1;

    {
        /* move the `next` code here */
        DynBuf *bc = &s->cur_func->byte_code;
        int chunk_size = pos_expr - pos_next;
        int offset = bc->size - pos_next;
        int i;
        if (dbuf_claim(bc, chunk_size))
            return -1;
        dbuf_put(bc, bc->buf + pos_next, chunk_size);
        memset(bc->buf + pos_next, OP_nop, chunk_size);
        /* `next` part ends with a goto */
        s->cur_func->last_opcode_pos = bc->size - 5;
        /* relocate labels */
        for (i = label_cont; i < s->cur_func->label_count; i++) {
            LabelSlot *ls = &s->cur_func->label_slots[i];
            if (ls->pos >= pos_next && ls->pos < pos_expr)
                ls->pos += offset;
        }
    }

    emit_label(s, label_body);
    {
        bool scope_has_using = fd->scopes[fd->scope_level].has_using;
        int using_scope_level = fd->scope_level;
        BlockEnv using_be;
        int had_using_be = 0;

        if (scope_has_using) {
            emit_goto(s, OP_catch, fd->scopes[using_scope_level].using_label_catch);
            push_break_entry(fd, &using_be, JS_ATOM_NULL, -1, -1, 1);
            using_be.has_using = true;
            using_be.using_scope_level = using_scope_level;
            had_using_be = 1;
        }

        if (js_parse_statement(s))
            return -1;

        if (had_using_be) {
            pop_break_entry(fd);
        }

        if (scope_has_using && js_is_live_code(s)) {
            emit_op(s, OP_drop);
            emit_op(s, OP_using_dispose_init);
            emit_op(s, OP_dispose_scope);
            emit_u16(s, using_scope_level);
            emit_op(s, OP_using_dispose_end);
            emit_goto(s, OP_goto,
                      fd->scopes[using_scope_level].using_label_end);
        }

        if (scope_has_using) {
            emit_label(s, fd->scopes[using_scope_level].using_label_catch);
            emit_op(s, OP_dispose_scope);
            emit_u16(s, using_scope_level);
            emit_op(s, OP_throw);

            emit_label(s, fd->scopes[using_scope_level].using_label_end);
        }
    }

    close_scopes(s, s->cur_func->scope_level, block_scope_level);

    emit_label(s, label_cont);
    if (is_for_of) {
        if (is_async) {
            /* call the next method */
            /* stack: iter_obj next catch_offset */
            emit_op(s, OP_dup3);
            emit_op(s, OP_drop);
            emit_op(s, OP_call_method);
            emit_u16(s, 0);
            /* get the result of the promise */
            emit_op(s, OP_await);
            /* unwrap the value and done values */
            emit_op(s, OP_iterator_get_value_done);
        } else {
            emit_op(s, OP_for_of_next);
            emit_u8(s, 0);
        }
    } else {
        emit_op(s, OP_for_in_next);
    }
    /* on stack: enum_rec / enum_obj value bool */
    emit_goto(s, OP_if_false, label_next);
    /* drop the undefined value from for_xx_next */
    emit_op(s, OP_drop);

    emit_label(s, label_break);
    if (is_for_of) {
        /* close and drop enum_rec */
        emit_source_loc_at(s, source_line_num, source_col_num);
        emit_op(s, OP_iterator_close);
    } else {
        emit_op(s, OP_drop);
    }
    pop_break_entry(s->cur_func);
    pop_scope(s);
    return 0;
}

static void set_eval_ret_undefined(JSParseState *s)
{
    if (s->cur_func->eval_ret_idx >= 0) {
        emit_op(s, OP_undefined);
        emit_op(s, OP_put_loc);
        emit_u16(s, s->cur_func->eval_ret_idx);
    }
}

static __exception int js_parse_statement_or_decl(JSParseState *s,
                                                  int decl_mask)
{
    JSContext *ctx = s->ctx;
    JSAtom label_name;
    int tok;

    /* specific label handling */
    /* XXX: support multiple labels on loop statements */
    label_name = JS_ATOM_NULL;
    if (is_label(s)) {
        BlockEnv *be;

        label_name = JS_DupAtom(ctx, s->token.u.ident.atom);

        for (be = s->cur_func->top_break; be; be = be->prev) {
            if (be->label_name == label_name) {
                js_parse_error(s, "duplicate label name");
                goto fail;
            }
        }

        if (next_token(s))
            goto fail;
        if (js_parse_expect(s, ':'))
            goto fail;
        if (s->token.val != TOK_FOR
        &&  s->token.val != TOK_DO
        &&  s->token.val != TOK_WHILE) {
            /* labelled regular statement */
            JSFunctionDef *fd = s->cur_func;
            int label_break, mask;
            BlockEnv break_entry;

            label_break = new_label(s);
            push_break_entry(fd, &break_entry, label_name, label_break, -1, 0);
            fd->top_break->is_regular_stmt = 1;
            if (!fd->is_strict_mode &&
                (decl_mask & DECL_MASK_FUNC_WITH_LABEL)) {
                mask = DECL_MASK_FUNC | DECL_MASK_FUNC_WITH_LABEL;
            } else {
                mask = 0;
            }
            if (js_parse_statement_or_decl(s, mask))
                goto fail;
            emit_label(s, label_break);
            pop_break_entry(fd);
            goto done;
        }
    }

    switch(tok = s->token.val) {
    case '{':
        if (js_parse_block(s))
            goto fail;
        break;
    case TOK_RETURN:
        if (s->cur_func->is_eval) {
            js_parse_error(s, "return not in a function");
            goto fail;
        }
        if (s->cur_func->func_type == JS_PARSE_FUNC_CLASS_STATIC_INIT) {
            js_parse_error(s, "return in a static initializer block");
            goto fail;
        }
        if (next_token(s))
            goto fail;
        if (s->token.val != ';' && s->token.val != '}' && !s->got_lf) {
            if (js_parse_expr(s))
                goto fail;
            emit_return(s, true);
        } else {
            emit_return(s, false);
        }
        if (js_parse_expect_semi(s))
            goto fail;
        break;
    case TOK_THROW:
        if (next_token(s))
            goto fail;
        if (s->got_lf) {
            js_parse_error(s, "line terminator not allowed after throw");
            goto fail;
        }
        emit_source_loc(s);
        if (js_parse_expr(s))
            goto fail;
        emit_op(s, OP_throw);
        if (js_parse_expect_semi(s))
            goto fail;
        break;
    case TOK_AWAIT:
        /* Check for 'await using' declaration */
        if (s->cur_func->func_kind & JS_FUNC_ASYNC) {
            JSParsePos pos;
            int u;
            js_parse_get_pos(s, &pos);
            if (next_token(s)) /* skip 'await' */
                goto fail;
            u = is_using(s, false);
            if (u < 0)
                goto fail;
            if (u) {
                if (!(decl_mask & DECL_MASK_OTHER)) {
                    js_parse_error(s, "lexical declarations can't appear in single-statement context");
                    goto fail;
                }
                s->cur_func->has_await = true;
                if (next_token(s)) /* skip 'using' */
                    goto fail;
                if (js_parse_var(s, PF_IN_ACCEPTED | PF_AWAIT_USING, TOK_USING, /*export_flag*/false))
                    goto fail;
                if (js_parse_expect_semi(s))
                    goto fail;
                break;
            }
            /* Not 'await using': restore to parse as expression */
            if (js_parse_seek_token(s, &pos))
                goto fail;
        }
        goto hasexpr;
    case TOK_LET:
    case TOK_CONST:
    haslet:
        if (!(decl_mask & DECL_MASK_OTHER)) {
            js_parse_error(s, "lexical declarations can't appear in single-statement context");
            goto fail;
        }
        /* fall thru */
    case TOK_VAR:
        if (next_token(s))
            goto fail;
        if (js_parse_var(s, PF_IN_ACCEPTED, tok, /*export_flag*/false))
            goto fail;
        if (js_parse_expect_semi(s))
            goto fail;
        break;
    case TOK_IF:
        {
            int label1, label2, mask;
            if (next_token(s))
                goto fail;
            /* create a new scope for `let f;if(1) function f(){}` */
            push_scope(s);
            set_eval_ret_undefined(s);
            if (js_parse_expr_paren(s))
                goto fail;
            label1 = emit_goto(s, OP_if_false, -1);
            if (s->cur_func->is_strict_mode)
                mask = 0;
            else
                mask = DECL_MASK_FUNC; /* Annex B.3.4 */

            if (js_parse_statement_or_decl(s, mask))
                goto fail;

            if (s->token.val == TOK_ELSE) {
                label2 = emit_goto(s, OP_goto, -1);
                if (next_token(s))
                    goto fail;

                emit_label(s, label1);
                if (js_parse_statement_or_decl(s, mask))
                    goto fail;

                label1 = label2;
            }
            emit_label(s, label1);
            pop_scope(s);
        }
        break;
    case TOK_WHILE:
        {
            int label_cont, label_break;
            BlockEnv break_entry;

            label_cont = new_label(s);
            label_break = new_label(s);

            push_break_entry(s->cur_func, &break_entry,
                             label_name, label_break, label_cont, 0);

            if (next_token(s))
                goto fail;

            set_eval_ret_undefined(s);

            emit_label(s, label_cont);
            if (js_parse_expr_paren(s))
                goto fail;
            emit_goto(s, OP_if_false, label_break);

            if (js_parse_statement(s))
                goto fail;
            emit_goto(s, OP_goto, label_cont);

            emit_label(s, label_break);

            pop_break_entry(s->cur_func);
        }
        break;
    case TOK_DO:
        {
            int label_cont, label_break, label1;
            BlockEnv break_entry;

            label_cont = new_label(s);
            label_break = new_label(s);
            label1 = new_label(s);

            push_break_entry(s->cur_func, &break_entry,
                             label_name, label_break, label_cont, 0);

            if (next_token(s))
                goto fail;

            emit_label(s, label1);

            set_eval_ret_undefined(s);

            if (js_parse_statement(s))
                goto fail;

            emit_label(s, label_cont);
            if (js_parse_expect(s, TOK_WHILE))
                goto fail;
            if (js_parse_expr_paren(s))
                goto fail;
            /* Insert semicolon if missing */
            if (s->token.val == ';') {
                if (next_token(s))
                    goto fail;
            }
            emit_goto(s, OP_if_true, label1);

            emit_label(s, label_break);

            pop_break_entry(s->cur_func);
        }
        break;
    case TOK_FOR:
        {
            int label_cont, label_break, label_body, label_test;
            int pos_cont, pos_body, block_scope_level;
            int for_scope_level;
            BlockEnv break_entry;
            int tok, bits;
            int source_line_num, source_col_num;
            bool is_async;

            source_line_num = s->token.line_num;
            source_col_num = s->token.col_num;
            if (next_token(s))
                goto fail;

            set_eval_ret_undefined(s);
            bits = 0;
            is_async = false;
            if (s->token.val == '(') {
                js_parse_skip_parens_token(s, &bits, false);
            } else if (s->token.val == TOK_AWAIT) {
                if (!(s->cur_func->func_kind & JS_FUNC_ASYNC)) {
                    js_parse_error(s, "for await is only valid in asynchronous functions");
                    goto fail;
                }
                is_async = true;
                if (next_token(s))
                    goto fail;
                s->cur_func->has_await = true;
            }
            if (js_parse_expect(s, '('))
                goto fail;

            if (!(bits & SKIP_HAS_SEMI)) {
                /* parse for/in or for/of */
                if (js_parse_for_in_of(s, label_name, is_async,
                                       source_line_num, source_col_num))
                    goto fail;
                break;
            }
            block_scope_level = s->cur_func->scope_level;

            /* create scope for the lexical variables declared in the initial,
               test and increment expressions */
            push_scope(s);
            for_scope_level = s->cur_func->scope_level;
            /* initial expression */
            tok = s->token.val;
            if (tok != ';') {
                switch (is_let(s, DECL_MASK_OTHER)) {
                case true:
                    tok = TOK_LET;
                    break;
                case false:
                    break;
                default:
                    goto fail;
                }
                if (tok != TOK_VAR && tok != TOK_LET && tok != TOK_CONST &&
                    token_is_pseudo_keyword(s, JS_ATOM_using)) {
                    int u = is_using(s, false);
                    if (u < 0)
                        goto fail;
                    if (u)
                        tok = TOK_USING;
                }
                if (tok == TOK_AWAIT &&
                    (s->cur_func->func_kind & JS_FUNC_ASYNC)) {
                    /* Check for `await using` declaration head */
                    JSParsePos pos;
                    int u;
                    js_parse_get_pos(s, &pos);
                    if (next_token(s)) /* skip 'await' */
                        goto fail;
                    u = is_using(s, false);
                    if (u < 0)
                        goto fail;
                    if (u) {
                        s->cur_func->has_await = true;
                        tok = TOK_USING;
                        if (next_token(s)) /* skip 'using' */
                            goto fail;
                        if (js_parse_var(s, PF_IN_ACCEPTED | PF_AWAIT_USING,
                                         TOK_USING, false))
                            goto fail;
                        goto for_init_done;
                    }
                    if (js_parse_seek_token(s, &pos))
                        goto fail;
                }
                if (tok == TOK_VAR
                || tok == TOK_LET
                || tok == TOK_CONST
                || tok == TOK_USING) {
                    int pf = 0;
                    if (tok == TOK_USING && is_async)
                        pf |= PF_AWAIT_USING;
                    if (next_token(s))
                        goto fail;
                    if (js_parse_var(s, pf, tok, /*export_flag*/false))
                        goto fail;
                } else {
                    if (js_parse_expr2(s, false))
                        goto fail;
                    emit_op(s, OP_drop);
                }

            for_init_done:
                /* close the closures before the first iteration */
                close_scopes(s, s->cur_func->scope_level, block_scope_level);
            }
            if (js_parse_expect(s, ';'))
                goto fail;

            label_test = new_label(s);
            label_cont = new_label(s);
            label_body = new_label(s);
            label_break = new_label(s);

            push_break_entry(s->cur_func, &break_entry,
                             label_name, label_break, label_cont, 0);
            if (s->cur_func->scopes[for_scope_level].has_using) {
                break_entry.has_using = true;
                break_entry.using_scope_level = for_scope_level;
                break_entry.drop_count = 1; /* catch_offset from OP_catch */
            }

            /* test expression */
            if (s->token.val == ';') {
                /* no test expression */
                label_test = label_body;
            } else {
                emit_label(s, label_test);
                if (js_parse_expr(s))
                    goto fail;
                emit_goto(s, OP_if_false, label_break);
            }
            if (js_parse_expect(s, ';'))
                goto fail;

            if (s->token.val == ')') {
                /* no end expression */
                break_entry.label_cont = label_cont = label_test;
                pos_cont = 0; /* avoid warning */
            } else {
                /* skip the end expression */
                emit_goto(s, OP_goto, label_body);

                pos_cont = s->cur_func->byte_code.size;
                emit_label(s, label_cont);
                if (js_parse_expr(s))
                    goto fail;
                emit_op(s, OP_drop);
                if (label_test != label_body)
                    emit_goto(s, OP_goto, label_test);
            }
            if (js_parse_expect(s, ')'))
                goto fail;

            pos_body = s->cur_func->byte_code.size;
            emit_label(s, label_body);
            if (js_parse_statement(s))
                goto fail;

            /* close the closures before the next iteration */
            /* XXX: check continue case */
            close_scopes(s, s->cur_func->scope_level, block_scope_level);

            if (label_test != label_body && label_cont != label_test) {
                /* move the increment code here */
                DynBuf *bc = &s->cur_func->byte_code;
                int chunk_size = pos_body - pos_cont;
                int offset = bc->size - pos_cont;
                int i;
                if (dbuf_claim(bc, chunk_size))
                    return -1;
                dbuf_put(bc, bc->buf + pos_cont, chunk_size);
                memset(bc->buf + pos_cont, OP_nop, chunk_size);
                /* increment part ends with a goto */
                s->cur_func->last_opcode_pos = bc->size - 5;
                /* relocate labels */
                for (i = label_cont; i < s->cur_func->label_count; i++) {
                    LabelSlot *ls = &s->cur_func->label_slots[i];
                    if (ls->pos >= pos_cont && ls->pos < pos_body)
                        ls->pos += offset;
                }
            } else {
                emit_goto(s, OP_goto, label_cont);
            }

            emit_label(s, label_break);

            pop_break_entry(s->cur_func);

            if (s->cur_func->scopes[for_scope_level].has_using) {
                int label_catch = s->cur_func->scopes[for_scope_level].using_label_catch;
                int label_end = s->cur_func->scopes[for_scope_level].using_label_end;

                /* Normal exit: drop catch_offset, dispose */
                emit_op(s, OP_drop);
                emit_op(s, OP_using_dispose_init);
                emit_op(s, OP_dispose_scope);
                emit_u16(s, for_scope_level);
                emit_op(s, OP_using_dispose_end);
                emit_goto(s, OP_goto, label_end);

                /* Catch handler: exception on stack */
                emit_label(s, label_catch);
                emit_op(s, OP_dispose_scope);
                emit_u16(s, for_scope_level);
                emit_op(s, OP_throw);

                emit_label(s, label_end);
            }
            pop_scope(s);
        }
        break;
    case TOK_BREAK:
    case TOK_CONTINUE:
        {
            int is_cont = s->token.val - TOK_BREAK;
            int label;

            if (next_token(s))
                goto fail;
            if (!s->got_lf && s->token.val == TOK_IDENT && !s->token.u.ident.is_reserved)
                label = s->token.u.ident.atom;
            else
                label = JS_ATOM_NULL;
            if (emit_break(s, label, is_cont))
                goto fail;
            if (label != JS_ATOM_NULL) {
                if (next_token(s))
                    goto fail;
            }
            if (js_parse_expect_semi(s))
                goto fail;
        }
        break;
    case TOK_SWITCH:
        {
            int label_case, label_break, label1;
            int default_label_pos;
            BlockEnv break_entry;

            if (next_token(s))
                goto fail;

            set_eval_ret_undefined(s);
            if (js_parse_expr_paren(s))
                goto fail;

            push_scope(s);
            label_break = new_label(s);
            push_break_entry(s->cur_func, &break_entry,
                             label_name, label_break, -1, 1);

            if (js_parse_expect(s, '{'))
                goto fail;

            default_label_pos = -1;
            label_case = -1;
            while (s->token.val != '}') {
                if (s->token.val == TOK_CASE) {
                    label1 = -1;
                    if (label_case >= 0) {
                        /* skip the case if needed */
                        label1 = emit_goto(s, OP_goto, -1);
                    }
                    emit_label(s, label_case);
                    label_case = -1;
                    for (;;) {
                        /* parse a sequence of case clauses */
                        if (next_token(s))
                            goto fail;
                        emit_op(s, OP_dup);
                        if (js_parse_expr(s))
                            goto fail;
                        if (js_parse_expect(s, ':'))
                            goto fail;
                        emit_op(s, OP_strict_eq);
                        if (s->token.val == TOK_CASE) {
                            label1 = emit_goto(s, OP_if_true, label1);
                        } else {
                            label_case = emit_goto(s, OP_if_false, -1);
                            emit_label(s, label1);
                            break;
                        }
                    }
                } else if (s->token.val == TOK_DEFAULT) {
                    if (next_token(s))
                        goto fail;
                    if (js_parse_expect(s, ':'))
                        goto fail;
                    if (default_label_pos >= 0) {
                        js_parse_error(s, "duplicate default");
                        goto fail;
                    }
                    if (label_case < 0) {
                        /* falling thru direct from switch expression */
                        label_case = emit_goto(s, OP_goto, -1);
                    }
                    /* Emit a dummy label opcode. Label will be patched after
                       the end of the switch body. Do not use emit_label(s, 0)
                       because it would clobber label 0 address, preventing
                       proper optimizer operation.
                     */
                    emit_op(s, OP_label);
                    emit_u32(s, 0);
                    default_label_pos = s->cur_func->byte_code.size - 4;
                } else {
                    if (label_case < 0) {
                        /* falling thru direct from switch expression */
                        js_parse_error(s, "invalid switch statement");
                        goto fail;
                    }
                    /* `using` / `await using` declarations are not allowed
                       directly within a CaseClause or DefaultClause
                       (early error per spec). */
                    if (token_is_pseudo_keyword(s, JS_ATOM_using)) {
                        int u = is_using(s, false);
                        if (u < 0)
                            goto fail;
                        if (u) {
                            js_parse_error(s, "using declaration is not allowed in this context");
                            goto fail;
                        }
                    }
                    if (s->token.val == TOK_AWAIT &&
                        (s->cur_func->func_kind & JS_FUNC_ASYNC)) {
                        JSParsePos pos;
                        int u;
                        js_parse_get_pos(s, &pos);
                        if (next_token(s))
                            goto fail;
                        u = is_using(s, false);
                        if (u < 0)
                            goto fail;
                        if (js_parse_seek_token(s, &pos))
                            goto fail;
                        if (u) {
                            js_parse_error(s, "await using declaration is not allowed in this context");
                            goto fail;
                        }
                    }
                    if (js_parse_statement_or_decl(s, DECL_MASK_ALL))
                        goto fail;
                }
            }
            if (js_parse_expect(s, '}'))
                goto fail;
            if (default_label_pos >= 0) {
                /* Ugly patch for the `default` label, shameful and risky */
                put_u32(s->cur_func->byte_code.buf + default_label_pos,
                        label_case);
                s->cur_func->label_slots[label_case].pos = default_label_pos + 4;
            } else {
                emit_label(s, label_case);
            }
            emit_label(s, label_break);
            emit_op(s, OP_drop); /* drop the switch expression */

            pop_break_entry(s->cur_func);
            pop_scope(s);
        }
        break;
    case TOK_TRY:
        {
            int label_catch, label_catch2, label_finally, label_end;
            JSAtom name;
            BlockEnv block_env;

            set_eval_ret_undefined(s);
            if (next_token(s))
                goto fail;
            label_catch = new_label(s);
            label_catch2 = new_label(s);
            label_finally = new_label(s);
            label_end = new_label(s);

            emit_goto(s, OP_catch, label_catch);

            push_break_entry(s->cur_func, &block_env,
                             JS_ATOM_NULL, -1, -1, 1);
            block_env.label_finally = label_finally;

            if (js_parse_block(s))
                goto fail;

            pop_break_entry(s->cur_func);

            if (js_is_live_code(s)) {
                /* drop the catch offset */
                emit_op(s, OP_drop);
                /* must push dummy value to keep same stack size */
                emit_op(s, OP_undefined);
                emit_goto(s, OP_gosub, label_finally);
                emit_op(s, OP_drop);

                emit_goto(s, OP_goto, label_end);
            }

            if (s->token.val == TOK_CATCH) {
                if (next_token(s))
                    goto fail;

                push_scope(s);  /* catch variable */
                emit_label(s, label_catch);

                if (s->token.val == '{') {
                    /* support optional-catch-binding feature */
                    emit_op(s, OP_drop);    /* pop the exception object */
                } else {
                    if (js_parse_expect(s, '('))
                        goto fail;
                    if (!(s->token.val == TOK_IDENT && !s->token.u.ident.is_reserved)) {
                        if (s->token.val == '[' || s->token.val == '{') {
                            /* XXX: TOK_LET is not completely correct */
                            if (js_parse_destructuring_element(s, TOK_LET, false, true, -1, true, false) < 0)
                                goto fail;
                        } else {
                            js_parse_error(s, "identifier expected");
                            goto fail;
                        }
                    } else {
                        name = JS_DupAtom(ctx, s->token.u.ident.atom);
                        if (next_token(s)
                        ||  js_define_var(s, name, TOK_CATCH) < 0) {
                            JS_FreeAtom(ctx, name);
                            goto fail;
                        }
                        /* store the exception value in the catch variable */
                        emit_op(s, OP_scope_put_var);
                        emit_u32(s, name);
                        emit_u16(s, s->cur_func->scope_level);
                    }
                    if (js_parse_expect(s, ')'))
                        goto fail;
                }
                /* XXX: should keep the address to nop it out if there is no finally block */
                emit_goto(s, OP_catch, label_catch2);

                push_scope(s);  /* catch block */
                push_break_entry(s->cur_func, &block_env, JS_ATOM_NULL,
                                 -1, -1, 1);
                block_env.label_finally = label_finally;

                if (js_parse_block(s))
                    goto fail;

                pop_break_entry(s->cur_func);
                pop_scope(s);  /* catch block */
                pop_scope(s);  /* catch variable */

                if (js_is_live_code(s)) {
                    /* drop the catch2 offset */
                    emit_op(s, OP_drop);
                    /* XXX: should keep the address to nop it out if there is no finally block */
                    /* must push dummy value to keep same stack size */
                    emit_op(s, OP_undefined);
                    emit_goto(s, OP_gosub, label_finally);
                    emit_op(s, OP_drop);
                    emit_goto(s, OP_goto, label_end);
                }
                /* catch exceptions thrown in the catch block to execute the
                 * finally clause and rethrow the exception */
                emit_label(s, label_catch2);
                /* catch value is at TOS, no need to push undefined */
                emit_goto(s, OP_gosub, label_finally);
                emit_op(s, OP_throw);

            } else if (s->token.val == TOK_FINALLY) {
                /* finally without catch : execute the finally clause
                 * and rethrow the exception */
                emit_label(s, label_catch);
                /* catch value is at TOS, no need to push undefined */
                emit_goto(s, OP_gosub, label_finally);
                emit_op(s, OP_throw);
            } else {
                js_parse_error(s, "expecting catch or finally");
                goto fail;
            }
            emit_label(s, label_finally);
            if (s->token.val == TOK_FINALLY) {
                int saved_eval_ret_idx = 0; /* avoid warning */

                if (next_token(s))
                    goto fail;
                /* on the stack: ret_value gosub_ret_value */
                push_break_entry(s->cur_func, &block_env, JS_ATOM_NULL,
                                 -1, -1, 2);

                if (s->cur_func->eval_ret_idx >= 0) {
                    /* 'finally' updates eval_ret only if not a normal
                       termination */
                    saved_eval_ret_idx =
                        add_var(s->ctx, s->cur_func, JS_ATOM__ret_);
                    if (saved_eval_ret_idx < 0)
                        goto fail;
                    emit_op(s, OP_get_loc);
                    emit_u16(s, s->cur_func->eval_ret_idx);
                    emit_op(s, OP_put_loc);
                    emit_u16(s, saved_eval_ret_idx);
                    set_eval_ret_undefined(s);
                }

                if (js_parse_block(s))
                    goto fail;

                if (s->cur_func->eval_ret_idx >= 0) {
                    emit_op(s, OP_get_loc);
                    emit_u16(s, saved_eval_ret_idx);
                    emit_op(s, OP_put_loc);
                    emit_u16(s, s->cur_func->eval_ret_idx);
                }
                pop_break_entry(s->cur_func);
            }
            emit_op(s, OP_ret);
            emit_label(s, label_end);
        }
        break;
    case ';':
        /* empty statement */
        if (next_token(s))
            goto fail;
        break;
    case TOK_WITH:
        if (s->cur_func->is_strict_mode) {
            js_parse_error(s, "invalid keyword: with");
            goto fail;
        } else {
            int with_idx;

            if (next_token(s))
                goto fail;

            if (js_parse_expr_paren(s))
                goto fail;

            push_scope(s);
            with_idx = define_var(s, s->cur_func, JS_ATOM__with_,
                                  JS_VAR_DEF_WITH);
            if (with_idx < 0)
                goto fail;
            emit_op(s, OP_to_object);
            emit_op(s, OP_put_loc);
            emit_u16(s, with_idx);

            set_eval_ret_undefined(s);
            if (js_parse_statement(s))
                goto fail;

            /* Popping scope drops lexical context for the with object variable */
            pop_scope(s);
        }
        break;
    case TOK_FUNCTION:
        /* ES6 Annex B.3.2 and B.3.3 semantics */
        if (!(decl_mask & DECL_MASK_FUNC))
            goto func_decl_error;
        if (!(decl_mask & DECL_MASK_OTHER) && peek_token(s, false) == '*')
            goto func_decl_error;
        goto parse_func_var;
    case TOK_IDENT:
        if (s->token.u.ident.is_reserved) {
            js_parse_error_reserved_identifier(s);
            goto fail;
        }
        /* Determine if `let` introduces a Declaration or an ExpressionStatement */
        switch (is_let(s, decl_mask)) {
        case true:
            tok = TOK_LET;
            goto haslet;
        case false:
            break;
        default:
            goto fail;
        }
        if (token_is_pseudo_keyword(s, JS_ATOM_using)) {
            int u = is_using(s, false);
            if (u < 0)
                goto fail;
            if (u) {
                JSFunctionDef *fd = s->cur_func;
                if (!(decl_mask & DECL_MASK_OTHER)) {
                    js_parse_error(s, "lexical declarations can't appear in single-statement context");
                    goto fail;
                }
                if (fd->is_eval &&
                    (fd->eval_type == JS_EVAL_TYPE_GLOBAL ||
                     fd->eval_type == JS_EVAL_TYPE_DIRECT ||
                     fd->eval_type == JS_EVAL_TYPE_INDIRECT) &&
                    fd->scope_level == fd->body_scope) {
                    js_parse_error(s, "using declaration is not allowed at the top level of a script");
                    goto fail;
                }
                if (next_token(s))
                    goto fail;
                if (js_parse_var(s, PF_IN_ACCEPTED, TOK_USING, /*export_flag*/false))
                    goto fail;
                if (js_parse_expect_semi(s))
                    goto fail;
                break;
            }
        }
        if (token_is_pseudo_keyword(s, JS_ATOM_async) &&
            peek_token(s, true) == TOK_FUNCTION) {
            if (!(decl_mask & DECL_MASK_OTHER)) {
            func_decl_error:
                js_parse_error(s, "function declarations can't appear in single-statement context");
                goto fail;
            }
        parse_func_var:
            if (js_parse_function_decl(s, JS_PARSE_FUNC_VAR,
                                       JS_FUNC_NORMAL, JS_ATOM_NULL,
                                       s->token.ptr,
                                       s->token.line_num,
                                       s->token.col_num))
                goto fail;
            break;
        }
        goto hasexpr;

    case TOK_CLASS:
        if (!(decl_mask & DECL_MASK_OTHER)) {
            js_parse_error(s, "class declarations can't appear in single-statement context");
            goto fail;
        }
        if (js_parse_class(s, false, JS_PARSE_EXPORT_NONE))
            return -1;
        break;

    case TOK_DEBUGGER:
        /* currently no debugger, so just skip the keyword */
        if (next_token(s))
            goto fail;
        if (js_parse_expect_semi(s))
            goto fail;
        break;

    case TOK_ENUM:
    case TOK_EXPORT:
    case TOK_EXTENDS:
        js_unsupported_keyword(s, s->token.u.ident.atom);
        goto fail;

    default:
    hasexpr:
        emit_source_loc(s);
        if (js_parse_expr(s))
            goto fail;
        if (s->cur_func->eval_ret_idx >= 0) {
            /* store the expression value so that it can be returned
               by eval() */
            emit_op(s, OP_put_loc);
            emit_u16(s, s->cur_func->eval_ret_idx);
        } else {
            emit_op(s, OP_drop); /* drop the result */
        }
        if (js_parse_expect_semi(s))
            goto fail;
        break;
    }
done:
    JS_FreeAtom(ctx, label_name);
    return 0;
fail:
    JS_FreeAtom(ctx, label_name);
    return -1;
}

#endif // QJS_DISABLE_PARSER

/* 'name' is freed */
