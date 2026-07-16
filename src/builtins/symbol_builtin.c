/* Engine domain source: builtins/regexp_json_reflect_proxy_symbol.inc -> symbol_builtin.
 * Ownership: builtins subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static int js_proxy_isExtensible(JSContext *ctx, JSValueConst obj)
{
    JSProxyData *s;
    JSValue method, ret;
    bool res;
    int res2;

    s = get_proxy_method(ctx, &method, obj, JS_ATOM_isExtensible);
    if (!s)
        return -1;
    if (JS_IsUndefined(method))
        return JS_IsExtensible(ctx, s->target);
    ret = JS_CallFree(ctx, method, s->handler, 1, vc(&s->target));
    if (JS_IsException(ret))
        return -1;
    res = JS_ToBoolFree(ctx, ret);
    res2 = JS_IsExtensible(ctx, s->target);
    if (res2 < 0)
        return res2;
    if (res != res2) {
        JS_ThrowTypeError(ctx, "proxy: inconsistent isExtensible");
        return -1;
    }
    return res;
}

static int js_proxy_preventExtensions(JSContext *ctx, JSValueConst obj)
{
    JSProxyData *s;
    JSValue method, ret;
    bool res;
    int res2;

    s = get_proxy_method(ctx, &method, obj, JS_ATOM_preventExtensions);
    if (!s)
        return -1;
    if (JS_IsUndefined(method))
        return JS_PreventExtensions(ctx, s->target);
    ret = JS_CallFree(ctx, method, s->handler, 1, vc(&s->target));
    if (JS_IsException(ret))
        return -1;
    res = JS_ToBoolFree(ctx, ret);
    if (res) {
        res2 = JS_IsExtensible(ctx, s->target);
        if (res2 < 0)
            return res2;
        if (res2) {
            JS_ThrowTypeError(ctx, "proxy: inconsistent preventExtensions");
            return -1;
        }
    }
    return res;
}

static int js_proxy_has(JSContext *ctx, JSValueConst obj, JSAtom atom)
{
    JSProxyData *s;
    JSValue method, ret1, atom_val;
    int res;
    JSObject *p;
    JSValueConst args[2];
    bool ret, res2;

    s = get_proxy_method(ctx, &method, obj, JS_ATOM_has);
    if (!s)
        return -1;
    if (JS_IsUndefined(method))
        return JS_HasProperty(ctx, s->target, atom);
    atom_val = JS_AtomToValue(ctx, atom);
    if (JS_IsException(atom_val)) {
        JS_FreeValue(ctx, method);
        return -1;
    }
    args[0] = s->target;
    args[1] = atom_val;
    ret1 = JS_CallFree(ctx, method, s->handler, 2, args);
    JS_FreeValue(ctx, atom_val);
    if (JS_IsException(ret1))
        return -1;
    ret = JS_ToBoolFree(ctx, ret1);
    if (!ret) {
        int desc_flags;
        p = JS_VALUE_GET_OBJ(s->target);
        res = JS_GetOwnPropertyFlagsInternal(ctx, &desc_flags, p, atom);
        if (res < 0)
            return -1;
        if (res) {
            res2 = !(desc_flags & JS_PROP_CONFIGURABLE);
            if (res2 || !p->extensible) {
                JS_ThrowTypeError(ctx, "proxy: inconsistent has");
                return -1;
            }
        }
    }
    return ret;
}

static JSValue js_proxy_get(JSContext *ctx, JSValueConst obj, JSAtom atom,
                            JSValueConst receiver)
{
    JSProxyData *s;
    JSValue method, ret, atom_val;
    int res;
    JSValueConst args[3];
    JSPropertyDescriptor desc;

    s = get_proxy_method(ctx, &method, obj, JS_ATOM_get);
    if (!s)
        return JS_EXCEPTION;
    /* Note: recursion is possible thru the prototype of s->target */
    if (JS_IsUndefined(method))
        return JS_GetPropertyInternal(ctx, s->target, atom, receiver, false);
    atom_val = JS_AtomToValue(ctx, atom);
    if (JS_IsException(atom_val)) {
        JS_FreeValue(ctx, method);
        return JS_EXCEPTION;
    }
    args[0] = s->target;
    args[1] = atom_val;
    args[2] = receiver;
    ret = JS_CallFree(ctx, method, s->handler, 3, args);
    JS_FreeValue(ctx, atom_val);
    if (JS_IsException(ret))
        return JS_EXCEPTION;
    res = JS_GetOwnPropertyInternal(ctx, &desc, JS_VALUE_GET_OBJ(s->target), atom);
    if (res < 0) {
        JS_FreeValue(ctx, ret);
        return JS_EXCEPTION;
    }
    if (res) {
        if ((desc.flags & (JS_PROP_GETSET | JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE)) == 0) {
            if (!js_same_value(ctx, desc.value, ret)) {
                goto fail;
            }
        } else if ((desc.flags & (JS_PROP_GETSET | JS_PROP_CONFIGURABLE)) == JS_PROP_GETSET) {
            if (JS_IsUndefined(desc.getter) && !JS_IsUndefined(ret)) {
            fail:
                js_free_desc(ctx, &desc);
                JS_FreeValue(ctx, ret);
                return JS_ThrowTypeError(ctx, "proxy: inconsistent get");
            }
        }
        js_free_desc(ctx, &desc);
    }
    return ret;
}

