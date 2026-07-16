/* Engine domain source: compiler/parser_compiler.inc -> lexer.
 * Ownership: compiler subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

/* JS parser */

enum {
    TOK_NUMBER = -128,
    TOK_STRING,
    TOK_TEMPLATE,
    TOK_IDENT,
    TOK_REGEXP,
    /* warning: order matters (see js_parse_assign_expr) */
    TOK_MUL_ASSIGN,
    TOK_DIV_ASSIGN,
    TOK_MOD_ASSIGN,
    TOK_PLUS_ASSIGN,
    TOK_MINUS_ASSIGN,
    TOK_SHL_ASSIGN,
    TOK_SAR_ASSIGN,
    TOK_SHR_ASSIGN,
    TOK_AND_ASSIGN,
    TOK_XOR_ASSIGN,
    TOK_OR_ASSIGN,
    TOK_POW_ASSIGN,
    TOK_LAND_ASSIGN,
    TOK_LOR_ASSIGN,
    TOK_DOUBLE_QUESTION_MARK_ASSIGN,
    TOK_DEC,
    TOK_INC,
    TOK_SHL,
    TOK_SAR,
    TOK_SHR,
    TOK_LT,
    TOK_LTE,
    TOK_GT,
    TOK_GTE,
    TOK_EQ,
    TOK_STRICT_EQ,
    TOK_NEQ,
    TOK_STRICT_NEQ,
    TOK_LAND,
    TOK_LOR,
    TOK_POW,
    TOK_ARROW,
    TOK_ELLIPSIS,
    TOK_DOUBLE_QUESTION_MARK,
    TOK_QUESTION_MARK_DOT,
    TOK_ERROR,
    TOK_PRIVATE_NAME,
    TOK_EOF,
    /* keywords: WARNING: same order as atoms */
    TOK_NULL, /* must be first */
    TOK_FALSE,
    TOK_TRUE,
    TOK_IF,
    TOK_ELSE,
    TOK_RETURN,
    TOK_VAR,
    TOK_THIS,
    TOK_DELETE,
    TOK_VOID,
    TOK_TYPEOF,
    TOK_NEW,
    TOK_IN,
    TOK_INSTANCEOF,
    TOK_DO,
    TOK_WHILE,
    TOK_FOR,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_SWITCH,
    TOK_CASE,
    TOK_DEFAULT,
    TOK_THROW,
    TOK_TRY,
    TOK_CATCH,
    TOK_FINALLY,
    TOK_FUNCTION,
    TOK_DEBUGGER,
    TOK_WITH,
    /* FutureReservedWord */
    TOK_CLASS,
    TOK_CONST,
    TOK_ENUM,
    TOK_EXPORT,
    TOK_EXTENDS,
    TOK_IMPORT,
    TOK_SUPER,
    TOK_USING,
    /* FutureReservedWords when parsing strict mode code */
    TOK_IMPLEMENTS,
    TOK_INTERFACE,
    TOK_LET,
    TOK_PACKAGE,
    TOK_PRIVATE,
    TOK_PROTECTED,
    TOK_PUBLIC,
    TOK_STATIC,
    TOK_YIELD,
    TOK_AWAIT, /* must be last */
    TOK_OF,     /* only used for js_parse_skip_parens_token() */
};

#define TOK_FIRST_KEYWORD   TOK_NULL
#define TOK_LAST_KEYWORD    TOK_AWAIT

/* unicode code points */
#define CP_NBSP 0x00a0
#define CP_BOM  0xfeff

#define CP_LS   0x2028
#define CP_PS   0x2029

typedef struct BlockEnv {
    struct BlockEnv *prev;
    JSAtom label_name; /* JS_ATOM_NULL if none */
    int label_break; /* -1 if none */
    int label_cont; /* -1 if none */
    int drop_count; /* number of stack elements to drop */
    int label_finally; /* -1 if none */
    int scope_level;
    uint8_t has_iterator : 1;
    uint8_t is_regular_stmt : 1; // i.e. not a loop statement
    uint8_t has_using : 1; /* scope has using declarations needing disposal */
    int using_scope_level; /* scope level for OP_dispose_scope (-1 if none) */
} BlockEnv;

typedef struct JSGlobalVar {
    int cpool_idx; /* if >= 0, index in the constant pool for hoisted
                      function defintion*/
    uint8_t force_init : 1; /* force initialization to undefined */
    uint8_t is_lexical : 1; /* global let/const definition */
    uint8_t is_const   : 1; /* const definition */
    int scope_level;    /* scope of definition */
    JSAtom var_name;  /* variable name */
} JSGlobalVar;

typedef struct RelocEntry {
    struct RelocEntry *next;
    uint32_t addr; /* address to patch */
    int size;   /* address size: 1, 2 or 4 bytes */
} RelocEntry;

typedef struct JumpSlot {
    int op;
    int size;
    int pos;
    int label;
} JumpSlot;

typedef struct LabelSlot {
    int ref_count;
    int pos;    /* phase 1 address, -1 means not resolved yet */
    int pos2;   /* phase 2 address, -1 means not resolved yet */
    int addr;   /* phase 3 address, -1 means not resolved yet */
    RelocEntry *first_reloc;
} LabelSlot;

typedef struct SourceLocSlot {
    uint32_t pc;
    int line_num;
    int col_num;
} SourceLocSlot;

typedef enum JSParseFunctionEnum {
    JS_PARSE_FUNC_STATEMENT,
    JS_PARSE_FUNC_VAR,
    JS_PARSE_FUNC_EXPR,
    JS_PARSE_FUNC_ARROW,
    JS_PARSE_FUNC_GETTER,
    JS_PARSE_FUNC_SETTER,
    JS_PARSE_FUNC_METHOD,
    JS_PARSE_FUNC_CLASS_STATIC_INIT,
    JS_PARSE_FUNC_CLASS_CONSTRUCTOR,
    JS_PARSE_FUNC_DERIVED_CLASS_CONSTRUCTOR,
} JSParseFunctionEnum;

typedef enum JSParseExportEnum {
    JS_PARSE_EXPORT_NONE,
    JS_PARSE_EXPORT_NAMED,
    JS_PARSE_EXPORT_DEFAULT,
} JSParseExportEnum;

