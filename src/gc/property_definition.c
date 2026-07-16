/* Engine domain source: gc/gc_exceptions_properties.inc -> property_definition.
 * Ownership: gc subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static bool check_define_prop_flags(int prop_flags, int flags)
{
    bool has_accessor, is_getset;

    if (!(prop_flags & JS_PROP_CONFIGURABLE)) {
        if ((flags & (JS_PROP_HAS_CONFIGURABLE | JS_PROP_CONFIGURABLE)) ==
            (JS_PROP_HAS_CONFIGURABLE | JS_PROP_CONFIGURABLE)) {
            return false;
        }
        if ((flags & JS_PROP_HAS_ENUMERABLE) &&
            (flags & JS_PROP_ENUMERABLE) != (prop_flags & JS_PROP_ENUMERABLE))
            return false;
    }
    if (flags & (JS_PROP_HAS_VALUE | JS_PROP_HAS_WRITABLE |
                 JS_PROP_HAS_GET | JS_PROP_HAS_SET)) {
        if (!(prop_flags & JS_PROP_CONFIGURABLE)) {
            has_accessor = ((flags & (JS_PROP_HAS_GET | JS_PROP_HAS_SET)) != 0);
            is_getset = ((prop_flags & JS_PROP_TMASK) == JS_PROP_GETSET);
            if (has_accessor != is_getset)
                return false;
            if (!has_accessor && !is_getset && !(prop_flags & JS_PROP_WRITABLE)) {
                /* not writable: cannot set the writable bit */
                if ((flags & (JS_PROP_HAS_WRITABLE | JS_PROP_WRITABLE)) ==
                    (JS_PROP_HAS_WRITABLE | JS_PROP_WRITABLE))
                    return false;
            }
        }
    }
    return true;
}

/* ensure that the shape can be safely modified */
static int js_shape_prepare_update(JSContext *ctx, JSObject *p,
                                   JSShapeProperty **pprs)
{
    JSShape *sh;
    uint32_t idx = 0;    /* prevent warning */

    sh = p->shape;
    if (sh->is_hashed) {
        if (JS_REF_COUNT(sh) != 1) {
            if (pprs)
                idx = *pprs - get_shape_prop(sh);
            /* clone the shape (the resulting one is no longer hashed) */
            sh = js_clone_shape(ctx, sh);
            if (!sh)
                return -1;
            js_free_shape(ctx->rt, p->shape);
            p->shape = sh;
            if (pprs)
                *pprs = &get_shape_prop(sh)[idx];
        } else {
            js_shape_hash_unlink(ctx->rt, sh);
            sh->is_hashed = false;
        }
    }
    return 0;
}

static int js_update_property_flags(JSContext *ctx, JSObject *p,
                                    JSShapeProperty **pprs, int flags)
{
    if (flags != (*pprs)->flags) {
        if (js_shape_prepare_update(ctx, p, pprs))
            return -1;
        (*pprs)->flags = flags;
    }
    return 0;
}