static int js_proxy_set(JSContext *ctx, JSValueConst obj, JSAtom atom,
                        JSValueConst value, JSValueConst receiver, int flags)
{
    JSProxyData *s;
    JSValue method, ret1, atom_val;
    bool ret;
    int res;
    JSValueConst args[4];

    s = get_proxy_method(ctx, &method, obj, JS_ATOM_set);
    if (!s)
        return -1;
    if (JS_IsUndefined(method)) {
        return JS_SetPropertyInternal2(ctx, s->target, atom,
                                       js_dup(value), receiver, flags);
    }
    atom_val = JS_AtomToValue(ctx, atom);
    if (JS_IsException(atom_val)) {
        JS_FreeValue(ctx, method);
        return -1;
    }
    args[0] = s->target;
    args[1] = atom_val;
    args[2] = value;
    args[3] = receiver;
    ret1 = JS_CallFree(ctx, method, s->handler, 4, args);
    JS_FreeValue(ctx, atom_val);
    if (JS_IsException(ret1))
        return -1;
    ret = JS_ToBoolFree(ctx, ret1);
    if (ret) {
        JSPropertyDescriptor desc;
        res = JS_GetOwnPropertyInternal(ctx, &desc, JS_VALUE_GET_OBJ(s->target), atom);
        if (res < 0)
            return -1;
        if (res) {
            if ((desc.flags & (JS_PROP_GETSET | JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE)) == 0) {
                if (!js_same_value(ctx, desc.value, value)) {
                    goto fail;
                }
            } else if ((desc.flags & (JS_PROP_GETSET | JS_PROP_CONFIGURABLE)) == JS_PROP_GETSET && JS_IsUndefined(desc.setter)) {
                fail:
                    js_free_desc(ctx, &desc);
                    JS_ThrowTypeError(ctx, "proxy: inconsistent set");
                    return -1;
            }
            js_free_desc(ctx, &desc);
        }
    } else {
        if ((flags & JS_PROP_THROW) ||
            ((flags & JS_PROP_THROW_STRICT) && is_strict_mode(ctx))) {
            JS_ThrowTypeError(ctx, "proxy: cannot set property");
            return -1;
        }
    }
    return ret;
}

static JSValue js_create_desc(JSContext *ctx, JSValueConst val,
                              JSValueConst getter, JSValueConst setter,
                              int flags)
{
    JSValue ret;
    ret = JS_NewObject(ctx);
    if (JS_IsException(ret))
        return ret;
    if (flags & JS_PROP_HAS_GET) {
        JS_DefinePropertyValueConst(ctx, ret, JS_ATOM_get, getter,
                                    JS_PROP_C_W_E);
    }
    if (flags & JS_PROP_HAS_SET) {
        JS_DefinePropertyValueConst(ctx, ret, JS_ATOM_set, setter,
                                    JS_PROP_C_W_E);
    }
    if (flags & JS_PROP_HAS_VALUE) {
        JS_DefinePropertyValueConst(ctx, ret, JS_ATOM_value, val,
                                    JS_PROP_C_W_E);
    }
    if (flags & JS_PROP_HAS_WRITABLE) {
        JS_DefinePropertyValue(ctx, ret, JS_ATOM_writable,
                               js_bool(flags & JS_PROP_WRITABLE),
                               JS_PROP_C_W_E);
    }
    if (flags & JS_PROP_HAS_ENUMERABLE) {
        JS_DefinePropertyValue(ctx, ret, JS_ATOM_enumerable,
                               js_bool(flags & JS_PROP_ENUMERABLE),
                               JS_PROP_C_W_E);
    }
    if (flags & JS_PROP_HAS_CONFIGURABLE) {
        JS_DefinePropertyValue(ctx, ret, JS_ATOM_configurable,
                               js_bool(flags & JS_PROP_CONFIGURABLE),
                               JS_PROP_C_W_E);
    }
    return ret;
}