typedef struct JSFunctionDef {
    JSContext *ctx;
    struct JSFunctionDef *parent;
    int parent_cpool_idx; /* index in the constant pool of the parent
                             or -1 if none */
    int parent_scope_level; /* scope level in parent at point of definition */
    struct list_head child_list; /* list of JSFunctionDef.link */
    struct list_head link;

    int eval_type; /* only valid if is_eval = true */

    /* Pack all boolean flags together as 1-bit fields to reduce struct size
    while avoiding padding and compiler deoptimization. */
    bool is_eval : 1; /* true if eval code */
    bool is_global_var : 1; /* true if variables are not defined locally:
                           eval global, eval module or non strict eval */
    bool is_func_expr : 1; /* true if function expression */
    bool has_home_object : 1; /* true if the home object is available */
    bool has_prototype : 1; /* true if a prototype field is necessary */
    bool has_simple_parameter_list : 1;
    bool has_parameter_expressions : 1; /* if true, an argument scope is created */
    bool has_use_strict : 1; /* to reject directive in special cases */
    bool has_eval_call : 1; /* true if the function contains a call to eval() */
    bool has_arguments_binding : 1; /* true if the 'arguments' binding is
                                   available in the function */
    bool has_this_binding : 1; /* true if the 'this' and new.target binding are
                              available in the function */
    bool new_target_allowed : 1; /* true if the 'new.target' does not
                                throw a syntax error */
    bool super_call_allowed : 1; /* true if super() is allowed */
    bool super_allowed : 1; /* true if super. or super[] is allowed */
    bool arguments_allowed : 1; /* true if the 'arguments' identifier is allowed */
    bool is_derived_class_constructor : 1;
    bool in_function_body : 1;
    bool backtrace_barrier : 1;
    bool need_home_object : 1;
    bool use_short_opcodes : 1; /* true if short opcodes are used in byte_code */
    bool has_await : 1; /* true if await is used (used in module eval) */

    JSFunctionKindEnum func_kind : 8;
    JSParseFunctionEnum func_type : 7;
    uint8_t is_strict_mode : 1;
    JSAtom func_name; /* JS_ATOM_NULL if no name */

    JSVarDef *vars;
    uint32_t *vars_htab; // indexes into vars[]
    int var_size; /* allocated size for vars[] */
    int var_count;
    JSVarDef *args;
    int arg_size; /* allocated size for args[] */
    int arg_count; /* number of arguments */
    int defined_arg_count;
    int var_ref_count; /* number of local/arg variable references */
    int var_object_idx; /* -1 if none */
    int arg_var_object_idx; /* -1 if none (var object for the argument scope) */
    int arguments_var_idx; /* -1 if none */
    int arguments_arg_idx; /* argument variable definition in argument scope,
                              -1 if none */
    int func_var_idx; /* variable containing the current function (-1
                         if none, only used if is_func_expr is true) */
    int eval_ret_idx; /* variable containing the return value of the eval, -1 if none */
    int this_var_idx; /* variable containg the 'this' value, -1 if none */
    int new_target_var_idx; /* variable containg the 'new.target' value, -1 if none */
    int this_active_func_var_idx; /* variable containg the 'this.active_func' value, -1 if none */
    int home_object_var_idx;

    int scope_level;    /* index into fd->scopes if the current lexical scope */
    int scope_first;    /* index into vd->vars of first lexically scoped variable */
    int scope_size;     /* allocated size of fd->scopes array */
    int scope_count;    /* number of entries used in the fd->scopes array */
    JSVarScope *scopes;
    JSVarScope def_scope_array[4];
    int body_scope; /* scope of the body of the function or eval */

    int global_var_count;
    int global_var_size;
    JSGlobalVar *global_vars;

    DynBuf byte_code;
    int last_opcode_pos; /* -1 if no last opcode */

    LabelSlot *label_slots;
    int label_size; /* allocated size for label_slots[] */
    int label_count;
    BlockEnv *top_break; /* break/continue label stack */

    /* constant pool (strings, functions, numbers) */
    JSValue *cpool;
    int cpool_count;
    int cpool_size;

    /* list of variables in the closure */
    int closure_var_count;
    int closure_var_size;
    JSClosureVar *closure_var;

    JumpSlot *jump_slots;
    int jump_size;
    int jump_count;

    SourceLocSlot *source_loc_slots;
    int source_loc_size;
    int source_loc_count;
    int line_number_last;
    int line_number_last_pc;
    int col_number_last;

    /* pc2line table */
    JSAtom filename;
    int line_num;
    int col_num;
    DynBuf pc2line;

    char *source;  /* raw source, utf-8 encoded */
    int source_len;

    JSModuleDef *module; /* != NULL when parsing a module */
} JSFunctionDef;

typedef struct JSToken {
    int val;
    int line_num;   /* line number of token start */
    int col_num;    /* column number of token start */
    const uint8_t *ptr;
    union {
        struct {
            JSValue str;
            int sep;
        } str;
        struct {
            JSValue val;
        } num;
        struct {
            JSAtom atom;
            bool has_escape;
            bool is_reserved;
        } ident;
        struct {
            JSValue body;
            JSValue flags;
        } regexp;
    } u;
} JSToken;

typedef struct JSParseState {
    JSContext *ctx;
    int last_line_num;  /* line number of last token */
    int last_col_num;   /* column number of last token */
    int line_num;       /* line number of current offset */
    int col_num;        /* column number of current offset */
    const char *filename;
    JSToken token;
    bool got_lf; /* true if got line feed before the current token */
    const uint8_t *last_ptr;
    const uint8_t *buf_start;
    const uint8_t *buf_ptr;
    const uint8_t *buf_end;
    const uint8_t *eol;  // most recently seen end-of-line character
    const uint8_t *mark; // first token character, invariant: eol < mark

    /* current function code */
    JSFunctionDef *cur_func;
    bool is_module; /* parsing a module */
    bool allow_html_comments;
} JSParseState;

typedef struct JSOpCode {
#ifdef ENABLE_DUMPS // JS_DUMP_BYTECODE_*
    const char *name;
#endif
    uint8_t size; /* in bytes */
    /* the opcodes remove n_pop items from the top of the stack, then
       pushes n_push items */
    uint8_t n_pop;
    uint8_t n_push;
    uint8_t fmt;
} JSOpCode;

