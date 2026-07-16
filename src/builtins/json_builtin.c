/* Engine domain source: builtins/regexp_json_reflect_proxy_symbol.inc -> json_builtin.
 * Ownership: builtins subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static void js_regexp_string_iterator_mark(JSRuntime *rt, JSValueConst val,
                                           JS_MarkFunc *mark_func)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);
    JSRegExpStringIteratorData *it = p->u.regexp_string_iterator_data;
    if (it) {
        JS_MarkValue(rt, it->iterating_regexp, mark_func);
        JS_MarkValue(rt, it->iterated_string, mark_func);
    }
}

static JSValue js_regexp_string_iterator_next(JSContext *ctx,
                                              JSValueConst this_val,
                                              int argc, JSValueConst *argv,
                                              int *pdone, int magic)
{
    JSRegExpStringIteratorData *it;
    JSValue R, S;
    JSValue matchStr = JS_UNDEFINED, match = JS_UNDEFINED;
    JSString *sp;

    it = JS_GetOpaque2(ctx, this_val, JS_CLASS_REGEXP_STRING_ITERATOR);
    if (!it)
        goto exception;
    if (it->done) {
        *pdone = true;
        return JS_UNDEFINED;
    }
    R = it->iterating_regexp;
    S = it->iterated_string;
    match = JS_RegExpExec(ctx, R, S);
    if (JS_IsException(match))
        goto exception;
    if (JS_IsNull(match)) {
        it->done = true;
        *pdone = true;
        return JS_UNDEFINED;
    } else if (it->global) {
        matchStr = JS_ToStringFree(ctx, JS_GetPropertyInt64(ctx, match, 0));
        if (JS_IsException(matchStr))
            goto exception;
        if (JS_IsEmptyString(matchStr)) {
            int64_t thisIndex, nextIndex;
            if (JS_ToLengthFree(ctx, &thisIndex,
                                JS_GetProperty(ctx, R, JS_ATOM_lastIndex)) < 0)
                goto exception;
            sp = JS_VALUE_GET_STRING(S);
            nextIndex = string_advance_index(sp, thisIndex, it->unicode);
            if (JS_SetProperty(ctx, R, JS_ATOM_lastIndex, js_int64(nextIndex)) < 0)
                goto exception;
        }
        JS_FreeValue(ctx, matchStr);
    } else {
        it->done = true;
    }
    *pdone = false;
    return match;
 exception:
    JS_FreeValue(ctx, match);
    JS_FreeValue(ctx, matchStr);
    *pdone = false;
    return JS_EXCEPTION;
}

static JSValue js_regexp_Symbol_matchAll(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    // [Symbol.matchAll](str)
    JSValueConst R = this_val;
    JSValue S, C, flags, matcher, iter;
    JSValueConst args[2];
    JSString *strp;
    int64_t lastIndex;
    JSRegExpStringIteratorData *it;

    if (!JS_IsObject(R))
        return JS_ThrowTypeErrorNotAnObject(ctx);

    C = JS_UNDEFINED;
    flags = JS_UNDEFINED;
    matcher = JS_UNDEFINED;
    iter = JS_UNDEFINED;

    S = JS_ToString(ctx, argv[0]);
    if (JS_IsException(S))
        goto exception;
    C = JS_SpeciesConstructor(ctx, R, ctx->regexp_ctor);
    if (JS_IsException(C))
        goto exception;
    flags = JS_ToStringFree(ctx, JS_GetProperty(ctx, R, JS_ATOM_flags));
    if (JS_IsException(flags))
        goto exception;
    args[0] = R;
    args[1] = flags;
    matcher = JS_CallConstructor(ctx, C, 2, args);
    if (JS_IsException(matcher))
        goto exception;
    if (JS_ToLengthFree(ctx, &lastIndex,
                        JS_GetProperty(ctx, R, JS_ATOM_lastIndex)))
        goto exception;
    if (JS_SetProperty(ctx, matcher, JS_ATOM_lastIndex, js_int64(lastIndex)) < 0)
        goto exception;

    iter = JS_NewObjectClass(ctx, JS_CLASS_REGEXP_STRING_ITERATOR);
    if (JS_IsException(iter))
        goto exception;
    it = js_malloc(ctx, sizeof(*it));
    if (!it)
        goto exception;
    it->iterating_regexp = matcher;
    it->iterated_string = S;
    strp = JS_VALUE_GET_STRING(flags);
    it->global = string_indexof_char(strp, 'g', 0) >= 0;
    it->unicode = string_indexof_char(strp, 'u', 0) >= 0 ||
                  string_indexof_char(strp, 'v', 0) >= 0;
    it->done = false;
    JS_SetOpaqueInternal(iter, it);

    JS_FreeValue(ctx, C);
    JS_FreeValue(ctx, flags);
    return iter;
 exception:
    JS_FreeValue(ctx, S);
    JS_FreeValue(ctx, C);
    JS_FreeValue(ctx, flags);
    JS_FreeValue(ctx, matcher);
    JS_FreeValue(ctx, iter);
    return JS_EXCEPTION;
}

typedef struct ValueBuffer {
    JSContext *ctx;
    JSValue *arr;
    JSValue def[4];
    int len;
    int size;
    int error_status;
} ValueBuffer;

static int value_buffer_init(JSContext *ctx, ValueBuffer *b)
{
    b->ctx = ctx;
    b->len = 0;
    b->size = 4;
    b->error_status = 0;
    b->arr = b->def;
    return 0;
}

static void value_buffer_free(ValueBuffer *b)
{
    while (b->len > 0)
        JS_FreeValue(b->ctx, b->arr[--b->len]);
    if (b->arr != b->def)
        js_free(b->ctx, b->arr);
    b->arr = b->def;
    b->size = 4;
}

static int value_buffer_append(ValueBuffer *b, JSValue val)
{
    if (b->error_status)
        return -1;

    if (b->len >= b->size) {
        int new_size = (b->len + (b->len >> 1) + 31) & ~16;
        size_t slack;
        JSValue *new_arr;

        if (b->arr == b->def) {
            new_arr = js_realloc2(b->ctx, NULL, sizeof(*b->arr) * new_size, &slack);
            if (new_arr)
                memcpy(new_arr, b->def, sizeof b->def);
        } else {
            new_arr = js_realloc2(b->ctx, b->arr, sizeof(*b->arr) * new_size, &slack);
        }
        if (!new_arr) {
            value_buffer_free(b);
            JS_FreeValue(b->ctx, val);
            b->error_status = -1;
            return -1;
        }
        new_size += slack / sizeof(*new_arr);
        b->arr = new_arr;
        b->size = new_size;
    }
    b->arr[b->len++] = val;
    return 0;
}

static int js_is_standard_regexp(JSContext *ctx, JSValueConst rx)
{
    JSValue val;
    int res;

    val = JS_GetProperty(ctx, rx, JS_ATOM_constructor);
    if (JS_IsException(val))
        return -1;
    // rx.constructor === RegExp
    res = js_same_value(ctx, val, ctx->regexp_ctor);
    JS_FreeValue(ctx, val);
    if (res) {
        val = JS_GetProperty(ctx, rx, JS_ATOM_exec);
        if (JS_IsException(val))
            return -1;
        // rx.exec === RE_exec
        res = JS_IsCFunction(ctx, val, js_regexp_exec, 0);
        JS_FreeValue(ctx, val);
    }
    return res;
}

static JSValue js_regexp_Symbol_replace(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv)
{
    // [Symbol.replace](str, rep)
    JSValueConst rx = this_val, rep = argv[1];
    JSValueConst args[6];
    JSValue flags, str, rep_val, matched, tab, rep_str, namedCaptures, res;
    JSString *p, *sp, *rp;
    StringBuffer b_s, *b = &b_s;
    ValueBuffer v_b, *results = &v_b;
    int nextSourcePosition, n, j, functionalReplace, is_global, fullUnicode;
    uint32_t nCaptures;
    int64_t position;

    if (!JS_IsObject(rx))
        return JS_ThrowTypeErrorNotAnObject(ctx);

    string_buffer_init(ctx, b, 0);
    value_buffer_init(ctx, results);

    rep_val = JS_UNDEFINED;
    matched = JS_UNDEFINED;
    tab = JS_UNDEFINED;
    flags = JS_UNDEFINED;
    rep_str = JS_UNDEFINED;
    namedCaptures = JS_UNDEFINED;

    str = JS_ToString(ctx, argv[0]);
    if (JS_IsException(str))
        goto exception;

    sp = JS_VALUE_GET_STRING(str);
    rp = NULL;
    functionalReplace = JS_IsFunction(ctx, rep);
    if (!functionalReplace) {
        rep_val = JS_ToString(ctx, rep);
        if (JS_IsException(rep_val))
            goto exception;
        rp = JS_VALUE_GET_STRING(rep_val);
    }

    flags = JS_GetProperty(ctx, rx, JS_ATOM_flags);
    if (JS_IsException(flags))
        goto exception;
    flags = JS_ToStringFree(ctx, flags);
    if (JS_IsException(flags))
        goto exception;
    p = JS_VALUE_GET_STRING(flags);

    fullUnicode = 0;
    is_global = (-1 != string_indexof_char(p, 'g', 0));
    if (is_global) {
        // 'v' flag implies full Unicode, like 'u'
        fullUnicode = (string_indexof_char(p, 'u', 0) >= 0 ||
                       string_indexof_char(p, 'v', 0) >= 0);
        if (JS_SetProperty(ctx, rx, JS_ATOM_lastIndex, js_int32(0)) < 0)
            goto exception;
    }

    if (rp && rp->len == 0 && is_global && js_is_standard_regexp(ctx, rx)) {
        /* use faster version for simple cases */
        res = JS_RegExpDelete(ctx, rx, str);
        goto done;
    }
    for(;;) {
        JSValue result;
        result = JS_RegExpExec(ctx, rx, str);
        if (JS_IsException(result))
            goto exception;
        if (JS_IsNull(result))
            break;
        if (value_buffer_append(results, result) < 0)
            goto exception;
        if (!is_global)
            break;
        JS_FreeValue(ctx, matched);
        matched = JS_ToStringFree(ctx, JS_GetPropertyInt64(ctx, result, 0));
        if (JS_IsException(matched))
            goto exception;
        if (JS_IsEmptyString(matched)) {
            /* always advance of at least one char */
            int64_t thisIndex, nextIndex;
            if (JS_ToLengthFree(ctx, &thisIndex, JS_GetProperty(ctx, rx, JS_ATOM_lastIndex)) < 0)
                goto exception;
            nextIndex = string_advance_index(sp, thisIndex, fullUnicode);
            if (JS_SetProperty(ctx, rx, JS_ATOM_lastIndex, js_int64(nextIndex)) < 0)
                goto exception;
        }
    }
    nextSourcePosition = 0;
    for(j = 0; j < results->len; j++) {
        JSValue result;
        result = results->arr[j];
        if (js_get_length32(ctx, &nCaptures, result) < 0)
            goto exception;
        JS_FreeValue(ctx, matched);
        matched = JS_ToStringFree(ctx, JS_GetPropertyInt64(ctx, result, 0));
        if (JS_IsException(matched))
            goto exception;
        if (JS_ToLengthFree(ctx, &position, JS_GetProperty(ctx, result, JS_ATOM_index)))
            goto exception;
        if (position > sp->len)
            position = sp->len;
        else if (position < 0)
            position = 0;
        /* ignore substition if going backward (can happen
           with custom regexp object) */
        JS_FreeValue(ctx, tab);
        tab = JS_NewArray(ctx);
        if (JS_IsException(tab))
            goto exception;
        if (JS_DefinePropertyValueInt64Const(ctx, tab, 0, matched,
                                             JS_PROP_C_W_E | JS_PROP_THROW) < 0)
            goto exception;
        for(n = 1; n < nCaptures; n++) {
            JSValue capN;
            capN = JS_GetPropertyInt64(ctx, result, n);
            if (JS_IsException(capN))
                goto exception;
            if (!JS_IsUndefined(capN)) {
                capN = JS_ToStringFree(ctx, capN);
                if (JS_IsException(capN))
                    goto exception;
            }
            if (JS_DefinePropertyValueInt64(ctx, tab, n, capN,
                                            JS_PROP_C_W_E | JS_PROP_THROW) < 0)
                goto exception;
        }
        JS_FreeValue(ctx, namedCaptures);
        namedCaptures = JS_GetProperty(ctx, result, JS_ATOM_groups);
        if (JS_IsException(namedCaptures))
            goto exception;
        if (functionalReplace) {
            if (JS_DefinePropertyValueInt64(ctx, tab, n++, js_int32(position), JS_PROP_C_W_E | JS_PROP_THROW) < 0)
                goto exception;
            if (JS_DefinePropertyValueInt64Const(ctx, tab, n++, str, JS_PROP_C_W_E | JS_PROP_THROW) < 0)
                goto exception;
            if (!JS_IsUndefined(namedCaptures)) {
                if (JS_DefinePropertyValueInt64Const(ctx, tab, n++, namedCaptures, JS_PROP_C_W_E | JS_PROP_THROW) < 0)
                    goto exception;
            }
            args[0] = JS_UNDEFINED;
            args[1] = tab;
            JS_FreeValue(ctx, rep_str);
            rep_str = JS_ToStringFree(ctx, js_function_apply(ctx, rep, 2, args, 0));
        } else {
            JSValue namedCaptures1;
            if (!JS_IsUndefined(namedCaptures)) {
                namedCaptures1 = JS_ToObject(ctx, namedCaptures);
                if (JS_IsException(namedCaptures1))
                    goto exception;
            } else {
                namedCaptures1 = JS_UNDEFINED;
            }
            args[0] = matched;
            args[1] = str;
            args[2] = js_int32(position);
            args[3] = tab;
            args[4] = namedCaptures1;
            args[5] = rep_val;
            JS_FreeValue(ctx, rep_str);
            rep_str = js_string___GetSubstitution(ctx, JS_UNDEFINED, 6, args);
            JS_FreeValue(ctx, namedCaptures1);
        }
        if (JS_IsException(rep_str))
            goto exception;
        if (position >= nextSourcePosition) {
            string_buffer_concat(b, sp, nextSourcePosition, position);
            string_buffer_concat_value(b, rep_str);
            nextSourcePosition = position + JS_VALUE_GET_STRING(matched)->len;
        }
    }
    string_buffer_concat(b, sp, nextSourcePosition, sp->len);
    res = string_buffer_end(b);
    goto done1;