static int js_proxy_get_own_property(JSContext *ctx, JSPropertyDescriptor *pdesc,
                                     JSValueConst obj, JSAtom prop)
{
    JSProxyData *s;
    JSValue method, trap_result_obj, prop_val;
    int res, target_desc_ret, ret;
    JSObject *p;
    JSValueConst args[2];
    JSPropertyDescriptor result_desc, target_desc;

    s = get_proxy_method(ctx, &method, obj, JS_ATOM_getOwnPropertyDescriptor);
    if (!s)
        return -1;
    p = JS_VALUE_GET_OBJ(s->target);
    if (JS_IsUndefined(method)) {
        return JS_GetOwnPropertyInternal(ctx, pdesc, p, prop);
    }
    prop_val = JS_AtomToValue(ctx, prop);
    if (JS_IsException(prop_val)) {
        JS_FreeValue(ctx, method);
        return -1;
    }
    args[0] = s->target;
    args[1] = prop_val;
    trap_result_obj = JS_CallFree(ctx, method, s->handler, 2, args);
    JS_FreeValue(ctx, prop_val);
    if (JS_IsException(trap_result_obj))
        return -1;
    if (!JS_IsObject(trap_result_obj) && !JS_IsUndefined(trap_result_obj)) {
        JS_FreeValue(ctx, trap_result_obj);
        goto fail;
    }
    target_desc_ret = JS_GetOwnPropertyInternal(ctx, &target_desc, p, prop);
    if (target_desc_ret < 0) {
        JS_FreeValue(ctx, trap_result_obj);
        return -1;
    }
    if (target_desc_ret)
        js_free_desc(ctx, &target_desc);
    if (JS_IsUndefined(trap_result_obj)) {
        if (target_desc_ret) {
            if (!(target_desc.flags & JS_PROP_CONFIGURABLE) || !p->extensible)
                goto fail;
        }
        ret = false;
    } else {
        int flags1, extensible_target;
        extensible_target = JS_IsExtensible(ctx, s->target);
        if (extensible_target < 0) {
            JS_FreeValue(ctx, trap_result_obj);
            return -1;
        }
        res = js_obj_to_desc(ctx, &result_desc, trap_result_obj);
        JS_FreeValue(ctx, trap_result_obj);
        if (res < 0)
            return -1;

        if (target_desc_ret) {
            /* convert result_desc.flags to defineProperty flags */
            flags1 = result_desc.flags | JS_PROP_HAS_CONFIGURABLE | JS_PROP_HAS_ENUMERABLE;
            if (result_desc.flags & JS_PROP_GETSET)
                flags1 |= JS_PROP_HAS_GET | JS_PROP_HAS_SET;
            else
                flags1 |= JS_PROP_HAS_VALUE | JS_PROP_HAS_WRITABLE;
            /* XXX: not complete check: need to compare value &
               getter/setter as in defineproperty */
            if (!check_define_prop_flags(target_desc.flags, flags1))
                goto fail1;
        } else {
            if (!extensible_target)
                goto fail1;
        }
        if (!(result_desc.flags & JS_PROP_CONFIGURABLE)) {
            if (!target_desc_ret || (target_desc.flags & JS_PROP_CONFIGURABLE))
                goto fail1;
            if ((result_desc.flags &
                 (JS_PROP_GETSET | JS_PROP_WRITABLE)) == 0 &&
                target_desc_ret &&
                (target_desc.flags & JS_PROP_WRITABLE) != 0) {
                /* proxy-missing-checks */
            fail1:
                js_free_desc(ctx, &result_desc);
            fail:
                JS_ThrowTypeError(ctx, "proxy: inconsistent getOwnPropertyDescriptor");
                return -1;
            }
        }
        ret = true;
        if (pdesc) {
            *pdesc = result_desc;
        } else {
            js_free_desc(ctx, &result_desc);
        }
    }
    return ret;
}