static const JSOpCode opcode_info[OP_COUNT + (OP_TEMP_END - OP_TEMP_START)] = {
#define FMT(f)
#ifdef ENABLE_DUMPS // JS_DUMP_BYTECODE_*
#define DEF(id, size, n_pop, n_push, f) { #id, size, n_pop, n_push, OP_FMT_ ## f },
#else
#define DEF(id, size, n_pop, n_push, f) { size, n_pop, n_push, OP_FMT_ ## f },
#endif
#include "bytecode_opcodes.h"
#undef DEF
#undef FMT
};

/* After the final compilation pass, short opcodes are used. Their
   opcodes overlap with the temporary opcodes which cannot appear in
   the final bytecode. Their description is after the temporary
   opcodes in opcode_info[]. */
#define short_opcode_info(op)           \
    opcode_info[(op) >= OP_TEMP_START ? \
                (op) + (OP_TEMP_END - OP_TEMP_START) : (op)]

static void json_free_token(JSParseState *s, JSToken *token) {
    // Only free actual allocated values
    switch(token->val) {
    case TOK_NUMBER:
        JS_FreeValue(s->ctx, token->u.num.val);
        break;
    case TOK_STRING:
        JS_FreeValue(s->ctx, token->u.str.str);
        break;
    case TOK_IDENT:
        JS_FreeAtom(s->ctx, token->u.ident.atom);
        break;
    }
}

static void free_token(JSParseState *s, JSToken *token)
{
    switch(token->val) {
    case TOK_NUMBER:
        JS_FreeValue(s->ctx, token->u.num.val);
        break;
    case TOK_STRING:
    case TOK_TEMPLATE:
        JS_FreeValue(s->ctx, token->u.str.str);
        break;
    case TOK_REGEXP:
        JS_FreeValue(s->ctx, token->u.regexp.body);
        JS_FreeValue(s->ctx, token->u.regexp.flags);
        break;
    case TOK_IDENT:
    case TOK_PRIVATE_NAME:
        JS_FreeAtom(s->ctx, token->u.ident.atom);
        break;
    default:
        if (token->val >= TOK_FIRST_KEYWORD &&
            token->val <= TOK_LAST_KEYWORD) {
            JS_FreeAtom(s->ctx, token->u.ident.atom);
        }
        break;
    }
}

static void __attribute((unused)) dump_token(JSParseState *s,
                                             const JSToken *token)
{
    printf("%d:%d ", token->line_num, token->col_num);
    switch(token->val) {
    case TOK_NUMBER:
        {
            double d;
            JS_ToFloat64(s->ctx, &d, token->u.num.val);  /* no exception possible */
            printf("number: %.14g\n", d);
        }
        break;
    case TOK_IDENT:
    dump_atom:
        {
            char buf[ATOM_GET_STR_BUF_SIZE];
            printf("ident: '%s'\n",
                   JS_AtomGetStr(s->ctx, buf, sizeof(buf), token->u.ident.atom));
        }
        break;
    case TOK_STRING:
        {
            const char *str;
            /* XXX: quote the string */
            str = JS_ToCString(s->ctx, token->u.str.str);
            printf("string: '%s'\n", str);
            JS_FreeCString(s->ctx, str);
        }
        break;
    case TOK_TEMPLATE:
        {
            const char *str;
            str = JS_ToCString(s->ctx, token->u.str.str);
            printf("template: `%s`\n", str);
            JS_FreeCString(s->ctx, str);
        }
        break;
    case TOK_REGEXP:
        {
            const char *str, *str2;
            str = JS_ToCString(s->ctx, token->u.regexp.body);
            str2 = JS_ToCString(s->ctx, token->u.regexp.flags);
            printf("regexp: '%s' '%s'\n", str, str2);
            JS_FreeCString(s->ctx, str);
            JS_FreeCString(s->ctx, str2);
        }
        break;
    case TOK_EOF:
        printf("eof\n");
        break;
    default:
        if (s->token.val >= TOK_NULL && s->token.val <= TOK_LAST_KEYWORD) {
            goto dump_atom;
        } else if (s->token.val >= 256) {
            printf("token: %d\n", token->val);
        } else {
            printf("token: '%c'\n", token->val);
        }
        break;
    }
}

int JS_PRINTF_FORMAT_ATTR(2, 3) js_parse_error(JSParseState *s, JS_PRINTF_FORMAT const char *fmt, ...)
{
    JSContext *ctx = s->ctx;
    va_list ap;
    int backtrace_flags;

    va_start(ap, fmt);
    JS_ThrowError2(ctx, JS_SYNTAX_ERROR, false, fmt, ap);
    va_end(ap);
    backtrace_flags = 0;
    if (s->cur_func && s->cur_func->backtrace_barrier)
        backtrace_flags = JS_BACKTRACE_FLAG_SINGLE_LEVEL;
    build_backtrace(ctx, ctx->rt->current_exception, JS_UNDEFINED, s->filename,
                    s->line_num, s->col_num, backtrace_flags);
    return -1;
}

#ifndef QJS_DISABLE_PARSER

static __exception int next_token(JSParseState *s);

static int js_parse_expect(JSParseState *s, int tok)
{
    char buf[ATOM_GET_STR_BUF_SIZE];

    if (s->token.val == tok)
        return next_token(s);

    switch(s->token.val) {
    case TOK_EOF:
        return js_parse_error(s, "Unexpected end of input");
    case TOK_NUMBER:
        return js_parse_error(s, "Unexpected number");
    case TOK_STRING:
        return js_parse_error(s, "Unexpected string");
    case TOK_TEMPLATE:
        return js_parse_error(s, "Unexpected string template");
    case TOK_REGEXP:
        return js_parse_error(s, "Unexpected regexp");
    case TOK_IDENT:
        return js_parse_error(s, "Unexpected identifier '%s'",
                              JS_AtomGetStr(s->ctx, buf, sizeof(buf),
                                            s->token.u.ident.atom));
    case TOK_ERROR:
        return js_parse_error(s, "Invalid or unexpected token");
    default:
        return js_parse_error(s, "Unexpected token '%.*s'",
                              (int)(s->buf_ptr - s->token.ptr),
                              (const char *)s->token.ptr);
    }
}