exception:
    res = JS_EXCEPTION;
done:
    string_buffer_free(b);
done1:
    value_buffer_free(results);
    JS_FreeValue(ctx, rep_val);
    JS_FreeValue(ctx, matched);
    JS_FreeValue(ctx, flags);
    JS_FreeValue(ctx, tab);
    JS_FreeValue(ctx, rep_str);
    JS_FreeValue(ctx, namedCaptures);
    JS_FreeValue(ctx, str);
    return res;
}

static JSValue js_regexp_Symbol_search(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    JSValueConst rx = this_val;
    JSValue str, previousLastIndex, currentLastIndex, result, index;

    if (!JS_IsObject(rx))
        return JS_ThrowTypeErrorNotAnObject(ctx);

    result = JS_UNDEFINED;
    currentLastIndex = JS_UNDEFINED;
    previousLastIndex = JS_UNDEFINED;
    str = JS_ToString(ctx, argv[0]);
    if (JS_IsException(str))
        goto exception;

    previousLastIndex = JS_GetProperty(ctx, rx, JS_ATOM_lastIndex);
    if (JS_IsException(previousLastIndex))
        goto exception;

    if (!js_same_value(ctx, previousLastIndex, js_int32(0))) {
        if (JS_SetProperty(ctx, rx, JS_ATOM_lastIndex, js_int32(0)) < 0) {
            goto exception;
        }
    }
    result = JS_RegExpExec(ctx, rx, str);
    if (JS_IsException(result))
        goto exception;
    currentLastIndex = JS_GetProperty(ctx, rx, JS_ATOM_lastIndex);
    if (JS_IsException(currentLastIndex))
        goto exception;
    if (js_same_value(ctx, currentLastIndex, previousLastIndex)) {
        JS_FreeValue(ctx, previousLastIndex);
    } else {
        if (JS_SetProperty(ctx, rx, JS_ATOM_lastIndex, previousLastIndex) < 0) {
            previousLastIndex = JS_UNDEFINED;
            goto exception;
        }
    }
    JS_FreeValue(ctx, str);
    JS_FreeValue(ctx, currentLastIndex);

    if (JS_IsNull(result)) {
        return js_int32(-1);
    } else {
        index = JS_GetProperty(ctx, result, JS_ATOM_index);
        JS_FreeValue(ctx, result);
        return index;
    }

exception:
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, str);
    JS_FreeValue(ctx, currentLastIndex);
    JS_FreeValue(ctx, previousLastIndex);
    return JS_EXCEPTION;
}