static int js_proxy_define_own_property(JSContext *ctx, JSValueConst obj,
                                        JSAtom prop, JSValueConst val,
                                        JSValueConst getter,
                                        JSValueConst setter, int flags)
{
    JSProxyData *s;
    JSValue method, ret1, prop_val, desc_val;
    int res;
    JSObject *p;
    JSValueConst args[3];
    JSPropertyDescriptor desc;
    bool ret, setting_not_configurable;

    s = get_proxy_method(ctx, &method, obj, JS_ATOM_defineProperty);
    if (!s)
        return -1;
    if (JS_IsUndefined(method)) {
        return JS_DefineProperty(ctx, s->target, prop, val, getter, setter, flags);
    }
    prop_val = JS_AtomToValue(ctx, prop);
    if (JS_IsException(prop_val)) {
        JS_FreeValue(ctx, method);
        return -1;
    }
    desc_val = js_create_desc(ctx, val, getter, setter, flags);
    if (JS_IsException(desc_val)) {
        JS_FreeValue(ctx, prop_val);
        JS_FreeValue(ctx, method);
        return -1;
    }
    args[0] = s->target;
    args[1] = prop_val;
    args[2] = desc_val;
    ret1 = JS_CallFree(ctx, method, s->handler, 3, args);
    JS_FreeValue(ctx, prop_val);
    JS_FreeValue(ctx, desc_val);
    if (JS_IsException(ret1))
        return -1;
    ret = JS_ToBoolFree(ctx, ret1);
    if (!ret) {
        if (flags & JS_PROP_THROW) {
            JS_ThrowTypeError(ctx, "proxy: defineProperty exception");
            return -1;
        } else {
            return 0;
        }
    }
    p = JS_VALUE_GET_OBJ(s->target);
    res = JS_GetOwnPropertyInternal(ctx, &desc, p, prop);
    if (res < 0)
        return -1;
    setting_not_configurable = ((flags & (JS_PROP_HAS_CONFIGURABLE |
                                          JS_PROP_CONFIGURABLE)) ==
                                JS_PROP_HAS_CONFIGURABLE);
    if (!res) {
        if (!p->extensible || setting_not_configurable)
            goto fail;
    } else {
        if (!check_define_prop_flags(desc.flags, flags) ||
            ((desc.flags & JS_PROP_CONFIGURABLE) && setting_not_configurable)) {
            goto fail1;
        }
        if (flags & (JS_PROP_HAS_GET | JS_PROP_HAS_SET)) {
            if ((desc.flags & (JS_PROP_GETSET | JS_PROP_CONFIGURABLE)) ==
                JS_PROP_GETSET) {
                if ((flags & JS_PROP_HAS_GET) &&
                    !js_same_value(ctx, getter, desc.getter)) {
                    goto fail1;
                }
                if ((flags & JS_PROP_HAS_SET) &&
                    !js_same_value(ctx, setter, desc.setter)) {
                    goto fail1;
                }
            }
        } else if (flags & JS_PROP_HAS_VALUE) {
            if ((desc.flags & (JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE)) ==
                JS_PROP_WRITABLE && !(flags & JS_PROP_WRITABLE)) {
                /* missing-proxy-check feature */
                goto fail1;
            } else if ((desc.flags & (JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE)) == 0 &&
                !js_same_value(ctx, val, desc.value)) {
                goto fail1;
            }
        }
        if (flags & JS_PROP_HAS_WRITABLE) {
            if ((desc.flags & (JS_PROP_GETSET | JS_PROP_CONFIGURABLE |
                               JS_PROP_WRITABLE)) == JS_PROP_WRITABLE) {
                /* proxy-missing-checks */
            fail1:
                js_free_desc(ctx, &desc);
            fail:
                JS_ThrowTypeError(ctx, "proxy: inconsistent defineProperty");
                return -1;
            }
        }
        js_free_desc(ctx, &desc);
    }
    return 1;
}

static int js_proxy_delete_property(JSContext *ctx, JSValueConst obj,
                                    JSAtom atom)
{
    JSProxyData *s;
    JSValue method, ret, atom_val;
    int res2, is_extensible;
    bool res;
    JSValueConst args[2];

    s = get_proxy_method(ctx, &method, obj, JS_ATOM_deleteProperty);
    if (!s)
        return -1;
    if (JS_IsUndefined(method)) {
        return JS_DeleteProperty(ctx, s->target, atom, 0);
    }
    atom_val = JS_AtomToValue(ctx, atom);;
    if (JS_IsException(atom_val)) {
        JS_FreeValue(ctx, method);
        return -1;
    }
    args[0] = s->target;
    args[1] = atom_val;
    ret = JS_CallFree(ctx, method, s->handler, 2, args);
    JS_FreeValue(ctx, atom_val);
    if (JS_IsException(ret))
        return -1;
    res = JS_ToBoolFree(ctx, ret);
    if (res) {
        int desc_flags;
        res2 = JS_GetOwnPropertyFlagsInternal(ctx, &desc_flags, JS_VALUE_GET_OBJ(s->target), atom);
        if (res2 < 0)
            return -1;
        if (res2) {
            if (!(desc_flags & JS_PROP_CONFIGURABLE)) {
                JS_ThrowTypeError(ctx, "proxy: inconsistent deleteProperty");
                return -1;
            }
            is_extensible = JS_IsExtensible(ctx, s->target);
            if (is_extensible < 0)
                return -1;
            if (!is_extensible) {
                /* proxy-missing-checks */
                JS_ThrowTypeError(ctx, "proxy: inconsistent deleteProperty");
                return -1;
            }
        }
    }
    return res;
}

/* return the index of the property or -1 if not found */
static int find_prop_key(const JSPropertyEnum *tab, int n, JSAtom atom)
{
    int i;
    for(i = 0; i < n; i++) {
        if (tab[i].atom == atom)
            return i;
    }
    return -1;
}