static int js_parse_expect_semi(JSParseState *s)
{
    if (s->token.val != ';') {
        /* automatic insertion of ';' */
        if (s->token.val == TOK_EOF || s->token.val == '}' || s->got_lf) {
            return 0;
        }
        return js_parse_error(s, "expecting '%c'", ';');
    }
    return next_token(s);
}

static int js_parse_error_reserved_identifier(JSParseState *s)
{
    char buf1[ATOM_GET_STR_BUF_SIZE];
    return js_parse_error(s, "'%s' is a reserved identifier",
                          JS_AtomGetStr(s->ctx, buf1, sizeof(buf1),
                                        s->token.u.ident.atom));
}

static __exception int js_parse_template_part(JSParseState *s,
                                              const uint8_t *p)
{
    const uint8_t *p_next;
    uint32_t c;
    StringBuffer b_s, *b = &b_s;
    JSValue str;

    /* p points to the first byte of the template part */
    if (string_buffer_init(s->ctx, b, 32))
        goto fail;
    for(;;) {
        if (p >= s->buf_end)
            goto unexpected_eof;
        c = *p++;
        if (c == '`') {
            /* template end part */
            break;
        }
        if (c == '$' && *p == '{') {
            /* template start or middle part */
            p++;
            break;
        }
        if (c == '\\') {
            if (string_buffer_putc8(b, c))
                goto fail;
            if (p >= s->buf_end)
                goto unexpected_eof;
            c = *p++;
        }
        /* newline sequences are normalized as single '\n' bytes */
        if (c == '\r') {
            if (*p == '\n')
                p++;
            c = '\n';
        }
        if (c == '\n') {
            s->line_num++;
            s->eol = &p[-1];
            s->mark = p;
        } else if (c >= 0x80) {
            c = utf8_decode(p - 1, &p_next);
            if (p_next == p) {
                js_parse_error(s, "invalid UTF-8 sequence");
                goto fail;
            }
            p = p_next;
        }
        if (string_buffer_putc(b, c))
            goto fail;
    }
    str = string_buffer_end(b);
    if (JS_IsException(str))
        return -1;
    s->token.val = TOK_TEMPLATE;
    s->token.u.str.sep = c;
    s->token.u.str.str = str;
    s->buf_ptr = p;
    return 0;

 unexpected_eof:
    js_parse_error(s, "unexpected end of string");
 fail:
    string_buffer_free(b);
    return -1;
}

static __exception int js_parse_string(JSParseState *s, int sep,
                                       bool do_throw, const uint8_t *p,
                                       JSToken *token, const uint8_t **pp)
{
    const uint8_t *p_next;
    int ret;
    uint32_t c;
    StringBuffer b_s, *b = &b_s;
    JSValue str;

    /* string */
    if (string_buffer_init(s->ctx, b, 32))
        goto fail;
    for(;;) {
        if (p >= s->buf_end)
            goto invalid_char;
        c = *p;
        if (c < 0x20) {
            if (sep == '`') {
                if (c == '\r') {
                    if (p[1] == '\n')
                        p++;
                    c = '\n';
                }
                /* do not update s->line_num */
            } else if (c == '\n' || c == '\r')
                goto invalid_char;
        }
        p++;
        if (c == sep)
            break;
        if (c == '$' && *p == '{' && sep == '`') {
            /* template start or middle part */
            p++;
            break;
        }
        if (c == '\\') {
            c = *p;
            switch(c) {
            case '\0':
                if (p >= s->buf_end) {
                    if (sep != '`')
                        goto invalid_char;
                    if (do_throw)
                        js_parse_error(s, "Unexpected end of input");
                    goto fail;
                }
                p++;
                break;
            case '\'':
            case '\"':
            case '\\':
                p++;
                break;
            case '\r':  /* accept DOS and MAC newline sequences */
                if (p[1] == '\n') {
                    p++;
                }
                /* fall thru */
            case '\n':
                /* ignore escaped newline sequence */
                p++;
                if (sep != '`') {
                    s->line_num++;
                    s->eol = &p[-1];
                    s->mark = p;
                }
                continue;
            default:
                if (c == '0' && !(p[1] >= '0' && p[1] <= '9')) {
                    /* accept isolated \0 */
                    p++;
                    c = '\0';
                } else
                if ((c >= '0' && c <= '9')
                &&  (s->cur_func->is_strict_mode || sep == '`')) {
                    if (do_throw) {
                        js_parse_error(s, "%s are not allowed in %s",
                                       (c >= '8') ? "\\8 and \\9" : "Octal escape sequences",
                                       (sep == '`') ? "template strings" : "strict mode");
                    }
                    goto fail;
                } else if (c >= 0x80) {
                    c = utf8_decode(p, &p_next);
                    if (p_next == p + 1) {
                        goto invalid_utf8;
                    }
                    p = p_next;
                    /* LS or PS are skipped */
                    if (c == CP_LS || c == CP_PS)
                        continue;
                } else {
                    ret = lre_parse_escape(&p, true);
                    if (ret == -1) {
                        if (do_throw) {
                            js_parse_error(s, "Invalid %s escape sequence",
                                           c == 'u' ? "Unicode" : "hexadecimal");
                        }
                        goto fail;
                    } else if (ret < 0) {
                        /* ignore the '\' (could output a warning) */
                        p++;
                    } else {
                        c = ret;
                    }
                }
                break;
            }
        } else if (c >= 0x80) {
            c = utf8_decode(p - 1, &p_next);
            if (p_next == p)
                goto invalid_utf8;
            p = p_next;
        }
        if (string_buffer_putc(b, c))
            goto fail;
    }
    str = string_buffer_end(b);
    if (JS_IsException(str))
        return -1;
    token->val = TOK_STRING;
    token->u.str.sep = c;
    token->u.str.str = str;
    *pp = p;
    return 0;

 invalid_utf8:
    if (do_throw)
        js_parse_error(s, "invalid UTF-8 sequence");
    goto fail;
 invalid_char:
    if (do_throw)
        js_parse_error(s, "unexpected end of string");
 fail:
    string_buffer_free(b);
    return -1;
}

static inline bool token_is_pseudo_keyword(JSParseState *s, JSAtom atom) {
    return s->token.val == TOK_IDENT && s->token.u.ident.atom == atom &&
        !s->token.u.ident.has_escape;
}