static JSValue js_regexp_Symbol_split(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    // [Symbol.split](str, limit)
    JSValueConst rx = this_val;
    JSValueConst args[2];
    JSValue str, ctor, splitter, A, flags, z, sub;
    JSString *strp;
    uint32_t lim, size, p, q;
    int unicodeMatching;
    int64_t lengthA, e, numberOfCaptures, i;

    if (!JS_IsObject(rx))
        return JS_ThrowTypeErrorNotAnObject(ctx);

    ctor = JS_UNDEFINED;
    splitter = JS_UNDEFINED;
    A = JS_UNDEFINED;
    flags = JS_UNDEFINED;
    z = JS_UNDEFINED;
    str = JS_ToString(ctx, argv[0]);
    if (JS_IsException(str))
        goto exception;
    ctor = JS_SpeciesConstructor(ctx, rx, ctx->regexp_ctor);
    if (JS_IsException(ctor))
        goto exception;
    flags = JS_ToStringFree(ctx, JS_GetProperty(ctx, rx, JS_ATOM_flags));
    if (JS_IsException(flags))
        goto exception;
    strp = JS_VALUE_GET_STRING(flags);
    unicodeMatching = string_indexof_char(strp, 'u', 0) >= 0 ||
                      string_indexof_char(strp, 'v', 0) >= 0;
    if (string_indexof_char(strp, 'y', 0) < 0) {
        flags = JS_ConcatString3(ctx, "", flags, "y");
        if (JS_IsException(flags))
            goto exception;
    }
    args[0] = rx;
    args[1] = flags;
    splitter = JS_CallConstructor(ctx, ctor, 2, args);
    if (JS_IsException(splitter))
        goto exception;
    A = JS_NewArray(ctx);
    if (JS_IsException(A))
        goto exception;
    lengthA = 0;
    if (JS_IsUndefined(argv[1])) {
        lim = 0xffffffff;
    } else {
        if (JS_ToUint32(ctx, &lim, argv[1]) < 0)
            goto exception;
        if (lim == 0)
            goto done;
    }
    strp = JS_VALUE_GET_STRING(str);
    p = q = 0;
    size = strp->len;
    if (size == 0) {
        z = JS_RegExpExec(ctx, splitter, str);
        if (JS_IsException(z))
            goto exception;
        if (JS_IsNull(z))
            goto add_tail;
        goto done;
    }
    while (q < size) {
        if (JS_SetProperty(ctx, splitter, JS_ATOM_lastIndex, js_int32(q)) < 0)
            goto exception;
        JS_FreeValue(ctx, z);
        z = JS_RegExpExec(ctx, splitter, str);
        if (JS_IsException(z))
            goto exception;
        if (JS_IsNull(z)) {
            q = string_advance_index(strp, q, unicodeMatching);
        } else {
            if (JS_ToLengthFree(ctx, &e, JS_GetProperty(ctx, splitter, JS_ATOM_lastIndex)))
                goto exception;
            if (e > size)
                e = size;
            if (e == p) {
                q = string_advance_index(strp, q, unicodeMatching);
            } else {
                sub = js_sub_string(ctx, strp, p, q);
                if (JS_IsException(sub))
                    goto exception;
                if (JS_DefinePropertyValueInt64(ctx, A, lengthA++, sub,
                                                JS_PROP_C_W_E | JS_PROP_THROW) < 0)
                    goto exception;
                if (lengthA == lim)
                    goto done;
                p = e;
                if (js_get_length64(ctx, &numberOfCaptures, z))
                    goto exception;
                for(i = 1; i < numberOfCaptures; i++) {
                    sub = JS_GetPropertyInt64(ctx, z, i);
                    if (JS_IsException(sub))
                        goto exception;
                    if (!JS_IsUndefined(sub)) {
                        sub = JS_ToStringFree(ctx, sub);
                        if (JS_IsException(sub))
                            goto exception;
                    }
                    if (JS_DefinePropertyValueInt64(ctx, A, lengthA++, sub, JS_PROP_C_W_E | JS_PROP_THROW) < 0)
                        goto exception;
                    if (lengthA == lim)
                        goto done;
                }
                q = p;
            }
        }
    }
add_tail:
    if (p > size)
        p = size;
    sub = js_sub_string(ctx, strp, p, size);
    if (JS_IsException(sub))
        goto exception;
    if (JS_DefinePropertyValueInt64(ctx, A, lengthA++, sub, JS_PROP_C_W_E | JS_PROP_THROW) < 0)
        goto exception;
    goto done;
exception:
    JS_FreeValue(ctx, A);
    A = JS_EXCEPTION;
done:
    JS_FreeValue(ctx, str);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, splitter);
    JS_FreeValue(ctx, flags);
    JS_FreeValue(ctx, z);
    return A;
}