static int js_proxy_get_own_property_names(JSContext *ctx,
                                           JSPropertyEnum **ptab,
                                           uint32_t *plen,
                                           JSValueConst obj)
{
    JSProxyData *s;
    JSValue method, prop_array, val;
    uint32_t len, i, len2;
    JSPropertyEnum *tab, *tab2;
    JSAtom atom;
    int res, is_extensible, idx;

    s = get_proxy_method(ctx, &method, obj, JS_ATOM_ownKeys);
    if (!s)
        return -1;
    if (JS_IsUndefined(method)) {
        return JS_GetOwnPropertyNamesInternal(ctx, ptab, plen,
                                      JS_VALUE_GET_OBJ(s->target),
                                      JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK);
    }
    prop_array = JS_CallFree(ctx, method, s->handler, 1, vc(&s->target));
    if (JS_IsException(prop_array))
        return -1;
    tab = NULL;
    len = 0;
    tab2 = NULL;
    len2 = 0;
    if (js_get_length32(ctx, &len, prop_array))
        goto fail;
    if (len > 0) {
        tab = js_mallocz(ctx, sizeof(tab[0]) * len);
        if (!tab)
            goto fail;
    }
    for(i = 0; i < len; i++) {
        val = JS_GetPropertyUint32(ctx, prop_array, i);
        if (JS_IsException(val))
            goto fail;
        if (!JS_IsString(val) && !JS_IsSymbol(val)) {
            JS_FreeValue(ctx, val);
            JS_ThrowTypeError(ctx, "proxy: properties must be strings or symbols");
            goto fail;
        }
        atom = JS_ValueToAtom(ctx, val);
        JS_FreeValue(ctx, val);
        if (atom == JS_ATOM_NULL)
            goto fail;
        tab[i].atom = atom;
        tab[i].is_enumerable = false; /* XXX: redundant? */
    }

    /* check duplicate properties (XXX: inefficient, could store the
     * properties an a temporary object to use the hash) */
    for(i = 1; i < len; i++) {
        if (find_prop_key(tab, i, tab[i].atom) >= 0) {
            JS_ThrowTypeError(ctx, "proxy: duplicate property");
            goto fail;
        }
    }

    is_extensible = JS_IsExtensible(ctx, s->target);
    if (is_extensible < 0)
        goto fail;

    /* check if there are non configurable properties */
    if (s->is_revoked) {
        JS_ThrowTypeErrorRevokedProxy(ctx);
        goto fail;
    }
    if (JS_GetOwnPropertyNamesInternal(ctx, &tab2, &len2, JS_VALUE_GET_OBJ(s->target),
                               JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK))
        goto fail;
    for(i = 0; i < len2; i++) {
        if (s->is_revoked) {
            JS_ThrowTypeErrorRevokedProxy(ctx);
            goto fail;
        }
        {
            int desc_flags;
            res = JS_GetOwnPropertyFlagsInternal(ctx, &desc_flags, JS_VALUE_GET_OBJ(s->target),
                                    tab2[i].atom);
            if (res < 0)
                goto fail;
            if (res) {  /* safety, property should be found */
                if (!(desc_flags & JS_PROP_CONFIGURABLE) || !is_extensible) {
                    idx = find_prop_key(tab, len, tab2[i].atom);
                    if (idx < 0) {
                        JS_ThrowTypeError(ctx, "proxy: target property must be present in proxy ownKeys");
                        goto fail;
                    }
                    /* mark the property as found */
                    if (!is_extensible)
                        tab[idx].is_enumerable = true;
                }
            }
        }
    }
    if (!is_extensible) {
        /* check that all property in 'tab' were checked */
        for(i = 0; i < len; i++) {
            if (!tab[i].is_enumerable) {
                JS_ThrowTypeError(ctx, "proxy: property not present in target were returned by non extensible proxy");
                goto fail;
            }
        }
    }

    js_free_prop_enum(ctx, tab2, len2);
    JS_FreeValue(ctx, prop_array);
    *ptab = tab;
    *plen = len;
    return 0;
 fail:
    js_free_prop_enum(ctx, tab2, len2);
    js_free_prop_enum(ctx, tab, len);
    JS_FreeValue(ctx, prop_array);
    return -1;
}

static JSValue js_proxy_call_constructor(JSContext *ctx, JSValueConst func_obj,
                                         JSValueConst new_target,
                                         int argc, JSValueConst *argv)
{
    JSProxyData *s;
    JSValue method, arg_array, ret;
    JSValueConst args[3];

    s = get_proxy_method(ctx, &method, func_obj, JS_ATOM_construct);
    if (!s)
        return JS_EXCEPTION;
    if (!JS_IsConstructor(ctx, s->target))
        return JS_ThrowTypeErrorNotAConstructor(ctx, s->target);
    if (JS_IsUndefined(method))
        return JS_CallConstructor2(ctx, s->target, new_target, argc, argv);
    arg_array = js_create_array(ctx, argc, argv);
    if (JS_IsException(arg_array)) {
        ret = JS_EXCEPTION;
        goto fail;
    }
    args[0] = s->target;
    args[1] = arg_array;
    args[2] = new_target;
    ret = JS_Call(ctx, method, s->handler, 3, args);
    if (!JS_IsException(ret) && JS_VALUE_GET_TAG(ret) != JS_TAG_OBJECT) {
        JS_FreeValue(ctx, ret);
        ret = JS_ThrowTypeErrorNotAnObject(ctx);
    }
 fail:
    JS_FreeValue(ctx, method);
    JS_FreeValue(ctx, arg_array);
    return ret;
}