static __exception int js_parse_regexp(JSParseState *s)
{
    const uint8_t *p, *p_next;
    bool in_class;
    StringBuffer b_s, *b = &b_s;
    StringBuffer b2_s, *b2 = &b2_s;
    uint32_t c;
    JSValue body_str, flags_str;

    p = s->buf_ptr;
    p++;
    in_class = false;
    if (string_buffer_init(s->ctx, b, 32))
        return -1;
    if (string_buffer_init(s->ctx, b2, 1))
        goto fail;
    for(;;) {
        if (p >= s->buf_end) {
        eof_error:
            js_parse_error(s, "unexpected end of regexp");
            goto fail;
        }
        c = *p++;
        if (c == '\n' || c == '\r') {
            goto eol_error;
        } else if (c == '/') {
            if (!in_class)
                break;
        } else if (c == '[') {
            in_class = true;
        } else if (c == ']') {
            /* XXX: incorrect as the first character in a class */
            in_class = false;
        } else if (c == '\\') {
            if (string_buffer_putc8(b, c))
                goto fail;
            c = *p++;
            if (c == '\n' || c == '\r')
                goto eol_error;
            else if (c == '\0' && p >= s->buf_end)
                goto eof_error;
            else if (c >= 0x80) {
                c = utf8_decode(p - 1, &p_next);
                if (p_next == p) {
                    goto invalid_utf8;
                }
                p = p_next;
                if (c == CP_LS || c == CP_PS)
                    goto eol_error;
            }
        } else if (c >= 0x80) {
            c = utf8_decode(p - 1, &p_next);
            if (p_next == p) {
            invalid_utf8:
                js_parse_error(s, "invalid UTF-8 sequence");
                goto fail;
            }
            p = p_next;
            /* LS or PS are considered as line terminator */
            if (c == CP_LS || c == CP_PS) {
            eol_error:
                js_parse_error(s, "unexpected line terminator in regexp");
                goto fail;
            }
        }
        if (string_buffer_putc(b, c))
            goto fail;
    }

    /* flags */
    for(;;) {
        c = utf8_decode(p, &p_next);
        /* no need to test for invalid UTF-8, 0xFFFD is not ident_next */
        if (!lre_js_is_ident_next(c))
            break;
        if (string_buffer_putc(b2, c))
            goto fail;
        p = p_next;
    }

    body_str = string_buffer_end(b);
    flags_str = string_buffer_end(b2);
    if (JS_IsException(body_str) ||
        JS_IsException(flags_str)) {
        JS_FreeValue(s->ctx, body_str);
        JS_FreeValue(s->ctx, flags_str);
        return -1;
    }
    s->token.val = TOK_REGEXP;
    s->token.u.regexp.body = body_str;
    s->token.u.regexp.flags = flags_str;
    s->buf_ptr = p;
    return 0;
 fail:
    string_buffer_free(b);
    string_buffer_free(b2);
    return -1;
}

#endif // QJS_DISABLE_PARSER

static __exception int ident_realloc(JSContext *ctx, char **pbuf, size_t *psize,
                                     char *static_buf)
{
    char *buf, *new_buf;
    size_t size, new_size;

    buf = *pbuf;
    size = *psize;
    if (size >= (SIZE_MAX / 3) * 2)
        new_size = SIZE_MAX;
    else
        new_size = size + (size >> 1);
    if (buf == static_buf) {
        new_buf = js_malloc(ctx, new_size);
        if (!new_buf)
            return -1;
        memcpy(new_buf, buf, size);
    } else {
        new_buf = js_realloc(ctx, buf, new_size);
        if (!new_buf)
            return -1;
    }
    *pbuf = new_buf;
    *psize = new_size;
    return 0;
}

#ifndef QJS_DISABLE_PARSER

/* convert a TOK_IDENT to a keyword when needed */
static void update_token_ident(JSParseState *s)
{
    /* `using` is contextually reserved, not a true keyword. Leave it as
       TOK_IDENT so it can be used as a regular identifier in expressions.
       Using declarations are detected explicitly at statement and
       for-loop head parsing via token_is_pseudo_keyword. */
    if (s->token.u.ident.atom == JS_ATOM_using)
        return;
    if (s->token.u.ident.atom <= JS_ATOM_LAST_KEYWORD ||
        (s->token.u.ident.atom <= JS_ATOM_LAST_STRICT_KEYWORD &&
         s->cur_func->is_strict_mode) ||
        (s->token.u.ident.atom == JS_ATOM_yield &&
         ((s->cur_func->func_kind & JS_FUNC_GENERATOR) ||
          (s->cur_func->func_type == JS_PARSE_FUNC_ARROW &&
           !s->cur_func->in_function_body && s->cur_func->parent &&
           (s->cur_func->parent->func_kind & JS_FUNC_GENERATOR)))) ||
        (s->token.u.ident.atom == JS_ATOM_await &&
         (s->is_module ||
          (s->cur_func->func_kind & JS_FUNC_ASYNC) ||
          s->cur_func->func_type == JS_PARSE_FUNC_CLASS_STATIC_INIT ||
          (s->cur_func->func_type == JS_PARSE_FUNC_ARROW &&
           !s->cur_func->in_function_body && s->cur_func->parent &&
           ((s->cur_func->parent->func_kind & JS_FUNC_ASYNC) ||
            s->cur_func->parent->func_type == JS_PARSE_FUNC_CLASS_STATIC_INIT))))) {
        if (s->token.u.ident.has_escape) {
            s->token.u.ident.is_reserved = true;
            s->token.val = TOK_IDENT;
        } else {
            /* The keywords atoms are pre allocated */
            s->token.val = s->token.u.ident.atom - 1 + TOK_FIRST_KEYWORD;
        }
    }
}

/* if the current token is an identifier or keyword, reparse it
   according to the current function type */
static void reparse_ident_token(JSParseState *s)
{
    if (s->token.val == TOK_IDENT ||
        (s->token.val >= TOK_FIRST_KEYWORD &&
         s->token.val <= TOK_LAST_KEYWORD)) {
        s->token.val = TOK_IDENT;
        s->token.u.ident.is_reserved = false;
        update_token_ident(s);
    }
}