static const JSCFunctionListEntry js_regexp_funcs[] = {
    JS_CFUNC_DEF("escape", 1, js_regexp_escape ),
    JS_CGETSET_DEF("[Symbol.species]", js_get_this, NULL ),
};

static const JSCFunctionListEntry js_regexp_proto_funcs[] = {
    JS_CGETSET_DEF("flags", js_regexp_get_flags, NULL ),
    JS_CGETSET_DEF("source", js_regexp_get_source, NULL ),
    JS_CGETSET_MAGIC_DEF("global", js_regexp_get_flag, NULL, LRE_FLAG_GLOBAL ),
    JS_CGETSET_MAGIC_DEF("ignoreCase", js_regexp_get_flag, NULL, LRE_FLAG_IGNORECASE ),
    JS_CGETSET_MAGIC_DEF("multiline", js_regexp_get_flag, NULL, LRE_FLAG_MULTILINE ),
    JS_CGETSET_MAGIC_DEF("dotAll", js_regexp_get_flag, NULL, LRE_FLAG_DOTALL ),
    JS_CGETSET_MAGIC_DEF("unicode", js_regexp_get_flag, NULL, LRE_FLAG_UNICODE ),
    JS_CGETSET_MAGIC_DEF("unicodeSets", js_regexp_get_flag, NULL, LRE_FLAG_UNICODE_SETS ),
    JS_CGETSET_MAGIC_DEF("sticky", js_regexp_get_flag, NULL, LRE_FLAG_STICKY ),
    JS_CGETSET_MAGIC_DEF("hasIndices", js_regexp_get_flag, NULL, LRE_FLAG_INDICES ),
    JS_CFUNC_DEF("exec", 1, js_regexp_exec ),
    JS_CFUNC_DEF("compile", 2, js_regexp_compile ),
    JS_CFUNC_DEF("test", 1, js_regexp_test ),
    JS_CFUNC_DEF("toString", 0, js_regexp_toString ),
    JS_CFUNC_DEF("[Symbol.replace]", 2, js_regexp_Symbol_replace ),
    JS_CFUNC_DEF("[Symbol.match]", 1, js_regexp_Symbol_match ),
    JS_CFUNC_DEF("[Symbol.matchAll]", 1, js_regexp_Symbol_matchAll ),
    JS_CFUNC_DEF("[Symbol.search]", 1, js_regexp_Symbol_search ),
    JS_CFUNC_DEF("[Symbol.split]", 2, js_regexp_Symbol_split ),
};