static JSValue js_proxy_call(JSContext *ctx, JSValueConst func_obj,
                             JSValueConst this_obj,
                             int argc, JSValueConst *argv, int flags)
{
    JSProxyData *s;
    JSValue method, arg_array, ret;
    JSValueConst args[3];

    if (flags & JS_CALL_FLAG_CONSTRUCTOR)
        return js_proxy_call_constructor(ctx, func_obj, this_obj, argc, argv);

    s = get_proxy_method(ctx, &method, func_obj, JS_ATOM_apply);
    if (!s)
        return JS_EXCEPTION;
    if (!s->is_func) {
        JS_FreeValue(ctx, method);
        return JS_ThrowTypeErrorNotAFunction(ctx);
    }
    if (JS_IsUndefined(method))
        return JS_Call(ctx, s->target, this_obj, argc, argv);
    arg_array = js_create_array(ctx, argc, argv);
    if (JS_IsException(arg_array)) {
        ret = JS_EXCEPTION;
        goto fail;
    }
    args[0] = s->target;
    args[1] = this_obj;
    args[2] = arg_array;
    ret = JS_Call(ctx, method, s->handler, 3, args);
 fail:
    JS_FreeValue(ctx, method);
    JS_FreeValue(ctx, arg_array);
    return ret;
}

static int js_proxy_isArray(JSContext *ctx, JSValueConst obj)
{
    JSProxyData *s = JS_GetOpaque(obj, JS_CLASS_PROXY);
    if (!s)
        return false;

    if (js_check_stack_overflow(ctx->rt, 0)) {
        JS_ThrowStackOverflow(ctx);
        return -1;
    }

    if (s->is_revoked) {
        JS_ThrowTypeErrorRevokedProxy(ctx);
        return -1;
    }
    return js_is_array(ctx, s->target);
}

static const JSClassExoticMethods js_proxy_exotic_methods = {
    .get_own_property = js_proxy_get_own_property,
    .define_own_property = js_proxy_define_own_property,
    .delete_property = js_proxy_delete_property,
    .get_own_property_names = js_proxy_get_own_property_names,
    .has_property = js_proxy_has,
    .get_property = js_proxy_get,
    .set_property = js_proxy_set,
};

JSValue JS_NewProxy(JSContext *ctx, JSValueConst target, JSValueConst handler)
{
    JSProxyData *s;
    JSValue obj;

    if (JS_VALUE_GET_TAG(target) != JS_TAG_OBJECT ||
        JS_VALUE_GET_TAG(handler) != JS_TAG_OBJECT) {
        return JS_ThrowTypeErrorNotAnObject(ctx);
    }
    obj = JS_NewObjectProtoClass(ctx, JS_NULL, JS_CLASS_PROXY);
    if (JS_IsException(obj))
        return obj;
    s = js_malloc(ctx, sizeof(*s));
    if (!s) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    s->target = js_dup(target);
    s->handler = js_dup(handler);
    s->is_func = JS_IsFunction(ctx, target);
    s->is_revoked = false;
    JS_SetOpaqueInternal(obj, s);
    JS_SetConstructorBit(ctx, obj, JS_IsConstructor(ctx, target));
    return obj;
}

static JSValue js_proxy_constructor(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    return JS_NewProxy(ctx, argv[0], argv[1]);
}

static JSValue js_proxy_revoke(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv, int magic,
                               JSValueConst *func_data)
{
    JSProxyData *s = JS_GetOpaque(func_data[0], JS_CLASS_PROXY);
    if (s) {
        /* We do not free the handler and target in case they are
           referenced as constants in the C call stack */
        s->is_revoked = true;
        JS_FreeValue(ctx, unsafe_unconst(func_data[0]));
        func_data[0] = JS_NULL;
    }
    return JS_UNDEFINED;
}

static JSValue js_proxy_revoke_constructor(JSContext *ctx,
                                           JSValueConst proxy_obj)
{
    return JS_NewCFunctionData(ctx, js_proxy_revoke, 0, 0, 1, &proxy_obj);
}