/* 'c' is the first character. Return JS_ATOM_NULL in case of error */
static JSAtom parse_ident(JSParseState *s, const uint8_t **pp,
                          bool *pident_has_escape, int c, bool is_private)
{
    const uint8_t *p, *p_next;
    char ident_buf[128], *buf;
    size_t ident_size, ident_pos;
    JSAtom atom = JS_ATOM_NULL;

    p = *pp;
    buf = ident_buf;
    ident_size = sizeof(ident_buf);
    ident_pos = 0;
    if (is_private)
        buf[ident_pos++] = '#';
    for(;;) {
        if (c < 0x80) {
            buf[ident_pos++] = c;
        } else {
            ident_pos += utf8_encode((uint8_t*)buf + ident_pos, c);
        }
        c = *p;
        p_next = p + 1;
        if (c == '\\' && *p_next == 'u') {
            c = lre_parse_escape(&p_next, true);
            *pident_has_escape = true;
        } else if (c >= 0x80) {
            c = utf8_decode(p, &p_next);
            /* no need to test for invalid UTF-8, 0xFFFD is not ident_next */
        }
        if (!lre_js_is_ident_next(c))
            break;
        p = p_next;
        if (unlikely(ident_pos >= ident_size - UTF8_CHAR_LEN_MAX)) {
            if (ident_realloc(s->ctx, &buf, &ident_size, ident_buf))
                goto done;
        }
    }
    /* buf is pure ASCII or UTF-8 encoded */
    atom = JS_NewAtomLen(s->ctx, buf, ident_pos);
 done:
    if (unlikely(buf != ident_buf))
        js_free(s->ctx, buf);
    *pp = p;
    return atom;
}