static const JSCFunctionListEntry js_regexp_string_iterator_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_regexp_string_iterator_next, 0 ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "RegExp String Iterator", JS_PROP_CONFIGURABLE ),
};

void JS_AddIntrinsicRegExpCompiler(JSContext *ctx)
{
    ctx->compile_regexp = js_compile_regexp;
}

int JS_AddIntrinsicRegExp(JSContext *ctx)
{
    JSValue proto, obj;

    proto = ctx->class_proto[JS_CLASS_REGEXP] = JS_NewObject(ctx);
    if (JS_IsException(proto))
        return -1;
    if (JS_SetPropertyFunctionList(ctx, proto, js_regexp_proto_funcs,
                                   countof(js_regexp_proto_funcs))) {
        return -1;
    }
    obj = JS_NewGlobalCConstructor(ctx, "RegExp", js_regexp_constructor, 2,
                                   proto);
    if (JS_IsException(obj))
        return -1;
    ctx->regexp_ctor = js_dup(obj);
    if (JS_SetPropertyFunctionList(ctx, obj, js_regexp_funcs,
                                   countof(js_regexp_funcs))) {
        return -1;
    }
    static const JSShapeProperty regexp_props[] = {
        {.atom=JS_ATOM_lastIndex, .flags=JS_PROP_WRITABLE},
    };
    static const JSShapeProperty regexp_result_props[] = {
        {.atom=JS_ATOM_length, .flags=JS_PROP_WRITABLE|JS_PROP_LENGTH},
        {.atom=JS_ATOM_index,  .flags=JS_PROP_C_W_E},
        {.atom=JS_ATOM_input,  .flags=JS_PROP_C_W_E},
        {.atom=JS_ATOM_groups, .flags=JS_PROP_C_W_E},
    };
    if (js_new_shape_with(ctx, &ctx->regexp_shape, proto,
                          countof(regexp_props), regexp_props)) {
        return -1;
    }
    if (js_new_shape_with(ctx, &ctx->regexp_result_shape,
                          ctx->class_proto[JS_CLASS_ARRAY],
                          countof(regexp_result_props), regexp_result_props)) {
        return -1;
    }
    proto = ctx->class_proto[JS_CLASS_REGEXP_STRING_ITERATOR] =
        JS_NewObjectProto(ctx, ctx->class_proto[JS_CLASS_ITERATOR]);
    if (JS_IsException(proto))
        return -1;
    if (JS_SetPropertyFunctionList(ctx, ctx->class_proto[JS_CLASS_REGEXP_STRING_ITERATOR],
                                   js_regexp_string_iterator_proto_funcs,
                                   countof(js_regexp_string_iterator_proto_funcs))) {
        return -1;
    }
    JS_AddIntrinsicRegExpCompiler(ctx);
    return 0;
}

