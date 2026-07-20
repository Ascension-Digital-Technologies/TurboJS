/* Engine domain source: builtins/regexp_json_reflect_proxy_symbol.inc -> reflect_proxy_builtin.
 * Ownership: builtins subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static JSValue JS_ParseJSON_internal(JSContext *ctx, const char *buf, size_t buf_len,
                                     const char *filename, JSONParseRecord *pr)
{
    JSParseState s1, *s = &s1;
    JSValue val = JS_UNDEFINED;

    js_parse_init(ctx, s, buf, buf_len, filename, 1);
    if (json_next_token(s))
        goto fail;
    val = json_parse_value(s, pr);
    if (JS_IsException(val))
        goto fail;
    if (s->token.val != TOK_EOF) {
        if (js_parse_error(s, "unexpected data at the end")) {
            json_free_parse_record(ctx, pr);
            goto fail;
        }
    }
    return val;
 fail:
    JS_FreeValue(ctx, val);
    free_token(s, &s->token);
    return JS_EXCEPTION;
}

/* 'buf' must be zero terminated i.e. buf[buf_len] = '\0'. */
JSValue JS_ParseJSON(JSContext *ctx, const char *buf, size_t buf_len, const char *filename)
{
    return JS_ParseJSON_internal(ctx, buf, buf_len, filename, NULL);
}

/* if pr != NULL, then pr->value = holder by construction */
static JSValue internalize_json_property(JSContext *ctx, JSValueConst holder,
                                         JSAtom name, JSValueConst reviver,
                                         const char *text_str, JSONParseRecord *pr)
{
    JSValue val, new_el, name_val, res, context;
    JSValueConst args[3];
    int ret, is_array;
    uint32_t i, len = 0;
    JSAtom prop;
    JSPropertyEnum *atoms = NULL;

    if (js_check_stack_overflow(ctx->rt, 0)) {
        return JS_ThrowStackOverflow(ctx);
    }

    val = JS_GetProperty(ctx, holder, name);
    if (JS_IsException(val))
        return val;

    if (pr) {
        if (js_is_array(ctx, pr->value)) {
            if (__JS_AtomIsTaggedInt(name)) {
                uint32_t idx = __JS_AtomToUInt32(name);
                if (idx < pr->u.array.count) {
                    pr = &pr->u.array.elements[idx];
                } else {
                    pr = NULL;
                }
            }
        } else {
            pr = json_parse_record_find(pr, name);
        }
        if (pr && !js_same_value(ctx, pr->value, val)) {
            pr = NULL;
        }
    }

    context = JS_NewObject(ctx);
    if (JS_IsException(context))
        goto fail;

    if (JS_IsObject(val)) {
        is_array = js_is_array(ctx, val);
        if (is_array < 0)
            goto fail;
        if (is_array) {
            if (js_get_length32(ctx, &len, val))
                goto fail;
        } else {
            ret = JS_GetOwnPropertyNamesInternal(ctx, &atoms, &len, JS_VALUE_GET_OBJ(val), JS_GPN_ENUM_ONLY | JS_GPN_STRING_MASK);
            if (ret < 0)
                goto fail;
        }
        for(i = 0; i < len; i++) {
            if (is_array) {
                prop = JS_NewAtomUInt32(ctx, i);
                if (prop == JS_ATOM_NULL)
                    goto fail;
            } else {
                prop = JS_DupAtom(ctx, atoms[i].atom);
            }
            new_el = internalize_json_property(ctx, val, prop, reviver, text_str, pr);
            if (JS_IsException(new_el)) {
                JS_FreeAtom(ctx, prop);
                goto fail;
            }
            if (JS_IsUndefined(new_el)) {
                ret = JS_DeleteProperty(ctx, val, prop, 0);
            } else {
                ret = JS_DefinePropertyValue(ctx, val, prop, new_el, JS_PROP_C_W_E);
            }
            JS_FreeAtom(ctx, prop);
            if (ret < 0)
                goto fail;
        }
    } else {
        if (pr) {
            new_el = JS_NewStringLen(ctx, text_str + pr->u.primitive.source_pos,
                                     pr->u.primitive.source_len);
            if (JS_IsException(new_el))
                goto fail;
            if (JS_DefinePropertyValue(ctx, context, JS_ATOM_source, new_el, JS_PROP_C_W_E) < 0)
                goto fail;
        }
    }
    js_free_prop_enum(ctx, atoms, len);
    atoms = NULL;
    name_val = JS_AtomToValue(ctx, name);
    if (JS_IsException(name_val))
        goto fail;
    args[0] = name_val;
    args[1] = val;
    args[2] = context;
    res = JS_Call(ctx, reviver, holder, 3, args);
    JS_FreeValue(ctx, name_val);
    JS_FreeValue(ctx, val);
    JS_FreeValue(ctx, context);
    return res;
 fail:
    js_free_prop_enum(ctx, atoms, len);
    JS_FreeValue(ctx, context);
    JS_FreeValue(ctx, val);
    return JS_EXCEPTION;
}