static __exception int next_token(JSParseState *s)
{
    const uint8_t *p, *p_next;
    int c;
    bool ident_has_escape;
    JSAtom atom;

    if (js_check_stack_overflow(s->ctx->rt, 1000)) {
        JS_ThrowStackOverflow(s->ctx);
        return -1;
    }

    free_token(s, &s->token);

    p = s->last_ptr = s->buf_ptr;
    s->got_lf = false;
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
    case '`':
        if (js_parse_template_part(s, p + 1))
            goto fail;
        p = s->buf_ptr;
        break;
    case '\'':
    case '\"':
        if (js_parse_string(s, c, true, p + 1, &s->token, &p))
            goto fail;
        break;
    case '\r':  /* accept DOS and MAC newline sequences */
        if (p[1] == '\n') {
            p++;
        }
        /* fall thru */
    case '\n':
        p++;
    line_terminator:
        s->eol = &p[-1];
        s->mark = p;
        s->got_lf = true;
        s->line_num++;
        goto redo;
    case '\f':
    case '\v':
    case ' ':
    case '\t':
        s->mark = ++p;
        goto redo;
    case '/':
        if (p[1] == '*') {
            /* comment */
            p += 2;
            for(;;) {
                if (*p == '\0' && p >= s->buf_end) {
                    js_parse_error(s, "unexpected end of comment");
                    goto fail;
                }
                if (p[0] == '*' && p[1] == '/') {
                    p += 2;
                    break;
                }
                if (*p == '\n') {
                    s->line_num++;
                    s->got_lf = true; /* considered as LF for ASI */
                    s->eol = p++;
                    s->mark = p;
                } else if (*p == '\r') {
                    s->got_lf = true; /* considered as LF for ASI */
                    p++;
                } else if (*p >= 0x80) {
                    c = utf8_decode(p, &p);
                    /* ignore invalid UTF-8 in comments */
                    if (c == CP_LS || c == CP_PS) {
                        s->got_lf = true; /* considered as LF for ASI */
                    }
                } else {
                    p++;
                }
            }
            s->mark = p;
            goto redo;
        } else if (p[1] == '/') {
            /* line comment */
            p += 2;
        skip_line_comment:
            for(;;) {
                if (*p == '\0' && p >= s->buf_end)
                    break;
                if (*p == '\r' || *p == '\n')
                    break;
                if (*p >= 0x80) {
                    c = utf8_decode(p, &p);
                    /* ignore invalid UTF-8 in comments */
                    /* LS or PS are considered as line terminator */
                    if (c == CP_LS || c == CP_PS) {
                        break;
                    }
                } else {
                    p++;
                }
            }
            s->mark = p;
            goto redo;
        } else if (p[1] == '=') {
            p += 2;
            s->token.val = TOK_DIV_ASSIGN;
        } else {
            p++;
            s->token.val = c;
        }
        break;
    case '\\':
        if (p[1] == 'u') {
            const uint8_t *p1 = p + 1;
            int c1 = lre_parse_escape(&p1, true);
            if (c1 >= 0 && lre_js_is_ident_first(c1)) {
                c = c1;
                p = p1;
                ident_has_escape = true;
                goto has_ident;
            } else {
                /* XXX: syntax error? */
            }
        }
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
        /* identifier */
        s->mark = p;
        p++;
        ident_has_escape = false;
    has_ident:
        atom = parse_ident(s, &p, &ident_has_escape, c, false);
        if (atom == JS_ATOM_NULL)
            goto fail;
        s->token.u.ident.atom = atom;
        s->token.u.ident.has_escape = ident_has_escape;
        s->token.u.ident.is_reserved = false;
        s->token.val = TOK_IDENT;
        update_token_ident(s);
        break;
    case '#':
        /* private name */
        {
            p++;
            c = *p;
            p_next = p + 1;
            if (c == '\\' && *p_next == 'u') {
                c = lre_parse_escape(&p_next, true);
            } else if (c >= 0x80) {
                c = utf8_decode(p, &p_next);
                if (p_next == p + 1)
                    goto invalid_utf8;
            }
            if (!lre_js_is_ident_first(c)) {
                js_parse_error(s, "invalid first character of private name");
                goto fail;
            }
            p = p_next;
            ident_has_escape = false; /* not used */
            atom = parse_ident(s, &p, &ident_has_escape, c, true);
            if (atom == JS_ATOM_NULL)
                goto fail;
            s->token.u.ident.atom = atom;
            s->token.val = TOK_PRIVATE_NAME;
        }
        break;
    case '.':
        if (p[1] == '.' && p[2] == '.') {
            p += 3;
            s->token.val = TOK_ELLIPSIS;
            break;
        }
        if (p[1] >= '0' && p[1] <= '9') {
            goto parse_number;
        } else {
            goto def_token;
        }
        break;
    case '0':
        /* in strict mode, octal literals are not accepted */
        if (is_digit(p[1]) && (s->cur_func->is_strict_mode)) {
            js_parse_error(s, "Octal literals are not allowed in strict mode");
            goto fail;
        }
        goto parse_number;
    case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8':
    case '9':
        /* number */
    parse_number:
        {
            JSValue ret;
            const uint8_t *p1;
            int flags;
            flags = ATOD_ACCEPT_BIN_OCT | ATOD_ACCEPT_LEGACY_OCTAL |
                ATOD_ACCEPT_UNDERSCORES | ATOD_ACCEPT_SUFFIX;
            ret = js_atof(s->ctx, (const char *)p, (const char **)&p, 0,
                          flags);
            if (JS_IsException(ret))
                goto fail;
            /* reject `10instanceof Number` */
            if (JS_VALUE_IS_NAN(ret) ||
                lre_js_is_ident_next(utf8_decode(p, &p1))) {
                JS_FreeValue(s->ctx, ret);
                s->col_num = max_int(1, s->mark - s->eol);
                js_parse_error(s, "invalid number literal");
                goto fail;
            }
            s->token.val = TOK_NUMBER;
            s->token.u.num.val = ret;
        }
        break;
    case '*':
        if (p[1] == '=') {
            p += 2;
            s->token.val = TOK_MUL_ASSIGN;
        } else if (p[1] == '*') {
            if (p[2] == '=') {
                p += 3;
                s->token.val = TOK_POW_ASSIGN;
            } else {
                p += 2;
                s->token.val = TOK_POW;
            }
        } else {
            goto def_token;
        }
        break;
    case '%':
        if (p[1] == '=') {
            p += 2;
            s->token.val = TOK_MOD_ASSIGN;
        } else {
            goto def_token;
        }
        break;
    case '+':
        if (p[1] == '=') {
            p += 2;
            s->token.val = TOK_PLUS_ASSIGN;
        } else if (p[1] == '+') {
            p += 2;
            s->token.val = TOK_INC;
        } else {
            goto def_token;
        }
        break;
    case '-':
        if (p[1] == '=') {
            p += 2;
            s->token.val = TOK_MINUS_ASSIGN;
        } else if (p[1] == '-') {
            if (s->allow_html_comments && p[2] == '>' &&
                (s->got_lf || s->last_ptr == s->buf_start)) {
                /* Annex B: `-->` at beginning of line is an html comment end.
                   It extends to the end of the line.
                 */
                goto skip_line_comment;
            }
            p += 2;
            s->token.val = TOK_DEC;
        } else {
            goto def_token;
        }
        break;
    case '<':
        if (p[1] == '=') {
            p += 2;
            s->token.val = TOK_LTE;
        } else if (p[1] == '<') {
            if (p[2] == '=') {
                p += 3;
                s->token.val = TOK_SHL_ASSIGN;
            } else {
                p += 2;
                s->token.val = TOK_SHL;
            }
        } else if (s->allow_html_comments &&
                   p[1] == '!' && p[2] == '-' && p[3] == '-') {
            /* Annex B: handle `<!--` single line html comments */
            goto skip_line_comment;
        } else {
            goto def_token;
        }
        break;
    case '>':
        if (p[1] == '=') {
            p += 2;
            s->token.val = TOK_GTE;
        } else if (p[1] == '>') {
            if (p[2] == '>') {
                if (p[3] == '=') {
                    p += 4;
                    s->token.val = TOK_SHR_ASSIGN;
                } else {
                    p += 3;
                    s->token.val = TOK_SHR;
                }
            } else if (p[2] == '=') {
                p += 3;
                s->token.val = TOK_SAR_ASSIGN;
            } else {
                p += 2;
                s->token.val = TOK_SAR;
            }
        } else {
            goto def_token;
        }
        break;
    case '=':
        if (p[1] == '=') {
            if (p[2] == '=') {
                p += 3;
                s->token.val = TOK_STRICT_EQ;
            } else {
                p += 2;
                s->token.val = TOK_EQ;
            }
        } else if (p[1] == '>') {
            p += 2;
            s->token.val = TOK_ARROW;
        } else {
            goto def_token;
        }
        break;
    case '!':
        if (p[1] == '=') {
            if (p[2] == '=') {
                p += 3;
                s->token.val = TOK_STRICT_NEQ;
            } else {
                p += 2;
                s->token.val = TOK_NEQ;
            }
        } else {
            goto def_token;
        }
        break;
    case '&':
        if (p[1] == '=') {
            p += 2;
            s->token.val = TOK_AND_ASSIGN;
        } else if (p[1] == '&') {
            if (p[2] == '=') {
                p += 3;
                s->token.val = TOK_LAND_ASSIGN;
            } else {
                p += 2;
                s->token.val = TOK_LAND;
            }
        } else {
            goto def_token;
        }
        break;
    case '^':
        if (p[1] == '=') {
            p += 2;
            s->token.val = TOK_XOR_ASSIGN;
        } else {
            goto def_token;
        }
        break;
    case '|':
        if (p[1] == '=') {
            p += 2;
            s->token.val = TOK_OR_ASSIGN;
        } else if (p[1] == '|') {
            if (p[2] == '=') {
                p += 3;
                s->token.val = TOK_LOR_ASSIGN;
            } else {
                p += 2;
                s->token.val = TOK_LOR;
            }
        } else {
            goto def_token;
        }
        break;
    case '?':
        if (p[1] == '?') {
            if (p[2] == '=') {
                p += 3;
                s->token.val = TOK_DOUBLE_QUESTION_MARK_ASSIGN;
            } else {
                p += 2;
                s->token.val = TOK_DOUBLE_QUESTION_MARK;
            }
        } else if (p[1] == '.' && !(p[2] >= '0' && p[2] <= '9')) {
            p += 2;
            s->token.val = TOK_QUESTION_MARK_DOT;
        } else {
            goto def_token;
        }
        break;
    default:
        if (c >= 0x80) {  /* non-ASCII code-point */
            c = utf8_decode(p, &p_next);
            if (p_next == p + 1)
                goto invalid_utf8;
            p = p_next;
            switch(c) {
            case CP_PS:
            case CP_LS:
                /* XXX: should avoid incrementing line_number, but
                   needed to handle HTML comments */
                goto line_terminator;
            default:
                if (lre_is_space(c)) {
                    s->mark = p;
                    goto redo;
                } else if (lre_js_is_ident_first(c)) {
                    ident_has_escape = false;
                    goto has_ident;
                } else {
                    js_parse_error(s, "unexpected character");
                    goto fail;
                }
            }
        }
    def_token:
        s->token.val = c;
        p++;
        break;
    }
    s->token.col_num = max_int(1, s->mark - s->eol);
    s->buf_ptr = p;

    //    dump_token(s, &s->token);
    return 0;

 invalid_utf8:
    js_parse_error(s, "invalid UTF-8 sequence");
 fail:
    s->token.val = TOK_ERROR;
    return -1;
}