/* JSON */

typedef struct {
    int count;
    uint32_t hash_size;
    struct JSONParseRecordEntry *entries;
    uint32_t *hash_table;
} JSONParseRecordObject;

typedef struct JSONParseRecord {
    JSValue value;
    union {
        JSONParseRecordObject obj;
        struct {
            int count;
            struct JSONParseRecord *elements;
        } array;
        struct {
            uint32_t source_pos;
            uint32_t source_len;
        } primitive;
    } u;
} JSONParseRecord;

typedef struct JSONParseRecordEntry {
    JSAtom atom;
    uint32_t hash_next;
    JSONParseRecord parse_record;
} JSONParseRecordEntry;

static void json_parse_record_init_obj(JSContext *ctx, JSONParseRecord *pr, JSValueConst val)
{
    pr->value = js_dup(val);
    pr->u.obj.count = 0;
    pr->u.obj.entries = NULL;
    pr->u.obj.hash_table = NULL;
    pr->u.obj.hash_size = 0;
}

static void json_parse_record_init_array(JSContext *ctx, JSONParseRecord *pr, JSValueConst val)
{
    pr->value = js_dup(val);
    pr->u.array.count = 0;
    pr->u.array.elements = NULL;
}

static void json_parse_record_init_primitive(JSContext *ctx, JSONParseRecord *pr, JSValueConst val,
                                             uint32_t source_pos, uint32_t source_len)
{
    pr->value = js_dup(val);
    pr->u.primitive.source_pos = source_pos;
    pr->u.primitive.source_len = source_len;
}