static JSValue js_json_parse(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    JSValue obj;
    const char *str;
    size_t len;

    str = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!str)
        return JS_EXCEPTION;
    if (argc > 1 && JS_IsFunction(ctx, argv[1])) {
        JSONParseRecord pr_s, *pr = &pr_s, *pr1;
        JSValue root;
        JSValueConst reviver;
        int size;

        reviver = argv[1];
        root = JS_NewObject(ctx);
        if (JS_IsException(root))
            goto fail;
        json_parse_record_init_obj(ctx, pr, root);
        size = 0;
        pr1 = json_parse_record_add(ctx, pr, JS_ATOM_empty_string, &size);
        if (!pr1)
            goto fail1;

        obj = JS_ParseJSON_internal(ctx, str, len, "<input>", pr1);
        if (JS_IsException(obj))
            goto fail1;

        if (JS_DefinePropertyValue(ctx, root, JS_ATOM_empty_string, obj,
                                   JS_PROP_C_W_E) < 0) {
        fail1:
            json_free_parse_record(ctx, pr);
            JS_FreeValue(ctx, root);
            goto fail;
        }

        obj = internalize_json_property(ctx, root, JS_ATOM_empty_string,
                                        reviver, str, pr);
        json_free_parse_record(ctx, pr);
        JS_FreeValue(ctx, root);
    } else {
        obj = JS_ParseJSON_internal(ctx, str, len, "<input>", NULL);
    }
    JS_FreeCString(ctx, str);
    return obj;
 fail:
    JS_FreeCString(ctx, str);
    return JS_EXCEPTION;
}

static JSValue js_json_isRawJSON(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    JSValueConst obj = argv[0];
    if (JS_VALUE_GET_TAG(obj) == JS_TAG_OBJECT) {
        JSObject *p = JS_VALUE_GET_OBJ(obj);
        return js_bool(p->class_id == JS_CLASS_RAWJSON);
    } else {
        return JS_FALSE;
    }
}