static JSValue js_proxy_revocable(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    JSValue proxy_obj, revoke_obj = JS_UNDEFINED, obj;

    proxy_obj = js_proxy_constructor(ctx, JS_UNDEFINED, argc, argv);
    if (JS_IsException(proxy_obj))
        goto fail;
    revoke_obj = js_proxy_revoke_constructor(ctx, proxy_obj);
    if (JS_IsException(revoke_obj))
        goto fail;
    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        goto fail;
    // XXX: exceptions?
    JS_DefinePropertyValue(ctx, obj, JS_ATOM_proxy, proxy_obj, JS_PROP_C_W_E);
    JS_DefinePropertyValue(ctx, obj, JS_ATOM_revoke, revoke_obj, JS_PROP_C_W_E);
    return obj;
 fail:
    JS_FreeValue(ctx, proxy_obj);
    JS_FreeValue(ctx, revoke_obj);
    return JS_EXCEPTION;
}

static const JSCFunctionListEntry js_proxy_funcs[] = {
    JS_CFUNC_DEF("revocable", 2, js_proxy_revocable ),
};

static const JSClassShortDef js_proxy_class_def[] = {
    { JS_ATOM_Object, js_proxy_finalizer, js_proxy_mark }, /* JS_CLASS_PROXY */
};

int JS_AddIntrinsicProxy(JSContext *ctx)
{
    JSRuntime *rt = ctx->rt;
    JSValue obj1;

    if (!JS_IsRegisteredClass(rt, JS_CLASS_PROXY)) {
        if (init_class_range(rt, js_proxy_class_def, JS_CLASS_PROXY,
                             countof(js_proxy_class_def)))
            return -1;
        rt->class_array[JS_CLASS_PROXY].exotic = &js_proxy_exotic_methods;
        rt->class_array[JS_CLASS_PROXY].call = js_proxy_call;
    }

    /* additional fields: name, length */
    obj1 = JS_NewCFunction3(ctx, js_proxy_constructor, "Proxy", 2,
                            JS_CFUNC_constructor, 0,
                            ctx->function_proto, countof(js_proxy_funcs) + 2);
    if (JS_IsException(obj1))
        return -1;
    JS_SetConstructorBit(ctx, obj1, true);
    if (JS_SetPropertyFunctionList(ctx, obj1, js_proxy_funcs,
                                   countof(js_proxy_funcs)))
        goto fail;
    if (JS_DefinePropertyValueStr(ctx, ctx->global_obj, "Proxy",
                                  obj1, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE) < 0)
        goto fail;
    return 0;
 fail:
    JS_FreeValue(ctx, obj1);
    return -1;
}

bool JS_IsProxy(JSValueConst val)
{
    if (JS_VALUE_GET_TAG(val) == JS_TAG_OBJECT) {
        JSObject *p = JS_VALUE_GET_OBJ(val);
        return p->class_id == JS_CLASS_PROXY;
    }
    return false;
}

static JSValue js_get_proxy_field(JSContext *ctx, JSValueConst proxy,
                                  int offset)
{
    if (JS_VALUE_GET_TAG(proxy) == JS_TAG_OBJECT) {
        JSObject *p = JS_VALUE_GET_OBJ(proxy);
        if (p->class_id == JS_CLASS_PROXY) {
            JSProxyData *s = JS_GetOpaque(proxy, JS_CLASS_PROXY);
            if (s->is_revoked)
                return JS_ThrowTypeErrorRevokedProxy(ctx);
            return js_dup(*(JSValue *)((char *)s + offset));
        }
    }
    return JS_ThrowTypeError(ctx, "not a proxy");
}

JSValue JS_GetProxyTarget(JSContext *ctx, JSValueConst proxy)
{
    return js_get_proxy_field(ctx, proxy, offsetof(JSProxyData, target));
}

JSValue JS_GetProxyHandler(JSContext *ctx, JSValueConst proxy)
{
    return js_get_proxy_field(ctx, proxy, offsetof(JSProxyData, handler));
}

/* Symbol */

static JSValue js_symbol_constructor(JSContext *ctx, JSValueConst new_target,
                                     int argc, JSValueConst *argv)
{
    JSValue str;
    JSString *p;

    if (!JS_IsUndefined(new_target))
        return JS_ThrowTypeErrorNotAConstructor(ctx, new_target);
    if (argc == 0 || JS_IsUndefined(argv[0])) {
        p = NULL;
    } else {
        str = JS_ToString(ctx, argv[0]);
        if (JS_IsException(str))
            return JS_EXCEPTION;
        p = JS_VALUE_GET_STRING(str);
    }
    return JS_NewSymbolInternal(ctx, p, JS_ATOM_TYPE_SYMBOL);
}