static int json_parse_record_resize_hash(JSContext *ctx, JSONParseRecordObject *po, uint32_t new_hash_size)
{
    uint32_t i, h, *new_hash_table;
    JSONParseRecordEntry *e;

    new_hash_table = js_malloc(ctx, sizeof(new_hash_table[0]) * new_hash_size);
    if (!new_hash_table)
        return -1;
    js_free(ctx, po->hash_table);
    po->hash_table = new_hash_table;
    po->hash_size = new_hash_size;

    for(i = 0; i < po->hash_size; i++) {
        po->hash_table[i] = -1;
    }
    for(i = 0; i < po->count; i++) {
        e = &po->entries[i];
        h = e->atom & (po->hash_size - 1);
        e->hash_next = po->hash_table[h];
        po->hash_table[h] = i;
    }
    return 0;
}

static JSONParseRecord *json_parse_record_add(JSContext *ctx, JSONParseRecord *pr, JSAtom key, int *psize)
{
    JSONParseRecordObject *po = &pr->u.obj;
    JSONParseRecordEntry *e;
    JSONParseRecord *pr1;
    uint32_t h;

    if (js_resize_array(ctx, (void **)&po->entries, sizeof(po->entries[0]),
                        psize, po->count + 1)) {
        return NULL;
    }
    /* switch to hash table when going over size threshold */
    if (po->count >= 8 && (po->count + 1) > po->hash_size) {
        int hash_bits = 32 - clz32(po->count);
        if (json_parse_record_resize_hash(ctx, po, 1 << hash_bits))
            return NULL;
    }

    e = &po->entries[po->count++];
    e->atom = JS_DupAtom(ctx, key);
    pr1 = &e->parse_record;
    pr1->value = JS_UNDEFINED;
    if (po->hash_size != 0) {
        h = key & (po->hash_size - 1);
        e->hash_next = po->hash_table[h];
        po->hash_table[h] = po->count - 1;
    }
    return pr1;
}

static JSONParseRecord *json_parse_record_find(JSONParseRecord *pr, JSAtom key)
{
    JSONParseRecordObject *po = &pr->u.obj;
    JSONParseRecordEntry *e;
    uint32_t h, i;

    if (po->hash_size == 0) {
        for(i = 0; i < po->count; i++) {
            if (po->entries[i].atom == key)
                return &po->entries[i].parse_record;
        }
    } else {
        h = key & (po->hash_size - 1);
        i = po->hash_table[h];
        while (i != -1) {
            e = &po->entries[i];
            if (e->atom == key)
                return &e->parse_record;
            i = e->hash_next;
        }
    }
    return NULL;
}

static void json_free_parse_record(JSContext *ctx, JSONParseRecord *pr)
{
    int i;
    if (!pr)
        return;
    if (JS_IsObject(pr->value)) {
        if (js_is_array(ctx, pr->value)) {
            for(i = 0; i < pr->u.array.count; i++) {
                json_free_parse_record(ctx, &pr->u.array.elements[i]);
            }
            js_free(ctx, pr->u.array.elements);
        } else {
            for(i = 0; i < pr->u.obj.count; i++) {
                JS_FreeAtom(ctx, pr->u.obj.entries[i].atom);
                json_free_parse_record(ctx, &pr->u.obj.entries[i].parse_record);
            }
            js_free(ctx, pr->u.obj.entries);
            js_free(ctx, pr->u.obj.hash_table);
        }
    }
    JS_FreeValue(ctx, pr->value);
    pr->value = JS_UNDEFINED; /* fail safe */
}