#endif // QJS_DISABLE_PARSER

static int json_parse_error(JSParseState *s, const uint8_t *curp, const char *msg)
{
    const uint8_t *p, *line_start;
    int position = curp - s->buf_start;
    int line = 1;
    for (line_start = p = s->buf_start; p < curp; p++) {
        /* column count does not account for TABs nor wide characters */
        if (*p == '\r' || *p == '\n') {
            p += 1 + (p[0] == '\r' && p[1] == '\n');
            line++;
            line_start = p;
        }
    }
    return js_parse_error(s, "%s in JSON at position %d (line %d column %d)",
                          msg, position, line, (int)(p - line_start) + 1);
}

static int json_parse_string(JSParseState *s, const uint8_t **pp)
{
    const uint8_t *p, *p_next;
    int i;
    uint32_t c;
    StringBuffer b_s, *b = &b_s;

    if (string_buffer_init(s->ctx, b, 48))
        goto fail;

    p = *pp;
    for(;;) {
        if (p >= s->buf_end) {
            goto end_of_input;
        }

        // Fast path: batch consecutive ASCII characters
        const uint8_t *p_start = p;
        while (p < s->buf_end && *p != '"' && *p != '\\' && *p >= 0x20 && *p < 0x80) {
            p++;
        }

        // Write batched ASCII in one call
        if (p > p_start) {
            if (string_buffer_write8(b, p_start, p - p_start))
                goto fail;
        }

        if (p >= s->buf_end)
            goto end_of_input;

        c = *p++;
        if (c == '"')
            break;
        if (c < 0x20) {
            json_parse_error(s, p - 1, "Bad control character in string literal");
            goto fail;
        }
        if (c == '\\') {
            c = *p++;
            switch(c) {
            case 'b':   c = '\b'; break;
            case 'f':   c = '\f'; break;
            case 'n':   c = '\n'; break;
            case 'r':   c = '\r'; break;
            case 't':   c = '\t'; break;
            case '\"':  break;
            case '\\':  break;
            case '/':   break; /* for v8 compatibility */
            case 'u':
                c = 0;
                for(i = 0; i < 4; i++) {
                    int h = from_hex(*p++);
                    if (h < 0) {
                        json_parse_error(s, p - 1, "Bad Unicode escape");
                        goto fail;
                    }
                    c = (c << 4) | h;
                }
                break;
            default:
                if (p > s->buf_end)
                    goto end_of_input;
                json_parse_error(s, p - 1, "Bad escaped character");
                goto fail;
            }
        } else
        if (c >= 0x80) {
            c = utf8_decode(p - 1, &p_next);
            if (p_next == p) {
                json_parse_error(s, p - 1, "Bad UTF-8 sequence");
                goto fail;
            }
            p = p_next;
        }
        if (string_buffer_putc(b, c))
            goto fail;
    }
    s->token.val = TOK_STRING;
    s->token.u.str.sep = '"';
    s->token.u.str.str = string_buffer_end(b);
    *pp = p;
    return 0;

 end_of_input:
    js_parse_error(s, "Unexpected end of JSON input");
 fail:
    string_buffer_free(b);
    return -1;
}

static int json_parse_number(JSParseState *s, const uint8_t **pp)
{
    const uint8_t *p = *pp;
    const uint8_t *p_start = p;

    if (*p == '+' || *p == '-')
        p++;

    if (!is_digit(*p))
        return js_parse_error(s, "Unexpected token '%c'", *p_start);

    if (p[0] == '0' && is_digit(p[1]))
        return json_parse_error(s, p, "Unexpected number");

    while (is_digit(*p))
        p++;

    if (*p == '.') {
        p++;
        if (!is_digit(*p))
            return json_parse_error(s, p, "Unterminated fractional number");
        while (is_digit(*p))
            p++;
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-')
            p++;
        if (!is_digit(*p))
            return json_parse_error(s, p, "Exponent part is missing a number");
        while (is_digit(*p))
            p++;
    }
    s->token.val = TOK_NUMBER;
    s->token.u.num.val = js_float64(strtod((const char *)p_start, NULL));
    *pp = p;
    return 0;
}

/* 'c' is the first character. Return JS_ATOM_NULL in case of error */
static JSAtom json_parse_ident(JSParseState *s, const uint8_t **pp, int c)
{
    const uint8_t *p;
    char ident_buf[128], *buf;
    size_t ident_size, ident_pos;
    JSAtom atom;

    p = *pp;
    buf = ident_buf;
    ident_size = sizeof(ident_buf);
    ident_pos = 0;
    for(;;) {
        buf[ident_pos++] = c;
        c = *p;
        if (c >= 128 ||
            !((lre_id_continue_table_ascii[c >> 5] >> (c & 31)) & 1))
            break;
        p++;
        if (unlikely(ident_pos >= ident_size - UTF8_CHAR_LEN_MAX)) {
            if (ident_realloc(s->ctx, &buf, &ident_size, ident_buf)) {
                atom = JS_ATOM_NULL;
                goto done;
            }
        }
    }
    /* buf contains pure ASCII */
    atom = JS_NewAtomLen(s->ctx, buf, ident_pos);
 done:
    if (unlikely(buf != ident_buf))
        js_free(s->ctx, buf);
    *pp = p;
    return atom;
}