static bool is_valid_raw_json_char(int c)
{
    return ((c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' ||
            c == '"');
}

static JSValue js_json_rawJSON(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    JSValue str, res, obj;
    JSString *p;
    str = JS_ToString(ctx, argv[0]);
    if (JS_IsException(str))
        return str;
    p = JS_VALUE_GET_STRING(str);
    if (p->len == 0 ||
        !is_valid_raw_json_char(string_get(p, 0)) ||
        !is_valid_raw_json_char(string_get(p, p->len - 1))) {
        goto syntax_error;
    }
    res = js_json_parse(ctx, JS_UNDEFINED, 1, (JSValueConst *)&str);
    if (JS_IsException(res)) {
    syntax_error:
        JS_ThrowSyntaxError(ctx, "invalid rawJSON string");
        goto fail;
    }
    JS_FreeValue(ctx, res);

    obj = JS_NewObjectProtoClass(ctx, JS_NULL, JS_CLASS_RAWJSON);
    if (JS_IsException(obj))
        goto fail;
    if (JS_DefinePropertyValue(ctx, obj, JS_ATOM_rawJSON, str, JS_PROP_ENUMERABLE) < 0) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    JS_PreventExtensions(ctx, obj);
    return obj;
 fail:
    JS_FreeValue(ctx, str);
    return JS_EXCEPTION;
}

#define JSON_INDEX_KEY_CACHE_SIZE 128

typedef struct JSONStringifyContext {
    JSValueConst replacer_func;
    JSValue property_list;
    JSValue gap;
    JSValue empty;
    StringBuffer *b;
    JSObject **object_stack;
    uint32_t object_stack_len;
    uint32_t object_stack_cap;
    JSValue index_key_cache[JSON_INDEX_KEY_CACHE_SIZE];
} JSONStringifyContext;

static int json_object_stack_contains(const JSONStringifyContext *jsc, const JSObject *object)
{
    uint32_t i;
    for (i = 0; i < jsc->object_stack_len; i++) {
        if (jsc->object_stack[i] == object)
            return 1;
    }
    return 0;
}

static int json_object_stack_push(JSContext *ctx, JSONStringifyContext *jsc, JSObject *object)
{
    JSObject **new_stack;
    uint32_t new_cap;
    if (jsc->object_stack_len == jsc->object_stack_cap) {
        new_cap = jsc->object_stack_cap ? jsc->object_stack_cap * 2 : 16;
        new_stack = js_realloc_rt(ctx->rt, jsc->object_stack,
                                  sizeof(*new_stack) * new_cap);
        if (!new_stack) {
            JS_ThrowOutOfMemory(ctx);
            return -1;
        }
        jsc->object_stack = new_stack;
        jsc->object_stack_cap = new_cap;
    }
    jsc->object_stack[jsc->object_stack_len++] = object;
    return 0;
}

/* Append JSON string quoting directly into the destination buffer.  This
 * avoids allocating a temporary quoted JSString for every object key and
 * string value during JSON.stringify. */
static int json_string_buffer_put_quoted(StringBuffer *b, JSString *p)
{
    int i;
    uint32_t c;
    char buf[7];

    if (string_buffer_putc8(b, '"'))
        return -1;
    for (i = 0; i < p->len;) {
        c = string_getc(p, &i);
        switch (c) {
        case '\t': c = 't'; goto quote;
        case '\r': c = 'r'; goto quote;
        case '\n': c = 'n'; goto quote;
        case '\b': c = 'b'; goto quote;
        case '\f': c = 'f'; goto quote;
        case '"':
        case '\\':
        quote:
            if (string_buffer_putc8(b, '\\') || string_buffer_putc8(b, c))
                return -1;
            break;
        default:
            if (c < 32 || is_surrogate(c)) {
                snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                if (string_buffer_write8(b, (const uint8_t *)buf, 6))
                    return -1;
            } else if (string_buffer_putc(b, c)) {
                return -1;
            }
            break;
        }
    }
    return string_buffer_putc8(b, '"');
}

static JSValue js_json_check(JSContext *ctx, JSONStringifyContext *jsc,
                             JSValueConst holder, JSValue val,
                             JSValueConst key)
{
    JSValue v;
    JSValueConst args[2];

    if (JS_IsObject(val) || JS_IsBigInt(val)) {
		JSValue f = JS_GetProperty(ctx, val, JS_ATOM_toJSON);
		if (JS_IsException(f))
			goto exception;
		if (JS_IsFunction(ctx, f)) {
			v = JS_CallFree(ctx, f, val, 1, &key);
			JS_FreeValue(ctx, val);
			val = v;
			if (JS_IsException(val))
				goto exception;
		} else {
			JS_FreeValue(ctx, f);
		}
	}

    if (!JS_IsUndefined(jsc->replacer_func)) {
        args[0] = key;
        args[1] = val;
        v = JS_Call(ctx, jsc->replacer_func, holder, 2, args);
        JS_FreeValue(ctx, val);
        val = v;
        if (JS_IsException(val))
            goto exception;
    }

    switch (JS_VALUE_GET_NORM_TAG(val)) {
    case JS_TAG_OBJECT:
        if (JS_IsFunction(ctx, val))
            break;
    case JS_TAG_STRING:
    case JS_TAG_STRING_ROPE:
    case JS_TAG_INT:
    case JS_TAG_FLOAT64:
    case JS_TAG_BOOL:
    case JS_TAG_NULL:
    case JS_TAG_SHORT_BIG_INT:
    case JS_TAG_BIG_INT:
    case JS_TAG_EXCEPTION:
        return val;
    default:
        break;
    }
    JS_FreeValue(ctx, val);
    return JS_UNDEFINED;

exception:
    JS_FreeValue(ctx, val);
    return JS_EXCEPTION;
}

static int js_json_to_str(JSContext *ctx, JSONStringifyContext *jsc,
                          JSValueConst holder, JSValue val,
                          JSValueConst indent)
{
    JSValue indent1, sep, sep1, tab, v, prop;
    JSObject *p;
    JSPropertyEnum *native_props;
    uint32_t native_prop_count;
    int64_t i, len;
    int cl, ret;
    bool has_content, use_native_props;

    indent1 = JS_UNDEFINED;
    sep = JS_UNDEFINED;
    sep1 = JS_UNDEFINED;
    tab = JS_UNDEFINED;
    prop = JS_UNDEFINED;
    native_props = NULL;
    native_prop_count = 0;
    use_native_props = false;

    if (js_check_stack_overflow(ctx->rt, 0)) {
        JS_ThrowStackOverflow(ctx);
        goto exception;
    }

    if (JS_IsObject(val)) {
        p = JS_VALUE_GET_OBJ(val);
        cl = p->class_id;
        if (cl == JS_CLASS_STRING) {
            val = JS_ToStringFree(ctx, val);
            if (JS_IsException(val))
                goto exception;
            goto concat_primitive;
        } else if (cl == JS_CLASS_NUMBER) {
            val = JS_ToNumberFree(ctx, val);
            if (JS_IsException(val))
                goto exception;
            goto concat_primitive;
        } else if (cl == JS_CLASS_BOOLEAN || cl == JS_CLASS_BIG_INT) {
            set_value(ctx, &val, js_dup(p->u.object_data));
            goto concat_primitive;
        } else if (cl == JS_CLASS_RAWJSON) {
            JSValue val1;
            val1 = JS_GetProperty(ctx, val, JS_ATOM_rawJSON);
            if (JS_IsException(val1))
                goto exception;
            JS_FreeValue(ctx, val);
            val = val1;
            goto concat_value;
        }
        if (json_object_stack_contains(jsc, p)) {
            JS_ThrowTypeError(ctx, "circular reference");
            goto exception;
        }
        indent1 = JS_ConcatString(ctx, js_dup(indent), js_dup(jsc->gap));
        if (JS_IsException(indent1))
            goto exception;
        if (!JS_IsEmptyString(jsc->gap)) {
            sep = JS_ConcatString3(ctx, "\n", js_dup(indent1), "");
            if (JS_IsException(sep))
                goto exception;
            sep1 = js_new_string8(ctx, " ");
            if (JS_IsException(sep1))
                goto exception;
        } else {
            sep = js_dup(jsc->empty);
            sep1 = js_dup(jsc->empty);
        }
        if (json_object_stack_push(ctx, jsc, p))
            goto exception;
        ret = js_is_array(ctx, val);
        if (ret < 0)
            goto exception;
        if (ret) {
            if (js_get_length64(ctx, &len, val))
                goto exception;
            string_buffer_putc8(jsc->b, '[');
            for(i = 0; i < len; i++) {
                if (i > 0)
                    string_buffer_putc8(jsc->b, ',');
                string_buffer_concat_value(jsc->b, sep);
                v = JS_GetPropertyInt64(ctx, val, i);
                if (JS_IsException(v))
                    goto exception;
                if ((uint64_t)i < JSON_INDEX_KEY_CACHE_SIZE) {
                    if (JS_IsUndefined(jsc->index_key_cache[i])) {
                        jsc->index_key_cache[i] = JS_ToStringFree(ctx, js_int64(i));
                        if (JS_IsException(jsc->index_key_cache[i]))
                            goto exception;
                    }
                    prop = js_dup(jsc->index_key_cache[i]);
                } else {
                    prop = JS_ToStringFree(ctx, js_int64(i));
                    if (JS_IsException(prop))
                        goto exception;
                }
                v = js_json_check(ctx, jsc, val, v, prop);
                JS_FreeValue(ctx, prop);
                prop = JS_UNDEFINED;
                if (JS_IsException(v))
                    goto exception;
                if (JS_IsUndefined(v))
                    v = JS_NULL;
                if (js_json_to_str(ctx, jsc, val, v, indent1))
                    goto exception;
            }
            if (len > 0 && !JS_IsEmptyString(jsc->gap)) {
                string_buffer_putc8(jsc->b, '\n');
                string_buffer_concat_value(jsc->b, indent);
            }
            string_buffer_putc8(jsc->b, ']');
        } else {
            if (!JS_IsUndefined(jsc->property_list)) {
                tab = js_dup(jsc->property_list);
                if (JS_IsException(tab))
                    goto exception;
                if (js_get_length64(ctx, &len, tab))
                    goto exception;
            } else {
                if (JS_GetOwnPropertyNames(ctx, &native_props, &native_prop_count,
                                           val, JS_GPN_ENUM_ONLY | JS_GPN_STRING_MASK) < 0)
                    goto exception;
                len = native_prop_count;
                use_native_props = true;
            }
            string_buffer_putc8(jsc->b, '{');
            has_content = false;
            for(i = 0; i < len; i++) {
                JS_FreeValue(ctx, prop);
                if (use_native_props) {
                    prop = JS_AtomToString(ctx, native_props[i].atom);
                    if (JS_IsException(prop))
                        goto exception;
                    v = JS_GetProperty(ctx, val, native_props[i].atom);
                } else {
                    prop = JS_GetPropertyInt64(ctx, tab, i);
                    if (JS_IsException(prop))
                        goto exception;
                    v = JS_GetPropertyValue(ctx, val, js_dup(prop));
                }
                if (JS_IsException(v))
                    goto exception;
                v = js_json_check(ctx, jsc, val, v, prop);
                if (JS_IsException(v))
                    goto exception;
                if (!JS_IsUndefined(v)) {
                    if (has_content)
                        string_buffer_putc8(jsc->b, ',');
                    string_buffer_concat_value(jsc->b, sep);
                    if (!JS_IsString(prop) ||
                        json_string_buffer_put_quoted(jsc->b, JS_VALUE_GET_STRING(prop))) {
                        JS_FreeValue(ctx, v);
                        goto exception;
                    }
                    string_buffer_putc8(jsc->b, ':');
                    string_buffer_concat_value(jsc->b, sep1);
                    if (js_json_to_str(ctx, jsc, val, v, indent1))
                        goto exception;
                    has_content = true;
                }
            }
            if (has_content && JS_VALUE_GET_STRING(jsc->gap)->len != 0) {
                string_buffer_putc8(jsc->b, '\n');
                string_buffer_concat_value(jsc->b, indent);
            }
            string_buffer_putc8(jsc->b, '}');
        }
        assert(jsc->object_stack_len > 0);
        jsc->object_stack_len--;
        JS_FreeValue(ctx, val);
        JS_FreeValue(ctx, tab);
        JS_FreePropertyEnum(ctx, native_props, native_prop_count);
        JS_FreeValue(ctx, sep);
        JS_FreeValue(ctx, sep1);
        JS_FreeValue(ctx, indent1);
        JS_FreeValue(ctx, prop);
        return 0;
    }
 concat_primitive:
    switch (JS_VALUE_GET_NORM_TAG(val)) {
    case JS_TAG_STRING:
    case JS_TAG_STRING_ROPE:
        if (json_string_buffer_put_quoted(jsc->b, JS_VALUE_GET_STRING(val)))
            goto exception;
        JS_FreeValue(ctx, val);
        return 0;
    case JS_TAG_FLOAT64:
        if (!isfinite(JS_VALUE_GET_FLOAT64(val))) {
            val = JS_NULL;
        }
        goto concat_value;
    case JS_TAG_INT:
    case JS_TAG_BOOL:
    case JS_TAG_NULL:
    concat_value:
        return string_buffer_concat_value_free(jsc->b, val);
    case JS_TAG_SHORT_BIG_INT:
    case JS_TAG_BIG_INT:
        JS_ThrowTypeError(ctx, "BigInt are forbidden in JSON.stringify");
        goto exception;
    default:
        JS_FreeValue(ctx, val);
        return 0;
    }

exception:
    JS_FreeValue(ctx, val);
    JS_FreeValue(ctx, tab);
    JS_FreePropertyEnum(ctx, native_props, native_prop_count);
    JS_FreeValue(ctx, sep);
    JS_FreeValue(ctx, sep1);
    JS_FreeValue(ctx, indent1);
    JS_FreeValue(ctx, prop);
    return -1;
}

JSValue JS_JSONStringify(JSContext *ctx, JSValueConst obj,
                         JSValueConst replacer, JSValueConst space0)
{
    StringBuffer b_s;
    JSONStringifyContext jsc_s, *jsc = &jsc_s;
    JSValue val, v, space, ret, wrapper;
    int res;
    int64_t i, j, n;
    uint32_t cache_index;

    jsc->replacer_func = JS_UNDEFINED;
    jsc->property_list = JS_UNDEFINED;
    jsc->gap = JS_UNDEFINED;
    jsc->b = &b_s;
    jsc->object_stack = NULL;
    jsc->object_stack_len = 0;
    jsc->object_stack_cap = 0;
    for (cache_index = 0; cache_index < JSON_INDEX_KEY_CACHE_SIZE; cache_index++)
        jsc->index_key_cache[cache_index] = JS_UNDEFINED;
    jsc->empty = js_empty_string(ctx->rt);
    ret = JS_UNDEFINED;
    wrapper = JS_UNDEFINED;

    string_buffer_init(ctx, jsc->b, 0);
    if (JS_IsFunction(ctx, replacer)) {
        jsc->replacer_func = replacer;
    } else {
        res = js_is_array(ctx, replacer);
        if (res < 0)
            goto exception;
        if (res) {
            /* XXX: enumeration is not fully correct */
            jsc->property_list = JS_NewArray(ctx);
            if (JS_IsException(jsc->property_list))
                goto exception;
            if (js_get_length64(ctx, &n, replacer))
                goto exception;
            for (i = j = 0; i < n; i++) {
                JSValue present;
                v = JS_GetPropertyInt64(ctx, replacer, i);
                if (JS_IsException(v))
                    goto exception;
                if (JS_IsObject(v)) {
                    JSObject *p = JS_VALUE_GET_OBJ(v);
                    if (p->class_id == JS_CLASS_STRING ||
                        p->class_id == JS_CLASS_NUMBER) {
                        v = JS_ToStringFree(ctx, v);
                        if (JS_IsException(v))
                            goto exception;
                    } else {
                        JS_FreeValue(ctx, v);
                        continue;
                    }
                } else if (JS_IsNumber(v)) {
                    v = JS_ToStringFree(ctx, v);
                    if (JS_IsException(v))
                        goto exception;
                } else if (!JS_IsString(v)) {
                    JS_FreeValue(ctx, v);
                    continue;
                }
                present = js_array_includes(ctx, jsc->property_list,
                                            1, vc(&v));
                if (JS_IsException(present)) {
                    JS_FreeValue(ctx, v);
                    goto exception;
                }
                if (!JS_ToBoolFree(ctx, present)) {
                    JS_SetPropertyInt64(ctx, jsc->property_list, j++, v);
                } else {
                    JS_FreeValue(ctx, v);
                }
            }
        }
    }
    space = js_dup(space0);
    if (JS_IsObject(space)) {
        JSObject *p = JS_VALUE_GET_OBJ(space);
        if (p->class_id == JS_CLASS_NUMBER) {
            space = JS_ToNumberFree(ctx, space);
        } else if (p->class_id == JS_CLASS_STRING) {
            space = JS_ToStringFree(ctx, space);
        }
        if (JS_IsException(space)) {
            JS_FreeValue(ctx, space);
            goto exception;
        }
    }
    if (JS_IsNumber(space)) {
        int n;
        if (JS_ToInt32Clamp(ctx, &n, space, 0, 10, 0))
            goto exception;
        jsc->gap = JS_NewStringLen(ctx, "          ", n);
    } else if (JS_IsString(space)) {
        JSString *p = JS_VALUE_GET_STRING(space);
        jsc->gap = js_sub_string(ctx, p, 0, min_int(p->len, 10));
    } else {
        jsc->gap = js_dup(jsc->empty);
    }
    JS_FreeValue(ctx, space);
    if (JS_IsException(jsc->gap))
        goto exception;
    wrapper = JS_NewObject(ctx);
    if (JS_IsException(wrapper))
        goto exception;
    if (JS_DefinePropertyValue(ctx, wrapper, JS_ATOM_empty_string,
                               js_dup(obj), JS_PROP_C_W_E) < 0)
        goto exception;
    val = js_dup(obj);

    val = js_json_check(ctx, jsc, wrapper, val, jsc->empty);
    if (JS_IsException(val))
        goto exception;
    if (JS_IsUndefined(val)) {
        ret = JS_UNDEFINED;
        goto done1;
    }
    if (js_json_to_str(ctx, jsc, wrapper, val, jsc->empty))
        goto exception;

    ret = string_buffer_end(jsc->b);
    goto done;

exception:
    ret = JS_EXCEPTION;
done1:
    string_buffer_free(jsc->b);
done:
    JS_FreeValue(ctx, wrapper);
    JS_FreeValue(ctx, jsc->empty);
    JS_FreeValue(ctx, jsc->gap);
    JS_FreeValue(ctx, jsc->property_list);
    for (cache_index = 0; cache_index < JSON_INDEX_KEY_CACHE_SIZE; cache_index++)
        JS_FreeValue(ctx, jsc->index_key_cache[cache_index]);
    js_free_rt(ctx->rt, jsc->object_stack);
    return ret;
}

static JSValue js_json_stringify(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    // stringify(val, replacer, space)
    return JS_JSONStringify(ctx, argv[0], argv[1], argv[2]);
}

static const JSCFunctionListEntry js_json_funcs[] = {
    JS_CFUNC_DEF("isRawJSON", 1, js_json_isRawJSON ),
    JS_CFUNC_DEF("parse", 2, js_json_parse ),
    JS_CFUNC_DEF("rawJSON", 1, js_json_rawJSON ),
    JS_CFUNC_DEF("stringify", 3, js_json_stringify ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "JSON", JS_PROP_CONFIGURABLE ),
};

static const JSCFunctionListEntry js_json_obj[] = {
    JS_OBJECT_DEF("JSON", js_json_funcs, countof(js_json_funcs), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE ),
};

static const JSClassShortDef js_json_class_def[] = {
    { JS_ATOM_Object, NULL, NULL }, /* JS_CLASS_RAWJSON */
};

int JS_AddIntrinsicJSON(JSContext *ctx)
{
    if (!JS_IsRegisteredClass(ctx->rt, JS_CLASS_RAWJSON)) {
        if (init_class_range(ctx->rt, js_json_class_def, JS_CLASS_RAWJSON,
                             countof(js_json_class_def)) < 0)
            return -1;
    }
    /* add JSON as autoinit object */
    return JS_SetPropertyFunctionList(ctx, ctx->global_obj, js_json_obj, countof(js_json_obj));
}

/* Reflect */

static JSValue js_reflect_apply(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    return js_function_apply(ctx, argv[0], max_int(0, argc - 1), argv + 1, 2);
}

static JSValue js_reflect_construct(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    JSValueConst func, array_arg, new_target;
    JSValue *tab, ret;
    uint32_t len;

    func = argv[0];
    array_arg = argv[1];
    if (argc > 2) {
        new_target = argv[2];
        if (!JS_IsConstructor(ctx, new_target))
            return JS_ThrowTypeErrorNotAConstructor(ctx, new_target);
    } else {
        new_target = func;
    }
    tab = build_arg_list(ctx, &len, array_arg);
    if (!tab)
        return JS_EXCEPTION;
    ret = JS_CallConstructor2(ctx, func, new_target, len, vc(tab));
    free_arg_list(ctx, tab, len);
    return ret;
}

static JSValue js_reflect_deleteProperty(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    JSValueConst obj;
    JSAtom atom;
    int ret;

    obj = argv[0];
    if (JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT)
        return JS_ThrowTypeErrorNotAnObject(ctx);
    atom = JS_ValueToAtom(ctx, argv[1]);
    if (unlikely(atom == JS_ATOM_NULL))
        return JS_EXCEPTION;
    ret = JS_DeleteProperty(ctx, obj, atom, 0);
    JS_FreeAtom(ctx, atom);
    if (ret < 0)
        return JS_EXCEPTION;
    else
        return js_bool(ret);
}

static JSValue js_reflect_get(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    JSValueConst obj, prop, receiver;
    JSAtom atom;
    JSValue ret;

    obj = argv[0];
    prop = argv[1];
    if (JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT)
        return JS_ThrowTypeErrorNotAnObject(ctx);
    if (argc > 2)
        receiver = argv[2];
    else
        receiver = obj;
    atom = JS_ValueToAtom(ctx, prop);
    if (unlikely(atom == JS_ATOM_NULL))
        return JS_EXCEPTION;
    ret = JS_GetPropertyInternal(ctx, obj, atom, receiver, false);
    JS_FreeAtom(ctx, atom);
    return ret;
}

static JSValue js_reflect_has(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    JSValueConst obj, prop;
    JSAtom atom;
    int ret;

    obj = argv[0];
    prop = argv[1];
    if (JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT)
        return JS_ThrowTypeErrorNotAnObject(ctx);
    atom = JS_ValueToAtom(ctx, prop);
    if (unlikely(atom == JS_ATOM_NULL))
        return JS_EXCEPTION;
    ret = JS_HasProperty(ctx, obj, atom);
    JS_FreeAtom(ctx, atom);
    if (ret < 0)
        return JS_EXCEPTION;
    else
        return js_bool(ret);
}

static JSValue js_reflect_set(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    JSValueConst obj, prop, val, receiver;
    int ret;
    JSAtom atom;

    obj = argv[0];
    prop = argv[1];
    val = argv[2];
    if (argc > 3)
        receiver = argv[3];
    else
        receiver = obj;
    if (JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT)
        return JS_ThrowTypeErrorNotAnObject(ctx);
    atom = JS_ValueToAtom(ctx, prop);
    if (unlikely(atom == JS_ATOM_NULL))
        return JS_EXCEPTION;
    ret = JS_SetPropertyInternal2(ctx, obj, atom, js_dup(val), receiver, 0);
    JS_FreeAtom(ctx, atom);
    if (ret < 0)
        return JS_EXCEPTION;
    else
        return js_bool(ret);
}

static JSValue js_reflect_setPrototypeOf(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    int ret;
    ret = JS_SetPrototypeInternal(ctx, argv[0], argv[1], false);
    if (ret < 0)
        return JS_EXCEPTION;
    else
        return js_bool(ret);
}

static JSValue js_reflect_ownKeys(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    if (JS_VALUE_GET_TAG(argv[0]) != JS_TAG_OBJECT)
        return JS_ThrowTypeErrorNotAnObject(ctx);
    return JS_GetOwnPropertyNames2(ctx, argv[0],
                                   JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK,
                                   JS_ITERATOR_KIND_KEY);
}

static const JSCFunctionListEntry js_reflect_funcs[] = {
    JS_CFUNC_DEF("apply", 3, js_reflect_apply ),
    JS_CFUNC_DEF("construct", 2, js_reflect_construct ),
    JS_CFUNC_MAGIC_DEF("defineProperty", 3, js_object_defineProperty, 1 ),
    JS_CFUNC_DEF("deleteProperty", 2, js_reflect_deleteProperty ),
    JS_CFUNC_DEF("get", 2, js_reflect_get ),
    JS_CFUNC_MAGIC_DEF("getOwnPropertyDescriptor", 2, js_object_getOwnPropertyDescriptor, 1 ),
    JS_CFUNC_MAGIC_DEF("getPrototypeOf", 1, js_object_getPrototypeOf, 1 ),
    JS_CFUNC_DEF("has", 2, js_reflect_has ),
    JS_CFUNC_MAGIC_DEF("isExtensible", 1, js_object_isExtensible, 1 ),
    JS_CFUNC_DEF("ownKeys", 1, js_reflect_ownKeys ),
    JS_CFUNC_MAGIC_DEF("preventExtensions", 1, js_object_preventExtensions, 1 ),
    JS_CFUNC_DEF("set", 3, js_reflect_set ),
    JS_CFUNC_DEF("setPrototypeOf", 2, js_reflect_setPrototypeOf ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Reflect", JS_PROP_CONFIGURABLE ),
};

static const JSCFunctionListEntry js_reflect_obj[] = {
    JS_OBJECT_DEF("Reflect", js_reflect_funcs, countof(js_reflect_funcs), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE ),
};

/* Proxy */

static void js_proxy_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSProxyData *s = JS_GetOpaque(val, JS_CLASS_PROXY);
    if (s) {
        JS_FreeValueRT(rt, s->target);
        JS_FreeValueRT(rt, s->handler);
        js_free_rt(rt, s);
    }
}

static void js_proxy_mark(JSRuntime *rt, JSValueConst val,
                          JS_MarkFunc *mark_func)
{
    JSProxyData *s = JS_GetOpaque(val, JS_CLASS_PROXY);
    if (s) {
        JS_MarkValue(rt, s->target, mark_func);
        JS_MarkValue(rt, s->handler, mark_func);
    }
}

static JSValue JS_ThrowTypeErrorRevokedProxy(JSContext *ctx)
{
    return JS_ThrowTypeError(ctx, "revoked proxy");
}

static JSProxyData *get_proxy_method(JSContext *ctx, JSValue *pmethod,
                                     JSValueConst obj, JSAtom name)
{
    JSProxyData *s = JS_GetOpaque(obj, JS_CLASS_PROXY);
    JSValue method;

    /* safer to test recursion in all proxy methods */
    if (js_check_stack_overflow(ctx->rt, 0)) {
        JS_ThrowStackOverflow(ctx);
        return NULL;
    }

    /* 's' should never be NULL */
    if (s->is_revoked) {
        JS_ThrowTypeErrorRevokedProxy(ctx);
        return NULL;
    }
    method = JS_GetProperty(ctx, s->handler, name);
    if (JS_IsException(method))
        return NULL;
    if (JS_IsNull(method))
        method = JS_UNDEFINED;
    *pmethod = method;
    return s;
}

static JSValue js_proxy_getPrototypeOf(JSContext *ctx, JSValueConst obj)
{
    JSProxyData *s;
    JSValue method, ret, proto1;
    int res;

    s = get_proxy_method(ctx, &method, obj, JS_ATOM_getPrototypeOf);
    if (!s)
        return JS_EXCEPTION;
    if (JS_IsUndefined(method))
        return JS_GetPrototype(ctx, s->target);
    ret = JS_CallFree(ctx, method, s->handler, 1, vc(&s->target));
    if (JS_IsException(ret))
        return ret;
    if (JS_VALUE_GET_TAG(ret) != JS_TAG_NULL &&
        JS_VALUE_GET_TAG(ret) != JS_TAG_OBJECT) {
        goto fail;
    }
    res = JS_IsExtensible(ctx, s->target);
    if (res < 0) {
        JS_FreeValue(ctx, ret);
        return JS_EXCEPTION;
    }
    if (!res) {
        /* check invariant */
        proto1 = JS_GetPrototype(ctx, s->target);
        if (JS_IsException(proto1)) {
            JS_FreeValue(ctx, ret);
            return JS_EXCEPTION;
        }
        if (JS_VALUE_GET_OBJ(proto1) != JS_VALUE_GET_OBJ(ret)) {
            JS_FreeValue(ctx, proto1);
        fail:
            JS_FreeValue(ctx, ret);
            return JS_ThrowTypeError(ctx, "proxy: inconsistent prototype");
        }
        JS_FreeValue(ctx, proto1);
    }
    return ret;
}

static int js_proxy_setPrototypeOf(JSContext *ctx, JSValueConst obj,
                                   JSValueConst proto_val, bool throw_flag)
{
    JSProxyData *s;
    JSValue method, ret, proto1;
    JSValueConst args[2];
    bool res;
    int res2;

    s = get_proxy_method(ctx, &method, obj, JS_ATOM_setPrototypeOf);
    if (!s)
        return -1;
    if (JS_IsUndefined(method))
        return JS_SetPrototypeInternal(ctx, s->target, proto_val, throw_flag);
    args[0] = s->target;
    args[1] = proto_val;
    ret = JS_CallFree(ctx, method, s->handler, 2, args);
    if (JS_IsException(ret))
        return -1;
    res = JS_ToBoolFree(ctx, ret);
    if (!res) {
        if (throw_flag) {
            JS_ThrowTypeError(ctx, "proxy: bad prototype");
            return -1;
        } else {
            return false;
        }
    }
    res2 = JS_IsExtensible(ctx, s->target);
    if (res2 < 0)
        return -1;
    if (!res2) {
        proto1 = JS_GetPrototype(ctx, s->target);
        if (JS_IsException(proto1))
            return -1;
        if (JS_VALUE_GET_OBJ(proto_val) != JS_VALUE_GET_OBJ(proto1)) {
            JS_FreeValue(ctx, proto1);
            JS_ThrowTypeError(ctx, "proxy: inconsistent prototype");
            return -1;
        }
        JS_FreeValue(ctx, proto1);
    }
    return true;
}