/* 'pr' can be NULL */
static JSValue json_parse_value(JSParseState *s, JSONParseRecord *pr)
{
    JSContext *ctx = s->ctx;
    JSValue val = JS_NULL;
    int ret;

    if (js_check_stack_overflow(ctx->rt, 0)) {
        return JS_ThrowStackOverflow(ctx);
    }

    if (pr) {
        pr->value = JS_UNDEFINED;
    }

    switch(s->token.val) {
    case '{':
        {
            JSValue prop_val;
            JSAtom prop_name;
            JSONParseRecord *pr1;
            int pr_size;

            if (json_next_token(s))
                goto fail;
            val = JS_NewObject(ctx);
            if (JS_IsException(val))
                goto fail;
            if (pr) {
                json_parse_record_init_obj(ctx, pr, val);
                pr_size = 0;
            }
            if (s->token.val != '}') {
                for(;;) {
                    if (s->token.val == TOK_STRING) {
                        prop_name = JS_ValueToAtom(ctx, s->token.u.str.str);
                        if (prop_name == JS_ATOM_NULL)
                            goto fail;
                    } else {
                        json_parse_error(s, s->token.ptr, "Expected property name or '}'");
                        goto fail;
                    }
                    if (json_next_token(s))
                        goto fail1;
                    if (s->token.val != ':') {
                        json_parse_error(s, s->token.ptr, "Expected ':' after property name");
                        goto fail1;
                    }
                    if (json_next_token(s))
                        goto fail1;
                    if (pr) {
                        pr1 = json_parse_record_add(ctx, pr, prop_name, &pr_size);
                        if (!pr1)
                            goto fail1;
                    } else {
                        pr1 = NULL;
                    }
                    prop_val = json_parse_value(s, pr1);
                    if (JS_IsException(prop_val)) {
                    fail1:
                        JS_FreeAtom(ctx, prop_name);
                        goto fail;
                    }
                    ret = JS_DefinePropertyValue(ctx, val, prop_name,
                                                 prop_val, JS_PROP_C_W_E);
                    JS_FreeAtom(ctx, prop_name);
                    if (ret < 0)
                        goto fail;

                    if (s->token.val == '}')
                        break;
                    if (s->token.val != ',') {
                        json_parse_error(s, s->token.ptr, "Expected ',' or '}' after property value");
                        goto fail;
                    }
                    if (json_next_token(s))
                        goto fail;
                }
            }
            if (json_next_token(s))
                goto fail;
        }
        break;
    case '[':
        {
            JSValue el;
            uint32_t idx;
            JSONParseRecord *pr1;
            int pr_size;

            if (json_next_token(s))
                goto fail;
            val = JS_NewArray(ctx);
            if (JS_IsException(val))
                goto fail;
            if (pr) {
                json_parse_record_init_array(ctx, pr, val);
                pr_size = 0;
            }
            if (s->token.val != ']') {
                for(idx = 0;; idx++) {
                    if (pr) {
                        if (js_resize_array(ctx, (void **)&pr->u.array.elements, sizeof(pr->u.array.elements[0]),
                                            &pr_size, pr->u.array.count + 1))
                            goto fail;
                        pr1 = &pr->u.array.elements[pr->u.array.count++];
                        pr1->value = JS_UNDEFINED;
                    } else {
                        pr1 = NULL;
                    }
                    el = json_parse_value(s, pr1);
                    if (JS_IsException(el))
                        goto fail;
                    ret = JS_DefinePropertyValueUint32(ctx, val, idx, el, JS_PROP_C_W_E);
                    if (ret < 0)
                        goto fail;
                    if (s->token.val == ']')
                        break;
                    if (s->token.val != ',') {
                        json_parse_error(s, s->token.ptr, "Expected ',' or ']' after array element");
                        goto fail;
                    }
                    if (json_next_token(s))
                        goto fail;
                }
            }
            if (json_next_token(s))
                goto fail;
        }
        break;
    case TOK_STRING:
        val = js_dup(s->token.u.str.str);
        if (pr) {
            json_parse_record_init_primitive(ctx, pr, val,
                                             s->token.ptr - s->buf_start,
                                             s->buf_ptr - s->token.ptr);
        }
        if (json_next_token(s))
            goto fail;
        break;
    case TOK_NUMBER:
        val = s->token.u.num.val;
        if (pr) {
            json_parse_record_init_primitive(ctx, pr, val,
                                             s->token.ptr - s->buf_start,
                                             s->buf_ptr - s->token.ptr);
        }
        if (json_next_token(s))
            goto fail;
        break;
    case TOK_IDENT:
        if (s->token.u.ident.atom == JS_ATOM_false ||
            s->token.u.ident.atom == JS_ATOM_true) {
            val = js_bool(s->token.u.ident.atom == JS_ATOM_true);
            if (pr) {
                json_parse_record_init_primitive(ctx, pr, val,
                                                 s->token.ptr - s->buf_start,
                                                 s->buf_ptr - s->token.ptr);
            }
        } else if (s->token.u.ident.atom == JS_ATOM_null) {
            val = JS_NULL;
            if (pr) {
                json_parse_record_init_primitive(ctx, pr, val,
                                                 s->token.ptr - s->buf_start,
                                                 s->buf_ptr - s->token.ptr);
            }
        } else {
            goto def_token;
        }
        if (json_next_token(s))
            goto fail;
        break;
    default:
    def_token:
        if (s->token.val == TOK_EOF) {
            js_parse_error(s, "Unexpected end of JSON input");
        } else {
            js_parse_error(s, "unexpected token: '%.*s'",
                           (int)(s->buf_ptr - s->token.ptr), s->token.ptr);
        }
        goto fail;
    }
    return val;
 fail:
    json_free_parse_record(ctx, pr);
    JS_FreeValue(ctx, val);
    return JS_EXCEPTION;
}