static JSValue js_thisSymbolValue(JSContext *ctx, JSValueConst this_val)
{
    if (JS_VALUE_GET_TAG(this_val) == JS_TAG_SYMBOL)
        return js_dup(this_val);

    if (JS_VALUE_GET_TAG(this_val) == JS_TAG_OBJECT) {
        JSObject *p = JS_VALUE_GET_OBJ(this_val);
        if (p->class_id == JS_CLASS_SYMBOL) {
            if (JS_VALUE_GET_TAG(p->u.object_data) == JS_TAG_SYMBOL)
                return js_dup(p->u.object_data);
        }
    }
    return JS_ThrowTypeError(ctx, "not a symbol");
}

static JSValue js_symbol_toString(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    JSValue val, ret;
    val = js_thisSymbolValue(ctx, this_val);
    if (JS_IsException(val))
        return val;
    /* XXX: use JS_ToStringInternal() with a flags */
    ret = js_string_constructor(ctx, JS_UNDEFINED, 1, vc(&val));
    JS_FreeValue(ctx, val);
    return ret;
}

static JSValue js_symbol_valueOf(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    return js_thisSymbolValue(ctx, this_val);
}

static JSValue js_symbol_get_description(JSContext *ctx, JSValueConst this_val)
{
    JSValue val, ret;
    JSAtomStruct *p;

    val = js_thisSymbolValue(ctx, this_val);
    if (JS_IsException(val))
        return val;
    p = JS_VALUE_GET_PTR(val);
    if (p->len == 0 && p->is_wide_char != 0) {
        ret = JS_UNDEFINED;
    } else {
        ret = JS_AtomToString(ctx, js_get_atom_index(ctx->rt, p));
    }
    JS_FreeValue(ctx, val);
    return ret;
}

static const JSCFunctionListEntry js_symbol_proto_funcs[] = {
    JS_CFUNC_DEF("toString", 0, js_symbol_toString ),
    JS_CFUNC_DEF("valueOf", 0, js_symbol_valueOf ),
    // XXX: should have writable: false
    JS_CFUNC_DEF("[Symbol.toPrimitive]", 1, js_symbol_valueOf ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Symbol", JS_PROP_CONFIGURABLE ),
    JS_CGETSET_DEF("description", js_symbol_get_description, NULL ),
};

static JSValue js_symbol_for(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    JSValue str;

    str = JS_ToString(ctx, argv[0]);
    if (JS_IsException(str))
        return JS_EXCEPTION;
    return JS_NewSymbolInternal(ctx, JS_VALUE_GET_STRING(str), JS_ATOM_TYPE_GLOBAL_SYMBOL);
}

static JSValue js_symbol_keyFor(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    JSAtomStruct *p;

    if (!JS_IsSymbol(argv[0]))
        return JS_ThrowTypeError(ctx, "not a symbol");
    p = JS_VALUE_GET_PTR(argv[0]);
    if (p->atom_type != JS_ATOM_TYPE_GLOBAL_SYMBOL)
        return JS_UNDEFINED;
    return js_dup(JS_MKPTR(JS_TAG_STRING, p));
}

static const JSCFunctionListEntry js_symbol_funcs[] = {
    JS_CFUNC_DEF("for", 1, js_symbol_for ),
    JS_CFUNC_DEF("keyFor", 1, js_symbol_keyFor ),
    JS_PROP_SYMBOL_DEF("toPrimitive", JS_ATOM_Symbol_toPrimitive, 0),
    JS_PROP_SYMBOL_DEF("iterator", JS_ATOM_Symbol_iterator, 0),
    JS_PROP_SYMBOL_DEF("match", JS_ATOM_Symbol_match, 0),
    JS_PROP_SYMBOL_DEF("matchAll", JS_ATOM_Symbol_matchAll, 0),
    JS_PROP_SYMBOL_DEF("replace", JS_ATOM_Symbol_replace, 0),
    JS_PROP_SYMBOL_DEF("search", JS_ATOM_Symbol_search, 0),
    JS_PROP_SYMBOL_DEF("split", JS_ATOM_Symbol_split, 0),
    JS_PROP_SYMBOL_DEF("toStringTag", JS_ATOM_Symbol_toStringTag, 0),
    JS_PROP_SYMBOL_DEF("isConcatSpreadable", JS_ATOM_Symbol_isConcatSpreadable, 0),
    JS_PROP_SYMBOL_DEF("hasInstance", JS_ATOM_Symbol_hasInstance, 0),
    JS_PROP_SYMBOL_DEF("species", JS_ATOM_Symbol_species, 0),
    JS_PROP_SYMBOL_DEF("unscopables", JS_ATOM_Symbol_unscopables, 0),
    JS_PROP_SYMBOL_DEF("asyncIterator", JS_ATOM_Symbol_asyncIterator, 0),
    JS_PROP_SYMBOL_DEF("dispose", JS_ATOM_Symbol_dispose, 0),
    JS_PROP_SYMBOL_DEF("asyncDispose", JS_ATOM_Symbol_asyncDispose, 0),
};