/* allowed flags:
   JS_PROP_CONFIGURABLE, JS_PROP_WRITABLE, JS_PROP_ENUMERABLE
   JS_PROP_HAS_GET, JS_PROP_HAS_SET, JS_PROP_HAS_VALUE,
   JS_PROP_HAS_CONFIGURABLE, JS_PROP_HAS_WRITABLE, JS_PROP_HAS_ENUMERABLE,
   JS_PROP_THROW, JS_PROP_NO_EXOTIC.
   If JS_PROP_THROW is set, return an exception instead of false.
   if JS_PROP_NO_EXOTIC is set, do not call the exotic
   define_own_property callback.
   return -1 (exception), false or true.
*/
int JS_DefineProperty(JSContext *ctx, JSValueConst this_obj,
                      JSAtom prop, JSValueConst val,
                      JSValueConst getter, JSValueConst setter, int flags)
{
    JSObject *p;
    JSShapeProperty *prs;
    JSProperty *pr;
    int mask, res;

    if (JS_VALUE_GET_TAG(this_obj) != JS_TAG_OBJECT) {
        JS_ThrowTypeErrorNotAnObject(ctx);
        return -1;
    }
    p = JS_VALUE_GET_OBJ(this_obj);

 redo_prop_update:
    prs = find_own_property(&pr, p, prop);
    if (prs) {
        /* the range of the Array length property is always tested before */
        if ((prs->flags & JS_PROP_LENGTH) && (flags & JS_PROP_HAS_VALUE)) {
            uint32_t array_length;
            if (JS_ToArrayLengthFree(ctx, &array_length,
                                     js_dup(val), false)) {
                return -1;
            }
            /* this code relies on the fact that Uint32 are never allocated */
            val = js_uint32(array_length);
            /* prs may have been modified */
            prs = find_own_property(&pr, p, prop);
            assert(prs != NULL);
        }
        /* property already exists */
        if (!check_define_prop_flags(prs->flags, flags)) {
        not_configurable:
            return JS_ThrowTypeErrorOrFalse(ctx, flags, "property is not configurable");
        }

        if ((prs->flags & JS_PROP_TMASK) == JS_PROP_AUTOINIT) {
            /* Instantiate property and retry */
            if (JS_AutoInitProperty(ctx, p, prop, pr, prs))
                return -1;
            goto redo_prop_update;
        }

        if (flags & (JS_PROP_HAS_VALUE | JS_PROP_HAS_WRITABLE |
                     JS_PROP_HAS_GET | JS_PROP_HAS_SET)) {
            if (flags & (JS_PROP_HAS_GET | JS_PROP_HAS_SET)) {
                JSObject *new_getter, *new_setter;

                if (JS_IsFunction(ctx, getter)) {
                    new_getter = JS_VALUE_GET_OBJ(getter);
                } else {
                    new_getter = NULL;
                }
                if (JS_IsFunction(ctx, setter)) {
                    new_setter = JS_VALUE_GET_OBJ(setter);
                } else {
                    new_setter = NULL;
                }

                if ((prs->flags & JS_PROP_TMASK) != JS_PROP_GETSET) {
                    if (js_shape_prepare_update(ctx, p, &prs))
                        return -1;
                    /* convert to getset */
                    if ((prs->flags & JS_PROP_TMASK) == JS_PROP_VARREF) {
                        free_var_ref(ctx->rt, pr->u.var_ref);
                    } else {
                        JS_FreeValue(ctx, pr->u.value);
                    }
                    prs->flags = (prs->flags &
                                  (JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE)) |
                        JS_PROP_GETSET;
                    pr->u.getset.getter = NULL;
                    pr->u.getset.setter = NULL;
                } else {
                    if (!(prs->flags & JS_PROP_CONFIGURABLE)) {
                        if ((flags & JS_PROP_HAS_GET) &&
                            new_getter != pr->u.getset.getter) {
                            goto not_configurable;
                        }
                        if ((flags & JS_PROP_HAS_SET) &&
                            new_setter != pr->u.getset.setter) {
                            goto not_configurable;
                        }
                    }
                }
                if (flags & JS_PROP_HAS_GET) {
                    if (pr->u.getset.getter)
                        JS_FreeValue(ctx, JS_MKPTR(JS_TAG_OBJECT, pr->u.getset.getter));
                    if (new_getter)
                        js_dup(getter);
                    pr->u.getset.getter = new_getter;
                }
                if (flags & JS_PROP_HAS_SET) {
                    if (pr->u.getset.setter)
                        JS_FreeValue(ctx, JS_MKPTR(JS_TAG_OBJECT, pr->u.getset.setter));
                    if (new_setter)
                        js_dup(setter);
                    pr->u.getset.setter = new_setter;
                }
            } else {
                if ((prs->flags & JS_PROP_TMASK) == JS_PROP_GETSET) {
                    /* convert to data descriptor */
                    if (js_shape_prepare_update(ctx, p, &prs))
                        return -1;
                    if (pr->u.getset.getter)
                        JS_FreeValue(ctx, JS_MKPTR(JS_TAG_OBJECT, pr->u.getset.getter));
                    if (pr->u.getset.setter)
                        JS_FreeValue(ctx, JS_MKPTR(JS_TAG_OBJECT, pr->u.getset.setter));
                    prs->flags &= ~(JS_PROP_TMASK | JS_PROP_WRITABLE);
                    pr->u.value = JS_UNDEFINED;
                } else if ((prs->flags & JS_PROP_TMASK) == JS_PROP_VARREF) {
                    /* Note: JS_PROP_VARREF is always writable */
                } else {
                    if ((prs->flags & (JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE)) == 0 &&
                        (flags & JS_PROP_HAS_VALUE)) {
                        if (!js_same_value(ctx, val, pr->u.value)) {
                            goto not_configurable;
                        } else {
                            return true;
                        }
                    }
                }
                if ((prs->flags & JS_PROP_TMASK) == JS_PROP_VARREF) {
                    if (flags & JS_PROP_HAS_VALUE) {
                        if (p->class_id == JS_CLASS_MODULE_NS) {
                            /* JS_PROP_WRITABLE is always true for variable
                               references, but they are write protected in module name
                               spaces. */
                            if (!js_same_value(ctx, val, *pr->u.var_ref->pvalue))
                                goto not_configurable;
                        }
                        /* update the reference */
                        set_value(ctx, pr->u.var_ref->pvalue, js_dup(val));
                    }
                    /* if writable is set to false, no longer a
                       reference (for mapped arguments) */
                    if ((flags & (JS_PROP_HAS_WRITABLE | JS_PROP_WRITABLE)) == JS_PROP_HAS_WRITABLE) {
                        JSValue val1;
                        if (js_shape_prepare_update(ctx, p, &prs))
                            return -1;
                        val1 = js_dup(*pr->u.var_ref->pvalue);
                        free_var_ref(ctx->rt, pr->u.var_ref);
                        pr->u.value = val1;
                        prs->flags &= ~(JS_PROP_TMASK | JS_PROP_WRITABLE);
                    }
                } else if (prs->flags & JS_PROP_LENGTH) {
                    if (flags & JS_PROP_HAS_VALUE) {
                        /* Note: no JS code is executable because
                           'val' is guaranted to be a Uint32 */
                        res = set_array_length(ctx, p, js_dup(val), flags);
                    } else {
                        res = true;
                    }
                    /* still need to reset the writable flag if
                       needed.  The JS_PROP_LENGTH is kept because the
                       Uint32 test is still done if the length
                       property is read-only. */
                    if ((flags & (JS_PROP_HAS_WRITABLE | JS_PROP_WRITABLE)) ==
                        JS_PROP_HAS_WRITABLE) {
                        prs = get_shape_prop(p->shape);
                        if (js_update_property_flags(ctx, p, &prs,
                                                     prs->flags & ~JS_PROP_WRITABLE))
                            return -1;
                    }
                    return res;
                } else {
                    if (flags & JS_PROP_HAS_VALUE) {
                        JS_FreeValue(ctx, pr->u.value);
                        pr->u.value = js_dup(val);
                    }
                    if (flags & JS_PROP_HAS_WRITABLE) {
                        if (js_update_property_flags(ctx, p, &prs,
                                                     (prs->flags & ~JS_PROP_WRITABLE) |
                                                     (flags & JS_PROP_WRITABLE)))
                            return -1;
                    }
                }
            }
        }
        mask = 0;
        if (flags & JS_PROP_HAS_CONFIGURABLE)
            mask |= JS_PROP_CONFIGURABLE;
        if (flags & JS_PROP_HAS_ENUMERABLE)
            mask |= JS_PROP_ENUMERABLE;
        if (js_update_property_flags(ctx, p, &prs,
                                     (prs->flags & ~mask) | (flags & mask)))
            return -1;
        return true;
    }

    /* handle modification of fast array elements */
    if (p->fast_array) {
        uint32_t idx;
        uint32_t prop_flags;
        if (p->class_id == JS_CLASS_ARRAY) {
            if (__JS_AtomIsTaggedInt(prop)) {
                idx = __JS_AtomToUInt32(prop);
                if (idx < p->u.array.count) {
                    prop_flags = get_prop_flags(flags, JS_PROP_C_W_E);
                    if (prop_flags != JS_PROP_C_W_E)
                        goto convert_to_slow_array;
                    if (flags & (JS_PROP_HAS_GET | JS_PROP_HAS_SET)) {
                    convert_to_slow_array:
                        if (convert_fast_array_to_array(ctx, p))
                            return -1;
                        else
                            goto redo_prop_update;
                    }
                    if (flags & JS_PROP_HAS_VALUE) {
                        set_value(ctx, &p->u.array.u.values[idx], js_dup(val));
                    }
                    return true;
                }
            }
        } else if (is_typed_array(p->class_id)) {
            JSValue num;
            int ret;

            if (!__JS_AtomIsTaggedInt(prop)) {
                /* slow path with to handle all numeric indexes */
                num = JS_AtomIsNumericIndex1(ctx, prop);
                if (JS_IsUndefined(num))
                    goto typed_array_done;
                if (JS_IsException(num))
                    return -1;
                ret = JS_NumberIsInteger(ctx, num);
                if (ret < 0) {
                    JS_FreeValue(ctx, num);
                    return -1;
                }
                if (!ret) {
                    JS_FreeValue(ctx, num);
                    return JS_ThrowTypeErrorOrFalse(ctx, flags, "non integer index in typed array");
                }
                ret = JS_NumberIsNegativeOrMinusZero(ctx, num);
                JS_FreeValue(ctx, num);
                if (ret) {
                    return JS_ThrowTypeErrorOrFalse(ctx, flags, "negative index in typed array");
                }
                if (!__JS_AtomIsTaggedInt(prop))
                    goto typed_array_oob;
            }
            idx = __JS_AtomToUInt32(prop);
            /* if the typed array is detached, p->u.array.count = 0 */
            if (idx >= p->u.array.count) {
            typed_array_oob:
                return JS_ThrowTypeErrorOrFalse(ctx, flags, "out-of-bound index in typed array");
            }
            prop_flags = get_prop_flags(flags, JS_PROP_ENUMERABLE | JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
            if (flags & (JS_PROP_HAS_GET | JS_PROP_HAS_SET) ||
                prop_flags != (JS_PROP_ENUMERABLE | JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE)) {
                return JS_ThrowTypeErrorOrFalse(ctx, flags, "invalid descriptor flags");
            }
            if (flags & JS_PROP_HAS_VALUE) {
                return JS_SetPropertyValue(ctx, this_obj, js_int32(idx), js_dup(val), flags);
            }
            return true;
        typed_array_done: ;
        }
    }

    return JS_CreateProperty(ctx, p, prop, val, getter, setter, flags);
}

static int JS_DefineAutoInitProperty(JSContext *ctx, JSValueConst this_obj,
                                     JSAtom prop, JSAutoInitIDEnum id,
                                     void *opaque, int flags)
{
    JSObject *p;
    JSProperty *pr;

    if (JS_VALUE_GET_TAG(this_obj) != JS_TAG_OBJECT)
        return false;

    p = JS_VALUE_GET_OBJ(this_obj);

    if (find_own_property(&pr, p, prop)) {
        /* property already exists */
        abort();
        return false;
    }

    /* Specialized CreateProperty */
    pr = add_property(ctx, p, prop, (flags & JS_PROP_C_W_E) | JS_PROP_AUTOINIT);
    if (unlikely(!pr))
        return -1;
    pr->u.init.realm_and_id = (uintptr_t)JS_DupContext(ctx);
    assert((pr->u.init.realm_and_id & 3) == 0);
    assert(id <= 3);
    pr->u.init.realm_and_id |= id;
    pr->u.init.opaque = opaque;
    return true;
}

/* Like JS_DefinePropertyValue but borrows val (does not free it) */
static int JS_DefinePropertyValueConst(JSContext *ctx, JSValueConst this_obj,
                                       JSAtom prop, JSValueConst val, int flags)
{
    return JS_DefineProperty(ctx, this_obj, prop, val, JS_UNDEFINED, JS_UNDEFINED,
                             flags | JS_PROP_HAS_VALUE | JS_PROP_HAS_CONFIGURABLE |
                             JS_PROP_HAS_WRITABLE | JS_PROP_HAS_ENUMERABLE);
}

/* shortcut to add or redefine a new property value */
int JS_DefinePropertyValue(JSContext *ctx, JSValueConst this_obj,
                           JSAtom prop, JSValue val, int flags)
{
    int ret;
    ret = JS_DefinePropertyValueConst(ctx, this_obj, prop, val, flags);
    JS_FreeValue(ctx, val);
    return ret;
}

int JS_DefinePropertyValueValue(JSContext *ctx, JSValueConst this_obj,
                                JSValue prop, JSValue val, int flags)
{
    JSAtom atom;
    int ret;
    atom = JS_ValueToAtom(ctx, prop);
    JS_FreeValue(ctx, prop);
    if (unlikely(atom == JS_ATOM_NULL)) {
        JS_FreeValue(ctx, val);
        return -1;
    }
    ret = JS_DefinePropertyValue(ctx, this_obj, atom, val, flags);
    JS_FreeAtom(ctx, atom);
    return ret;
}

int JS_DefinePropertyValueUint32(JSContext *ctx, JSValueConst this_obj,
                                 uint32_t idx, JSValue val, int flags)
{
    return JS_DefinePropertyValueValue(ctx, this_obj, js_uint32(idx),
                                       val, flags);
}

int JS_DefinePropertyValueInt64(JSContext *ctx, JSValueConst this_obj,
                                int64_t idx, JSValue val, int flags)
{
    return JS_DefinePropertyValueValue(ctx, this_obj, js_int64(idx),
                                       val, flags);
}

/* `prop` may be pure ASCII or UTF-8 encoded */
int JS_DefinePropertyValueStr(JSContext *ctx, JSValueConst this_obj,
                              const char *prop, JSValue val, int flags)
{
    JSAtom atom;
    int ret;
    atom = JS_NewAtom(ctx, prop);
    if (atom == JS_ATOM_NULL) {
        JS_FreeValue(ctx, val);
        return -1;
    }
    ret = JS_DefinePropertyValue(ctx, this_obj, atom, val, flags);
    JS_FreeAtom(ctx, atom);
    return ret;
}

/* shortcut to add getter & setter */
int JS_DefinePropertyGetSet(JSContext *ctx, JSValueConst this_obj,
                            JSAtom prop, JSValue getter, JSValue setter,
                            int flags)
{
    int ret;
    ret = JS_DefineProperty(ctx, this_obj, prop, JS_UNDEFINED, getter, setter,
                            flags | JS_PROP_HAS_GET | JS_PROP_HAS_SET |
                            JS_PROP_HAS_CONFIGURABLE | JS_PROP_HAS_ENUMERABLE);
    JS_FreeValue(ctx, getter);
    JS_FreeValue(ctx, setter);
    return ret;
}

static int JS_CreateDataPropertyUint32(JSContext *ctx, JSValueConst this_obj,
                                       int64_t idx, JSValue val, int flags)
{
    return JS_DefinePropertyValueValue(ctx, this_obj, js_int64(idx),
                                       val, flags | JS_PROP_CONFIGURABLE |
                                       JS_PROP_ENUMERABLE | JS_PROP_WRITABLE);
}

/* Like JS_DefinePropertyValueInt64 but borrows val (does not free it) */
static int JS_DefinePropertyValueInt64Const(JSContext *ctx, JSValueConst this_obj,
                                            int64_t idx, JSValueConst val, int flags)
{
    JSAtom atom;
    int ret;
    atom = JS_ValueToAtom(ctx, js_int64(idx));
    if (unlikely(atom == JS_ATOM_NULL))
        return -1;
    ret = JS_DefinePropertyValueConst(ctx, this_obj, atom, val, flags);
    JS_FreeAtom(ctx, atom);
    return ret;
}

/* Like JS_CreateDataPropertyUint32 but borrows val (does not free it) */
static int JS_CreateDataPropertyUint32Const(JSContext *ctx, JSValueConst this_obj,
                                            int64_t idx, JSValueConst val, int flags)
{
    return JS_DefinePropertyValueInt64Const(ctx, this_obj, idx, val,
                                            flags | JS_PROP_CONFIGURABLE |
                                            JS_PROP_ENUMERABLE | JS_PROP_WRITABLE);
}


/* return true if 'obj' has a non empty 'name' string */
static bool js_object_has_name(JSContext *ctx, JSValue obj)
{
    JSProperty *pr;
    JSShapeProperty *prs;
    JSValue val;
    JSString *p;

    prs = find_own_property(&pr, JS_VALUE_GET_OBJ(obj), JS_ATOM_name);
    if (!prs)
        return false;
    if ((prs->flags & JS_PROP_TMASK) != JS_PROP_NORMAL)
        return true;
    val = pr->u.value;
    if (JS_VALUE_GET_TAG(val) != JS_TAG_STRING)
        return true;
    p = JS_VALUE_GET_STRING(val);
    return (p->len != 0);
}

static int JS_DefineObjectName(JSContext *ctx, JSValue obj,
                               JSAtom name, int flags)
{
    if (name != JS_ATOM_NULL
    &&  JS_IsObject(obj)
    &&  !js_object_has_name(ctx, obj)
    &&  JS_DefinePropertyValue(ctx, obj, JS_ATOM_name, JS_AtomToString(ctx, name), flags) < 0) {
        return -1;
    }
    return 0;
}

static int JS_DefineObjectNameComputed(JSContext *ctx, JSValue obj,
                                       JSValue str, int flags)
{
    if (JS_IsObject(obj) &&
        !js_object_has_name(ctx, obj)) {
        JSAtom prop;
        JSValue name_str;
        prop = JS_ValueToAtom(ctx, str);
        if (prop == JS_ATOM_NULL)
            return -1;
        name_str = js_get_function_name(ctx, prop);
        JS_FreeAtom(ctx, prop);
        if (JS_IsException(name_str))
            return -1;
        if (JS_DefinePropertyValue(ctx, obj, JS_ATOM_name, name_str, flags) < 0)
            return -1;
    }
    return 0;
}

#define DEFINE_GLOBAL_LEX_VAR (1 << 7)
#define DEFINE_GLOBAL_FUNC_VAR (1 << 6)

static JSValue JS_ThrowSyntaxErrorVarRedeclaration(JSContext *ctx, JSAtom prop)
{
    return JS_ThrowSyntaxErrorAtom(ctx, "redeclaration of '%s'", prop);
}

/* flags is 0, DEFINE_GLOBAL_LEX_VAR or DEFINE_GLOBAL_FUNC_VAR */
/* XXX: could support exotic global object. */
static int JS_CheckDefineGlobalVar(JSContext *ctx, JSAtom prop, int flags)
{
    JSObject *p;
    JSShapeProperty *prs;

    p = JS_VALUE_GET_OBJ(ctx->global_obj);
    prs = find_own_property1(p, prop);
    /* XXX: should handle JS_PROP_AUTOINIT */
    if (flags & DEFINE_GLOBAL_LEX_VAR) {
        if (prs && !(prs->flags & JS_PROP_CONFIGURABLE))
            goto fail_redeclaration;
    } else {
        if (!prs && !p->extensible)
            goto define_error;
        if (flags & DEFINE_GLOBAL_FUNC_VAR) {
            if (prs) {
                if (!(prs->flags & JS_PROP_CONFIGURABLE) &&
                    ((prs->flags & JS_PROP_TMASK) == JS_PROP_GETSET ||
                     ((prs->flags & (JS_PROP_WRITABLE | JS_PROP_ENUMERABLE)) !=
                      (JS_PROP_WRITABLE | JS_PROP_ENUMERABLE)))) {
                define_error:
                    JS_ThrowTypeErrorAtom(ctx, "cannot define variable '%s'",
                                          prop);
                    return -1;
                }
            }
        }
    }
    /* check if there already is a lexical declaration */
    p = JS_VALUE_GET_OBJ(ctx->global_var_obj);
    prs = find_own_property1(p, prop);
    if (prs) {
    fail_redeclaration:
        JS_ThrowSyntaxErrorVarRedeclaration(ctx, prop);
        return -1;
    }
    return 0;
}

/* def_flags is (0, DEFINE_GLOBAL_LEX_VAR) |
   JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE */
/* XXX: could support exotic global object. */
static int JS_DefineGlobalVar(JSContext *ctx, JSAtom prop, int def_flags)
{
    JSObject *p;
    JSShapeProperty *prs;
    JSProperty *pr;
    JSValue val;
    int flags;

    if (def_flags & DEFINE_GLOBAL_LEX_VAR) {
        p = JS_VALUE_GET_OBJ(ctx->global_var_obj);
        flags = JS_PROP_ENUMERABLE | (def_flags & JS_PROP_WRITABLE) |
            JS_PROP_CONFIGURABLE;
        val = JS_UNINITIALIZED;
    } else {
        p = JS_VALUE_GET_OBJ(ctx->global_obj);
        flags = JS_PROP_ENUMERABLE | JS_PROP_WRITABLE |
            (def_flags & JS_PROP_CONFIGURABLE);
        val = JS_UNDEFINED;
    }
    prs = find_own_property1(p, prop);
    if (prs)
        return 0;
    if (!p->extensible)
        return 0;
    pr = add_property(ctx, p, prop, flags);
    if (unlikely(!pr))
        return -1;
    pr->u.value = val;
    return 0;
}

/* 'def_flags' is 0 or JS_PROP_CONFIGURABLE. */
/* XXX: could support exotic global object. */
static int JS_DefineGlobalFunction(JSContext *ctx, JSAtom prop,
                                   JSValue func, int def_flags)
{

    JSObject *p;
    JSShapeProperty *prs;
    int flags;

    p = JS_VALUE_GET_OBJ(ctx->global_obj);
    prs = find_own_property1(p, prop);
    flags = JS_PROP_HAS_VALUE | JS_PROP_THROW;
    if (!prs || (prs->flags & JS_PROP_CONFIGURABLE)) {
        flags |= JS_PROP_ENUMERABLE | JS_PROP_WRITABLE | def_flags |
            JS_PROP_HAS_CONFIGURABLE | JS_PROP_HAS_WRITABLE | JS_PROP_HAS_ENUMERABLE;
    }
    if (JS_DefineProperty(ctx, ctx->global_obj, prop, func,
                          JS_UNDEFINED, JS_UNDEFINED, flags) < 0)
        return -1;
    return 0;
}

static JSValue JS_GetGlobalVar(JSContext *ctx, JSAtom prop,
                               bool throw_ref_error)
{
    JSObject *p;
    JSShapeProperty *prs;
    JSProperty *pr;

    /* no exotic behavior is possible in global_var_obj */
    p = JS_VALUE_GET_OBJ(ctx->global_var_obj);
    prs = find_own_property(&pr, p, prop);
    if (prs) {
        /* XXX: should handle JS_PROP_TMASK properties */
        if (unlikely(JS_IsUninitialized(pr->u.value)))
            return JS_ThrowReferenceErrorUninitialized(ctx, prs->atom);
        return js_dup(pr->u.value);
    }

    /* fast path */
    p = JS_VALUE_GET_OBJ(ctx->global_obj);
    prs = find_own_property(&pr, p, prop);
    if (prs) {
        if (likely((prs->flags & JS_PROP_TMASK) == 0))
            return js_dup(pr->u.value);
    }
    return JS_GetPropertyInternal(ctx, ctx->global_obj, prop,
                                 ctx->global_obj, throw_ref_error);
}

/* construct a reference to a global variable */
static int JS_GetGlobalVarRef(JSContext *ctx, JSAtom prop, JSValue *sp)
{
    JSObject *p;
    JSShapeProperty *prs;
    JSProperty *pr;

    /* no exotic behavior is possible in global_var_obj */
    p = JS_VALUE_GET_OBJ(ctx->global_var_obj);
    prs = find_own_property(&pr, p, prop);
    if (prs) {
        /* XXX: should handle JS_PROP_AUTOINIT properties? */
        /* XXX: conformance: do these tests in
           OP_put_var_ref/OP_get_var_ref ? */
        if (unlikely(JS_IsUninitialized(pr->u.value))) {
            JS_ThrowReferenceErrorUninitialized(ctx, prs->atom);
            return -1;
        }
        if (unlikely(!(prs->flags & JS_PROP_WRITABLE))) {
            return JS_ThrowTypeErrorReadOnly(ctx, JS_PROP_THROW, prop);
        }
        sp[0] = js_dup(ctx->global_var_obj);
    } else {
        int ret;
        ret = JS_HasProperty(ctx, ctx->global_obj, prop);
        if (ret < 0)
            return -1;
        if (ret) {
            sp[0] = js_dup(ctx->global_obj);
        } else {
            sp[0] = JS_UNDEFINED;
        }
    }
    sp[1] = JS_AtomToValue(ctx, prop);
    return 0;
}

/* flag = 0: normal variable write
   flag = 1: initialize lexical variable
*/
static inline int JS_SetGlobalVar(JSContext *ctx, JSAtom prop, JSValue val,
                                  int flag)
{
    JSObject *p;
    JSShapeProperty *prs;
    JSProperty *pr;
    int ret;

    /* no exotic behavior is possible in global_var_obj */
    p = JS_VALUE_GET_OBJ(ctx->global_var_obj);
    prs = find_own_property(&pr, p, prop);
    if (prs) {
        /* XXX: should handle JS_PROP_AUTOINIT properties? */
        if (flag != 1) {
            if (unlikely(JS_IsUninitialized(pr->u.value))) {
                JS_FreeValue(ctx, val);
                JS_ThrowReferenceErrorUninitialized(ctx, prs->atom);
                return -1;
            }
            if (unlikely(!(prs->flags & JS_PROP_WRITABLE))) {
                JS_FreeValue(ctx, val);
                return JS_ThrowTypeErrorReadOnly(ctx, JS_PROP_THROW, prop);
            }
        }
        set_value(ctx, &pr->u.value, val);
        return 0;
    }

    p = JS_VALUE_GET_OBJ(ctx->global_obj);
    prs = find_own_property(&pr, p, prop);
    if (prs) {
        if (likely((prs->flags & (JS_PROP_TMASK | JS_PROP_WRITABLE |
                                  JS_PROP_LENGTH)) == JS_PROP_WRITABLE)) {
            /* fast path */
            set_value(ctx, &pr->u.value, val);
            return 0;
        }
    }
    /* slow path */
    ret = JS_HasProperty(ctx, ctx->global_obj, prop);
    if (ret < 0) {
        JS_FreeValue(ctx, val);
        return -1;
    }
    if (ret == 0 && is_strict_mode(ctx)) {
        JS_FreeValue(ctx, val);
        JS_ThrowReferenceErrorNotDefined(ctx, prop);
        return -1;
    }
    return JS_SetPropertyInternal(ctx, ctx->global_obj, prop, val,
                                  JS_PROP_THROW_STRICT);
}

/* return -1, false or true */
static int JS_DeleteGlobalVar(JSContext *ctx, JSAtom prop)
{
    JSObject *p;
    JSShapeProperty *prs;
    JSProperty *pr;
    int ret;

    /* 9.1.1.4.7 DeleteBinding ( N ) */
    p = JS_VALUE_GET_OBJ(ctx->global_var_obj);
    prs = find_own_property(&pr, p, prop);
    if (prs)
        return false; /* lexical variables cannot be deleted */
    ret = JS_HasProperty(ctx, ctx->global_obj, prop);
    if (ret < 0)
        return -1;
    if (ret) {
        return JS_DeleteProperty(ctx, ctx->global_obj, prop, 0);
    } else {
        return true;
    }
}

/* return -1, false or true. return false if not configurable or
   invalid object. return -1 in case of exception.
   flags can be 0, JS_PROP_THROW or JS_PROP_THROW_STRICT */
int JS_DeleteProperty(JSContext *ctx, JSValueConst obj, JSAtom prop, int flags)
{
    JSValue obj1;
    JSObject *p;
    int res;

    obj1 = JS_ToObject(ctx, obj);
    if (JS_IsException(obj1))
        return -1;
    p = JS_VALUE_GET_OBJ(obj1);
    res = delete_property(ctx, p, prop);
    JS_FreeValue(ctx, obj1);
    if (res != false)
        return res;
    if ((flags & JS_PROP_THROW) ||
        ((flags & JS_PROP_THROW_STRICT) && is_strict_mode(ctx))) {
        JS_ThrowTypeError(ctx, "could not delete property");
        return -1;
    }
    return false;
}

int JS_DeletePropertyInt64(JSContext *ctx, JSValueConst obj, int64_t idx, int flags)
{
    JSAtom prop;
    int res;

    if ((uint64_t)idx <= JS_ATOM_MAX_INT) {
        /* fast path for fast arrays */
        return JS_DeleteProperty(ctx, obj, __JS_AtomFromUInt32(idx), flags);
    }
    prop = JS_NewAtomInt64(ctx, idx);
    if (prop == JS_ATOM_NULL)
        return -1;
    res = JS_DeleteProperty(ctx, obj, prop, flags);
    JS_FreeAtom(ctx, prop);
    return res;
}

bool JS_IsFunction(JSContext *ctx, JSValueConst val)
{
    JSObject *p;
    if (JS_VALUE_GET_TAG(val) != JS_TAG_OBJECT)
        return false;
    p = JS_VALUE_GET_OBJ(val);
    switch(p->class_id) {
    case JS_CLASS_BYTECODE_FUNCTION:
        return true;
    case JS_CLASS_PROXY:
        return p->u.proxy_data->is_func;
    default:
        return (ctx->rt->class_array[p->class_id].call != NULL);
    }
}

bool JS_IsAsyncFunction(JSValueConst val)
{
    return JS_CLASS_ASYNC_FUNCTION == JS_GetClassID(val);
}

static bool JS_IsCFunction(JSContext *ctx, JSValueConst val, JSCFunction *func,
                           int magic)
{
    JSObject *p;
    if (JS_VALUE_GET_TAG(val) != JS_TAG_OBJECT)
        return false;
    p = JS_VALUE_GET_OBJ(val);
    if (p->class_id == JS_CLASS_C_FUNCTION)
        return (p->u.cfunc.c_function.generic == func && p->u.cfunc.magic == magic);
    else
        return false;
}

bool JS_IsConstructor(JSContext *ctx, JSValueConst val)
{
    JSObject *p;
    if (JS_VALUE_GET_TAG(val) != JS_TAG_OBJECT)
        return false;
    p = JS_VALUE_GET_OBJ(val);
    return p->is_constructor;
}

bool JS_SetConstructorBit(JSContext *ctx, JSValueConst func_obj, bool val)
{
    JSObject *p;
    if (JS_VALUE_GET_TAG(func_obj) != JS_TAG_OBJECT)
        return false;
    p = JS_VALUE_GET_OBJ(func_obj);
    p->is_constructor = val;
    return true;
}

bool JS_IsRegExp(JSValueConst val)
{
    return JS_CLASS_REGEXP == JS_GetClassID(val);
}

bool JS_IsMap(JSValueConst val)
{
    return JS_CLASS_MAP == JS_GetClassID(val);
}

bool JS_IsSet(JSValueConst val)
{
    return JS_CLASS_SET == JS_GetClassID(val);
}

bool JS_IsWeakRef(JSValueConst val)
{
    return JS_CLASS_WEAK_REF == JS_GetClassID(val);
}

bool JS_IsWeakSet(JSValueConst val)
{
    return JS_CLASS_WEAKSET == JS_GetClassID(val);
}

bool JS_IsWeakMap(JSValueConst val)
{
    return JS_CLASS_WEAKMAP == JS_GetClassID(val);
}

bool JS_IsDataView(JSValueConst val)
{
    return JS_CLASS_DATAVIEW == JS_GetClassID(val);
}

bool JS_IsError(JSValueConst val)
{
    return JS_CLASS_ERROR == JS_GetClassID(val);
}

/* used to avoid catching interrupt exceptions */
bool JS_IsUncatchableError(JSValueConst val)
{
    JSObject *p;
    if (JS_VALUE_GET_TAG(val) != JS_TAG_OBJECT)
        return false;
    p = JS_VALUE_GET_OBJ(val);
    return p->class_id == JS_CLASS_ERROR && p->is_uncatchable_error;
}

static void js_set_uncatchable_error(JSContext *ctx, JSValueConst val, bool flag)
{
    JSObject *p;
    if (JS_VALUE_GET_TAG(val) != JS_TAG_OBJECT)
        return;
    p = JS_VALUE_GET_OBJ(val);
    if (p->class_id == JS_CLASS_ERROR)
        p->is_uncatchable_error = flag;
}

void JS_SetUncatchableError(JSContext *ctx, JSValueConst val)
{
    js_set_uncatchable_error(ctx, val, true);
}

void JS_ClearUncatchableError(JSContext *ctx, JSValueConst val)
{
    js_set_uncatchable_error(ctx, val, false);
}

void turbojs_internal_reset_uncatchable_error(JSContext *ctx)
{
    js_set_uncatchable_error(ctx, ctx->rt->current_exception, false);
}

int JS_SetOpaque(JSValueConst obj, void *opaque)
{
    JSObject *p;
    if (JS_VALUE_GET_TAG(obj) == JS_TAG_OBJECT) {
        p = JS_VALUE_GET_OBJ(obj);
        // User code can't set the opaque of internal objects.
        if (p->class_id >= JS_CLASS_INIT_COUNT) {
            p->u.opaque = opaque;
            return 0;
        }
    }

    return -1;
}

