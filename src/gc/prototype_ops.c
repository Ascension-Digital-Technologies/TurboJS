/* Engine domain source: gc/gc_exceptions_properties.inc -> prototype_ops.
 * Ownership: gc subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static JSValue JS_GetPropertyInternal(JSContext *ctx, JSValueConst obj,
                                      JSAtom prop, JSValueConst this_obj,
                                      bool throw_ref_error)
{
    JSObject *p;
    JSProperty *pr;
    JSShapeProperty *prs;
    uint32_t tag;

    tag = JS_VALUE_GET_TAG(obj);
    if (unlikely(tag != JS_TAG_OBJECT)) {
        switch(tag) {
        case JS_TAG_NULL:
            return JS_ThrowTypeErrorAtom(ctx, "cannot read property '%s' of null", prop);
        case JS_TAG_UNDEFINED:
            return JS_ThrowTypeErrorAtom(ctx, "cannot read property '%s' of undefined", prop);
        case JS_TAG_EXCEPTION:
            return JS_EXCEPTION;
        case JS_TAG_STRING:
            {
                JSString *p1 = JS_VALUE_GET_STRING(obj);
                if (__JS_AtomIsTaggedInt(prop)) {
                    uint32_t idx, ch;
                    idx = __JS_AtomToUInt32(prop);
                    if (idx < p1->len) {
                        ch = string_get(p1, idx);
                        return js_new_string_char(ctx, ch);
                    }
                } else if (prop == JS_ATOM_length) {
                    return js_int32(p1->len);
                }
            }
            break;
        case JS_TAG_STRING_ROPE:
            {
                JSStringRope *r = JS_VALUE_GET_STRING_ROPE(obj);
                if (__JS_AtomIsTaggedInt(prop)) {
                    uint32_t idx, ch;
                    idx = __JS_AtomToUInt32(prop);
                    if (idx < r->len) {
                        ch = string_rope_get(obj, idx);
                        return js_new_string_char(ctx, ch);
                    }
                } else if (prop == JS_ATOM_length) {
                    return js_int32(r->len);
                }
            }
            break;
        default:
            break;
        }
        /* cannot raise an exception */
        p = JS_VALUE_GET_OBJ(JS_GetPrototypePrimitive(ctx, obj));
        if (!p)
            return JS_UNDEFINED;
    } else {
        p = JS_VALUE_GET_OBJ(obj);
    }

    for(;;) {
        prs = find_own_property(&pr, p, prop);
        if (prs) {
            /* found */
            if (unlikely(prs->flags & JS_PROP_TMASK)) {
                if ((prs->flags & JS_PROP_TMASK) == JS_PROP_GETSET) {
                    if (unlikely(!pr->u.getset.getter)) {
                        return JS_UNDEFINED;
                    } else {
                        JSValue func = JS_MKPTR(JS_TAG_OBJECT, pr->u.getset.getter);
                        /* Note: the field could be removed in the getter */
                        func = js_dup(func);
                        return JS_CallFree(ctx, func, this_obj, 0, NULL);
                    }
                } else if ((prs->flags & JS_PROP_TMASK) == JS_PROP_VARREF) {
                    JSValue val = *pr->u.var_ref->pvalue;
                    if (unlikely(JS_IsUninitialized(val)))
                        return JS_ThrowReferenceErrorUninitialized(ctx, prs->atom);
                    return js_dup(val);
                } else if ((prs->flags & JS_PROP_TMASK) == JS_PROP_AUTOINIT) {
                    /* Instantiate property and retry */
                    if (JS_AutoInitProperty(ctx, p, prop, pr, prs))
                        return JS_EXCEPTION;
                    continue;
                }
            } else {
                return js_dup(pr->u.value);
            }
        }
        if (unlikely(p->is_exotic)) {
            /* exotic behaviors */
            if (p->fast_array) {
                if (__JS_AtomIsTaggedInt(prop)) {
                    uint32_t idx = __JS_AtomToUInt32(prop);
                    if (idx < p->u.array.count) {
                        /* we avoid duplicating the code */
                        return JS_GetPropertyUint32(ctx, JS_MKPTR(JS_TAG_OBJECT, p), idx);
                    } else if (is_typed_array(p->class_id)) {
                        return JS_UNDEFINED;
                    }
                } else if (is_typed_array(p->class_id)) {
                    int ret;
                    ret = JS_AtomIsNumericIndex(ctx, prop);
                    if (ret != 0) {
                        if (ret < 0)
                            return JS_EXCEPTION;
                        return JS_UNDEFINED;
                    }
                }
            } else {
                const JSClassExoticMethods *em = ctx->rt->class_array[p->class_id].exotic;
                if (em) {
                    if (em->get_property) {
                        JSValue obj1, retval;
                        /* XXX: should pass throw_ref_error */
                        /* Note: if 'p' is a prototype, it can be
                           freed in the called function */
                        obj1 = js_dup(JS_MKPTR(JS_TAG_OBJECT, p));
                        retval = em->get_property(ctx, obj1, prop, this_obj);
                        JS_FreeValue(ctx, obj1);
                        return retval;
                    }
                    if (em->get_own_property) {
                        JSPropertyDescriptor desc;
                        int ret;
                        JSValue obj1;

                        /* Note: if 'p' is a prototype, it can be
                           freed in the called function */
                        obj1 = js_dup(JS_MKPTR(JS_TAG_OBJECT, p));
                        ret = em->get_own_property(ctx, &desc, obj1, prop);
                        JS_FreeValue(ctx, obj1);
                        if (ret < 0)
                            return JS_EXCEPTION;
                        if (ret) {
                            if (desc.flags & JS_PROP_GETSET) {
                                JS_FreeValue(ctx, desc.setter);
                                return JS_CallFree(ctx, desc.getter, this_obj, 0, NULL);
                            } else {
                                return desc.value;
                            }
                        }
                    }
                }
            }
        }
        p = p->shape->proto;
        if (!p)
            break;
    }
    if (unlikely(throw_ref_error)) {
        return JS_ThrowReferenceErrorNotDefined(ctx, prop);
    } else {
        return JS_UNDEFINED;
    }
}

JSValue JS_GetProperty(JSContext *ctx, JSValueConst this_obj, JSAtom prop)
{
    return JS_GetPropertyInternal(ctx, this_obj, prop, this_obj, false);
}

static JSValue JS_ThrowTypeErrorPrivateNotFound(JSContext *ctx, JSAtom atom)
{
    return JS_ThrowTypeErrorAtom(ctx, "private class field '%s' does not exist",
                                 atom);
}

/* Per the nonextensible-applies-to-private proposal, private fields cannot
   be added to non-extensible objects. Note that a Proxy's JSObject stays
   extensible (its trap reflects the target), so private fields can still be
   stamped onto Proxies. */
static int JS_DefinePrivateField(JSContext *ctx, JSValueConst obj,
                                 JSValue name, JSValue val)
{
    JSObject *p;
    JSShapeProperty *prs;
    JSProperty *pr;
    JSAtom prop;

    if (unlikely(JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT)) {
        JS_ThrowTypeErrorNotAnObject(ctx);
        goto fail;
    }
    /* safety check */
    if (unlikely(JS_VALUE_GET_TAG(name) != JS_TAG_SYMBOL)) {
        JS_ThrowTypeErrorNotASymbol(ctx);
        goto fail;
    }
    prop = js_symbol_to_atom(ctx, name);
    p = JS_VALUE_GET_OBJ(obj);
    if (unlikely(!p->extensible)) {
        JS_ThrowTypeError(ctx, "object is not extensible");
        goto fail;
    }
    prs = find_own_property(&pr, p, prop);
    if (prs) {
        JS_ThrowTypeErrorAtom(ctx, "private class field '%s' already exists",
                              prop);
        goto fail;
    }
    pr = add_property(ctx, p, prop, JS_PROP_C_W_E);
    if (unlikely(!pr)) {
    fail:
        JS_FreeValue(ctx, val);
        return -1;
    }
    pr->u.value = val;
    return 0;
}

static JSValue JS_GetPrivateField(JSContext *ctx, JSValueConst obj,
                                  JSValueConst name)
{
    JSObject *p;
    JSShapeProperty *prs;
    JSProperty *pr;
    JSAtom prop;

    if (unlikely(JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT))
        return JS_ThrowTypeErrorNotAnObject(ctx);
    /* safety check */
    if (unlikely(JS_VALUE_GET_TAG(name) != JS_TAG_SYMBOL))
        return JS_ThrowTypeErrorNotASymbol(ctx);
    prop = js_symbol_to_atom(ctx, name);
    p = JS_VALUE_GET_OBJ(obj);
    prs = find_own_property(&pr, p, prop);
    if (!prs) {
        JS_ThrowTypeErrorPrivateNotFound(ctx, prop);
        return JS_EXCEPTION;
    }
    return js_dup(pr->u.value);
}

static int JS_SetPrivateField(JSContext *ctx, JSValueConst obj,
                              JSValueConst name, JSValue val)
{
    JSObject *p;
    JSShapeProperty *prs;
    JSProperty *pr;
    JSAtom prop;

    if (unlikely(JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT)) {
        JS_ThrowTypeErrorNotAnObject(ctx);
        goto fail;
    }
    /* safety check */
    if (unlikely(JS_VALUE_GET_TAG(name) != JS_TAG_SYMBOL)) {
        JS_ThrowTypeErrorNotASymbol(ctx);
        goto fail;
    }
    prop = js_symbol_to_atom(ctx, name);
    p = JS_VALUE_GET_OBJ(obj);
    prs = find_own_property(&pr, p, prop);
    if (!prs) {
        JS_ThrowTypeErrorPrivateNotFound(ctx, prop);
    fail:
        JS_FreeValue(ctx, val);
        return -1;
    }
    set_value(ctx, &pr->u.value, val);
    return 0;
}

/* add a private brand field to 'home_obj' if not already present and
   if obj is != null add a private brand to it */
static int JS_AddBrand(JSContext *ctx, JSValueConst obj, JSValueConst home_obj)
{
    JSObject *p, *p1;
    JSShapeProperty *prs;
    JSProperty *pr;
    JSValue brand;
    JSAtom brand_atom;

    if (unlikely(JS_VALUE_GET_TAG(home_obj) != JS_TAG_OBJECT)) {
        JS_ThrowTypeErrorNotAnObject(ctx);
        return -1;
    }
    p = JS_VALUE_GET_OBJ(home_obj);
    prs = find_own_property(&pr, p, JS_ATOM_Private_brand);
    if (!prs) {
        /* if the brand is not present, add it */
        brand = JS_NewSymbolFromAtom(ctx, JS_ATOM_brand, JS_ATOM_TYPE_PRIVATE);
        if (JS_IsException(brand))
            return -1;
        pr = add_property(ctx, p, JS_ATOM_Private_brand, JS_PROP_C_W_E);
        if (!pr) {
            JS_FreeValue(ctx, brand);
            return -1;
        }
        pr->u.value = js_dup(brand);
    } else {
        brand = js_dup(pr->u.value);
    }
    brand_atom = js_symbol_to_atom(ctx, brand);

    if (JS_IsObject(obj)) {
        p1 = JS_VALUE_GET_OBJ(obj);
        if (unlikely(!p1->extensible)) {
            JS_FreeAtom(ctx, brand_atom);
            JS_ThrowTypeError(ctx, "object is not extensible");
            return -1;
        }
        prs = find_own_property(&pr, p1, brand_atom);
        if (unlikely(prs)) {
            JS_FreeAtom(ctx, brand_atom);
            JS_ThrowTypeError(ctx, "private method is already present");
            return -1;
        }
        pr = add_property(ctx, p1, brand_atom, JS_PROP_C_W_E);
        JS_FreeAtom(ctx, brand_atom);
        if (!pr)
            return -1;
        pr->u.value = JS_UNDEFINED;
    } else {
        JS_FreeAtom(ctx, brand_atom);
    }

    return 0;
}

/* return a boolean telling if the brand of the home object of 'func'
   is present on 'obj' or -1 in case of exception */
static int JS_CheckBrand(JSContext *ctx, JSValue obj, JSValue func)
{
    JSObject *p, *p1, *home_obj;
    JSShapeProperty *prs;
    JSProperty *pr;
    JSValue brand;

    /* get the home object of 'func' */
    if (unlikely(JS_VALUE_GET_TAG(func) != JS_TAG_OBJECT))
        goto not_obj;
    p1 = JS_VALUE_GET_OBJ(func);
    if (!js_class_has_bytecode(p1->class_id))
        goto not_obj;
    home_obj = p1->u.func.home_object;
    if (!home_obj)
        goto not_obj;
    prs = find_own_property(&pr, home_obj, JS_ATOM_Private_brand);
    if (!prs) {
        JS_ThrowTypeError(ctx, "expecting <brand> private field");
        return -1;
    }
    brand = pr->u.value;
    /* safety check */
    if (unlikely(JS_VALUE_GET_TAG(brand) != JS_TAG_SYMBOL))
        goto not_obj;

    /* get the brand array of 'obj' */
    if (unlikely(JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT)) {
    not_obj:
        JS_ThrowTypeErrorNotAnObject(ctx);
        return -1;
    }
    p = JS_VALUE_GET_OBJ(obj);
    prs = find_own_property(&pr, p, js_symbol_to_atom(ctx, brand));
    return (prs != NULL);
}

static uint32_t js_string_obj_get_length(JSContext *ctx, JSValueConst obj)
{
    JSObject *p;
    JSString *p1;
    uint32_t len = 0;

    /* This is a class exotic method: obj class_id is JS_CLASS_STRING */
    p = JS_VALUE_GET_OBJ(obj);
    if (JS_VALUE_GET_TAG(p->u.object_data) == JS_TAG_STRING) {
        p1 = JS_VALUE_GET_STRING(p->u.object_data);
        len = p1->len;
    }
    return len;
}

static int num_keys_cmp(const void *p1, const void *p2, void *opaque)
{
    JSContext *ctx = opaque;
    JSAtom atom1 = ((const JSPropertyEnum *)p1)->atom;
    JSAtom atom2 = ((const JSPropertyEnum *)p2)->atom;
    uint32_t v1, v2;
    bool atom1_is_integer, atom2_is_integer;

    atom1_is_integer = JS_AtomIsArrayIndex(ctx, &v1, atom1);
    atom2_is_integer = JS_AtomIsArrayIndex(ctx, &v2, atom2);
    assert(atom1_is_integer && atom2_is_integer);
    if (v1 < v2)
        return -1;
    else if (v1 == v2)
        return 0;
    else
        return 1;
}

static void js_free_prop_enum(JSContext *ctx, JSPropertyEnum *tab, uint32_t len)
{
    uint32_t i;
    if (tab) {
        for(i = 0; i < len; i++)
            JS_FreeAtom(ctx, tab[i].atom);
        js_free(ctx, tab);
    }
}

/* return < 0 in case if exception, 0 if OK. ptab and its atoms must
   be freed by the user. */
static int __exception JS_GetOwnPropertyNamesInternal(JSContext *ctx,
                                                      JSPropertyEnum **ptab,
                                                      uint32_t *plen,
                                                      JSObject *p, int flags)
{
    int i, j;
    JSShape *sh;
    JSShapeProperty *prs;
    JSPropertyEnum *tab_atom, *tab_exotic;
    JSAtom atom;
    uint32_t num_keys_count, str_keys_count, sym_keys_count, atom_count;
    uint32_t num_index, str_index, sym_index, exotic_count, exotic_keys_count;
    bool is_enumerable, num_sorted;
    uint32_t num_key;
    JSAtomKindEnum kind;

    /* clear pointer for consistency in case of failure */
    *ptab = NULL;
    *plen = 0;

    /* compute the number of returned properties */
    num_keys_count = 0;
    str_keys_count = 0;
    sym_keys_count = 0;
    exotic_keys_count = 0;
    exotic_count = 0;
    tab_exotic = NULL;
    sh = p->shape;
    for(i = 0, prs = get_shape_prop(sh); i < sh->prop_count; i++, prs++) {
        atom = prs->atom;
        if (atom != JS_ATOM_NULL) {
            is_enumerable = ((prs->flags & JS_PROP_ENUMERABLE) != 0);
            kind = JS_AtomGetKind(ctx, atom);
            if ((!(flags & JS_GPN_ENUM_ONLY) || is_enumerable) &&
                ((flags >> kind) & 1) != 0) {
                /* need to raise an exception in case of the module
                   name space (implicit GetOwnProperty) */
                if (unlikely((prs->flags & JS_PROP_TMASK) == JS_PROP_VARREF) &&
                    (flags & (JS_GPN_SET_ENUM | JS_GPN_ENUM_ONLY))) {
                    JSVarRef *var_ref = p->prop[i].u.var_ref;
                    if (unlikely(JS_IsUninitialized(*var_ref->pvalue))) {
                        JS_ThrowReferenceErrorUninitialized(ctx, prs->atom);
                        return -1;
                    }
                }
                if (JS_AtomIsArrayIndex(ctx, &num_key, atom)) {
                    num_keys_count++;
                } else if (kind == JS_ATOM_KIND_STRING) {
                    str_keys_count++;
                } else {
                    sym_keys_count++;
                }
            }
        }
    }

    if (p->is_exotic) {
        if (p->fast_array) {
            if (flags & JS_GPN_STRING_MASK) {
                num_keys_count += p->u.array.count;
            }
        } else if (p->class_id == JS_CLASS_STRING) {
            if (flags & JS_GPN_STRING_MASK) {
                num_keys_count += js_string_obj_get_length(ctx, JS_MKPTR(JS_TAG_OBJECT, p));
            }
        } else {
            const JSClassExoticMethods *em = ctx->rt->class_array[p->class_id].exotic;
            if (em && em->get_own_property_names) {
                if (em->get_own_property_names(ctx, &tab_exotic, &exotic_count,
                                               JS_MKPTR(JS_TAG_OBJECT, p)))
                    return -1;
                for(i = 0; i < exotic_count; i++) {
                    atom = tab_exotic[i].atom;
                    kind = JS_AtomGetKind(ctx, atom);
                    if (((flags >> kind) & 1) != 0) {
                        is_enumerable = false;
                        if (flags & (JS_GPN_SET_ENUM | JS_GPN_ENUM_ONLY)) {
                            int desc_flags, res;
                            /* set the "is_enumerable" field if necessary */
                            res = JS_GetOwnPropertyFlagsInternal(ctx, &desc_flags, p, atom);
                            if (res < 0) {
                                js_free_prop_enum(ctx, tab_exotic, exotic_count);
                                return -1;
                            }
                            if (res) {
                                is_enumerable =
                                    ((desc_flags & JS_PROP_ENUMERABLE) != 0);
                            }
                            tab_exotic[i].is_enumerable = is_enumerable;
                        }
                        if (!(flags & JS_GPN_ENUM_ONLY) || is_enumerable) {
                            exotic_keys_count++;
                        }
                    }
                }
            }
        }
    }

    /* fill them */

    atom_count = num_keys_count + str_keys_count + sym_keys_count + exotic_keys_count;
    /* avoid allocating 0 bytes */
    tab_atom = js_malloc(ctx, sizeof(tab_atom[0]) * max_int(atom_count, 1));
    if (!tab_atom) {
        js_free_prop_enum(ctx, tab_exotic, exotic_count);
        return -1;
    }

    num_index = 0;
    str_index = num_keys_count;
    sym_index = str_index + str_keys_count;

    num_sorted = true;
    sh = p->shape;
    for(i = 0, prs = get_shape_prop(sh); i < sh->prop_count; i++, prs++) {
        atom = prs->atom;
        if (atom != JS_ATOM_NULL) {
            is_enumerable = ((prs->flags & JS_PROP_ENUMERABLE) != 0);
            kind = JS_AtomGetKind(ctx, atom);
            if ((!(flags & JS_GPN_ENUM_ONLY) || is_enumerable) &&
                ((flags >> kind) & 1) != 0) {
                if (JS_AtomIsArrayIndex(ctx, &num_key, atom)) {
                    j = num_index++;
                    num_sorted = false;
                } else if (kind == JS_ATOM_KIND_STRING) {
                    j = str_index++;
                } else {
                    j = sym_index++;
                }
                tab_atom[j].atom = JS_DupAtom(ctx, atom);
                tab_atom[j].is_enumerable = is_enumerable;
            }
        }
    }

    if (p->is_exotic) {
        int len;
        if (p->fast_array) {
            if (flags & JS_GPN_STRING_MASK) {
                len = p->u.array.count;
                goto add_array_keys;
            }
        } else if (p->class_id == JS_CLASS_STRING) {
            if (flags & JS_GPN_STRING_MASK) {
                len = js_string_obj_get_length(ctx, JS_MKPTR(JS_TAG_OBJECT, p));
            add_array_keys:
                for(i = 0; i < len; i++) {
                    tab_atom[num_index].atom = __JS_AtomFromUInt32(i);
                    if (tab_atom[num_index].atom == JS_ATOM_NULL) {
                        js_free_prop_enum(ctx, tab_atom, num_index);
                        return -1;
                    }
                    tab_atom[num_index].is_enumerable = true;
                    num_index++;
                }
            }
        } else {
            /* Note: exotic keys are not reordered and comes after the object own properties. */
            for(i = 0; i < exotic_count; i++) {
                atom = tab_exotic[i].atom;
                is_enumerable = tab_exotic[i].is_enumerable;
                kind = JS_AtomGetKind(ctx, atom);
                if ((!(flags & JS_GPN_ENUM_ONLY) || is_enumerable) &&
                    ((flags >> kind) & 1) != 0) {
                    tab_atom[sym_index].atom = atom;
                    tab_atom[sym_index].is_enumerable = is_enumerable;
                    sym_index++;
                } else {
                    JS_FreeAtom(ctx, atom);
                }
            }
            js_free(ctx, tab_exotic);
        }
    }

    assert(num_index == num_keys_count);
    assert(str_index == num_keys_count + str_keys_count);
    assert(sym_index == atom_count);

    if (num_keys_count != 0 && !num_sorted) {
        rqsort(tab_atom, num_keys_count, sizeof(tab_atom[0]), num_keys_cmp,
               ctx);
    }
    *ptab = tab_atom;
    *plen = atom_count;
    return 0;
}

int JS_GetOwnPropertyNames(JSContext *ctx, JSPropertyEnum **ptab,
                           uint32_t *plen, JSValueConst obj, int flags)
{
    if (JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT) {
        JS_ThrowTypeErrorNotAnObject(ctx);
        return -1;
    }
    return JS_GetOwnPropertyNamesInternal(ctx, ptab, plen,
                                          JS_VALUE_GET_OBJ(obj), flags);
}

/* Return -1 if exception,
   false if the property does not exist, true if it exists. If true is
   returned, the property descriptor 'desc' is filled present. */
static int JS_GetOwnPropertyInternal2(JSContext *ctx, JSPropertyDescriptor *desc,
                                      JSObject *p, JSAtom prop, int *pflags)
{
    JSShapeProperty *prs;
    JSProperty *pr;
    int flags_only = (desc == NULL && pflags != NULL);

retry:
    prs = find_own_property(&pr, p, prop);
    if (prs) {
        if (desc) {
            desc->flags = prs->flags & JS_PROP_C_W_E;
            desc->getter = JS_UNDEFINED;
            desc->setter = JS_UNDEFINED;
            desc->value = JS_UNDEFINED;
            if (unlikely(prs->flags & JS_PROP_TMASK)) {
                if ((prs->flags & JS_PROP_TMASK) == JS_PROP_GETSET) {
                    desc->flags |= JS_PROP_GETSET;
                    if (pr->u.getset.getter)
                        desc->getter = js_dup(JS_MKPTR(JS_TAG_OBJECT, pr->u.getset.getter));
                    if (pr->u.getset.setter)
                        desc->setter = js_dup(JS_MKPTR(JS_TAG_OBJECT, pr->u.getset.setter));
                } else if ((prs->flags & JS_PROP_TMASK) == JS_PROP_VARREF) {
                    JSValue val = *pr->u.var_ref->pvalue;
                    if (unlikely(JS_IsUninitialized(val))) {
                        JS_ThrowReferenceErrorUninitialized(ctx, prs->atom);
                        return -1;
                    }
                    desc->value = js_dup(val);
                } else if ((prs->flags & JS_PROP_TMASK) == JS_PROP_AUTOINIT) {
                    /* Instantiate property and retry */
                    if (JS_AutoInitProperty(ctx, p, prop, pr, prs))
                        return -1;
                    goto retry;
                }
            } else {
                desc->value = js_dup(pr->u.value);
            }
        } else {
            if (pflags) {
                *pflags = prs->flags & JS_PROP_C_W_E;
                if ((prs->flags & JS_PROP_TMASK) == JS_PROP_GETSET)
                    *pflags |= JS_PROP_GETSET;
            }
            /* for consistency, send the exception even if desc is NULL */
            if (unlikely((prs->flags & JS_PROP_TMASK) == JS_PROP_VARREF)) {
                if (unlikely(JS_IsUninitialized(*pr->u.var_ref->pvalue))) {
                    JS_ThrowReferenceErrorUninitialized(ctx, prs->atom);
                    return -1;
                }
            } else if ((prs->flags & JS_PROP_TMASK) == JS_PROP_AUTOINIT) {
                /* nothing to do: delay instantiation until actual value and/or attributes are read */
            }
        }
        return true;
    }
    if (p->is_exotic) {
        if (p->fast_array) {
            /* specific case for fast arrays */
            if (__JS_AtomIsTaggedInt(prop)) {
                uint32_t idx;
                idx = __JS_AtomToUInt32(prop);
                if (idx < p->u.array.count) {
                    if (desc) {
                        desc->flags = JS_PROP_WRITABLE | JS_PROP_ENUMERABLE |
                            JS_PROP_CONFIGURABLE;
                        desc->getter = JS_UNDEFINED;
                        desc->setter = JS_UNDEFINED;
                        desc->value = JS_GetPropertyUint32(ctx, JS_MKPTR(JS_TAG_OBJECT, p), idx);
                    } else if (flags_only) {
                        *pflags = JS_PROP_WRITABLE | JS_PROP_ENUMERABLE |
                            JS_PROP_CONFIGURABLE;
                    }
                    return true;
                }
            }
        } else {
            const JSClassExoticMethods *em = ctx->rt->class_array[p->class_id].exotic;
            if (em && em->get_own_property) {
                if (flags_only) {
                    /* Call the exotic handler with a temporary desc,
                       extract flags, and free the values. For Proxy
                       objects, the JS trap runs regardless, so the
                       dup+free overhead is negligible. */
                    JSPropertyDescriptor d;
                    int ret = em->get_own_property(ctx, &d,
                                                   JS_MKPTR(JS_TAG_OBJECT, p), prop);
                    if (ret > 0) {
                        *pflags = d.flags;
                        js_free_desc(ctx, &d);
                    }
                    return ret;
                }
                return em->get_own_property(ctx, desc,
                                            JS_MKPTR(JS_TAG_OBJECT, p), prop);
            }
        }
    }
    return false;
}

static int JS_GetOwnPropertyInternal(JSContext *ctx, JSPropertyDescriptor *desc,
                                     JSObject *p, JSAtom prop)
{
    return JS_GetOwnPropertyInternal2(ctx, desc, p, prop, NULL);
}

/* Same as JS_GetOwnPropertyInternal but only returns flags, not
   value/getter/setter. Avoids unnecessary js_dup() for callers that
   only need the property flags. */
static int JS_GetOwnPropertyFlagsInternal(JSContext *ctx, int *pflags,
                                          JSObject *p, JSAtom prop)
{
    return JS_GetOwnPropertyInternal2(ctx, NULL, p, prop, pflags);
}

int JS_GetOwnProperty(JSContext *ctx, JSPropertyDescriptor *desc,
                      JSValueConst obj, JSAtom prop)
{
    if (JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT) {
        JS_ThrowTypeErrorNotAnObject(ctx);
        return -1;
    }
    return JS_GetOwnPropertyInternal(ctx, desc, JS_VALUE_GET_OBJ(obj), prop);
}

void JS_FreePropertyEnum(JSContext *ctx, JSPropertyEnum *tab,
                         uint32_t len)
{
    js_free_prop_enum(ctx, tab, len);
}

/* return -1 if exception (Proxy object only) or true/false */
int JS_IsExtensible(JSContext *ctx, JSValueConst obj)
{
    JSObject *p;

    if (unlikely(JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT))
        return false;
    p = JS_VALUE_GET_OBJ(obj);
    if (unlikely(p->class_id == JS_CLASS_PROXY))
        return js_proxy_isExtensible(ctx, obj);
    else
        return p->extensible;
}

/* return -1 if exception (Proxy object only) or true/false */
int JS_PreventExtensions(JSContext *ctx, JSValueConst obj)
{
    JSObject *p;

    if (unlikely(JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT))
        return false;
    p = JS_VALUE_GET_OBJ(obj);
    if (unlikely(p->class_id == JS_CLASS_PROXY))
        return js_proxy_preventExtensions(ctx, obj);
    p->extensible = false;
    return true;
}

/* return -1 if exception otherwise true or false */
int JS_HasProperty(JSContext *ctx, JSValueConst obj, JSAtom prop)
{
    JSObject *p;
    int ret;
    JSValue obj1;

    if (unlikely(JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT))
        return false;
    p = JS_VALUE_GET_OBJ(obj);
    for(;;) {
        if (p->is_exotic) {
            const JSClassExoticMethods *em = ctx->rt->class_array[p->class_id].exotic;
            if (em && em->has_property) {
                /* has_property can free the prototype */
                obj1 = js_dup(JS_MKPTR(JS_TAG_OBJECT, p));
                ret = em->has_property(ctx, obj1, prop);
                JS_FreeValue(ctx, obj1);
                return ret;
            }
        }
        /* JS_GetOwnPropertyInternal can free the prototype */
        js_dup(JS_MKPTR(JS_TAG_OBJECT, p));
        ret = JS_GetOwnPropertyInternal(ctx, NULL, p, prop);
        JS_FreeValue(ctx, JS_MKPTR(JS_TAG_OBJECT, p));
        if (ret != 0)
            return ret;
        if (is_typed_array(p->class_id)) {
            ret = JS_AtomIsNumericIndex(ctx, prop);
            if (ret != 0) {
                if (ret < 0)
                    return -1;
                return false;
            }
        }
        p = p->shape->proto;
        if (!p)
            break;
    }
    return false;
}

/* val must be a symbol */
static JSAtom js_symbol_to_atom(JSContext *ctx, JSValueConst val)
{
    JSAtomStruct *p = JS_VALUE_GET_PTR(val);
    return js_get_atom_index(ctx->rt, p);
}

/* return JS_ATOM_NULL in case of exception */
static JSAtom JS_ValueToAtomInternal(JSContext *ctx, JSValueConst val,
                                     int flags)
{
    JSAtom atom;
    uint32_t tag;
    tag = JS_VALUE_GET_TAG(val);
    if (tag == JS_TAG_INT &&
        (uint32_t)JS_VALUE_GET_INT(val) <= JS_ATOM_MAX_INT) {
        /* fast path for integer values */
        atom = __JS_AtomFromUInt32(JS_VALUE_GET_INT(val));
    } else if (tag == JS_TAG_SYMBOL) {
        JSAtomStruct *p = JS_VALUE_GET_PTR(val);
        atom = JS_DupAtom(ctx, js_get_atom_index(ctx->rt, p));
    } else {
        JSValue str;
        str = JS_ToPropertyKeyInternal(ctx, val, flags);
        if (JS_IsException(str))
            return JS_ATOM_NULL;
        if (JS_VALUE_GET_TAG(str) == JS_TAG_SYMBOL) {
            atom = js_symbol_to_atom(ctx, str);
        } else {
            atom = JS_NewAtomStr(ctx, JS_VALUE_GET_STRING(str));
        }
    }
    return atom;
}

JSAtom JS_ValueToAtom(JSContext *ctx, JSValueConst val)
{
    return JS_ValueToAtomInternal(ctx, val, /*flags*/0);
}

static bool js_get_fast_array_element(JSContext *ctx, JSObject *p,
                                      uint32_t idx, JSValue *pval)
{
    switch(p->class_id) {
    case JS_CLASS_ARRAY:
    case JS_CLASS_ARGUMENTS:
        if (unlikely(idx >= p->u.array.count)) return false;
        *pval = js_dup(p->u.array.u.values[idx]);
        return true;
    case JS_CLASS_MAPPED_ARGUMENTS:
        if (unlikely(idx >= p->u.array.count)) return false;
        *pval = js_dup(*p->u.array.u.var_refs[idx]->pvalue);
        return true;
    case JS_CLASS_INT8_ARRAY:
        if (unlikely(idx >= p->u.array.count)) return false;
        *pval = js_int32(p->u.array.u.int8_ptr[idx]);
        return true;
    case JS_CLASS_UINT8C_ARRAY:
    case JS_CLASS_UINT8_ARRAY:
        if (unlikely(idx >= p->u.array.count)) return false;
        *pval = js_int32(p->u.array.u.uint8_ptr[idx]);
        return true;
    case JS_CLASS_INT16_ARRAY:
        if (unlikely(idx >= p->u.array.count)) return false;
        *pval = js_int32(p->u.array.u.int16_ptr[idx]);
        return true;
    case JS_CLASS_UINT16_ARRAY:
        if (unlikely(idx >= p->u.array.count)) return false;
        *pval = js_int32(p->u.array.u.uint16_ptr[idx]);
        return true;
    case JS_CLASS_INT32_ARRAY:
        if (unlikely(idx >= p->u.array.count)) return false;
        *pval = js_int32(p->u.array.u.int32_ptr[idx]);
        return true;
    case JS_CLASS_UINT32_ARRAY:
        if (unlikely(idx >= p->u.array.count)) return false;
        *pval = js_uint32(p->u.array.u.uint32_ptr[idx]);
        return true;
    case JS_CLASS_BIG_INT64_ARRAY:
        if (unlikely(idx >= p->u.array.count)) return false;
        *pval = JS_NewBigInt64(ctx, p->u.array.u.int64_ptr[idx]);
        return true;
    case JS_CLASS_BIG_UINT64_ARRAY:
        if (unlikely(idx >= p->u.array.count)) return false;
        *pval = JS_NewBigUint64(ctx, p->u.array.u.uint64_ptr[idx]);
        return true;
    case JS_CLASS_FLOAT16_ARRAY:
        if (unlikely(idx >= p->u.array.count)) return false;
        *pval = js_float64(fromfp16(p->u.array.u.fp16_ptr[idx]));
        return true;
    case JS_CLASS_FLOAT32_ARRAY:
        if (unlikely(idx >= p->u.array.count)) return false;
        *pval = js_float64(p->u.array.u.float_ptr[idx]);
        return true;
    case JS_CLASS_FLOAT64_ARRAY:
        if (unlikely(idx >= p->u.array.count)) return false;
        *pval = js_float64(p->u.array.u.double_ptr[idx]);
        return true;
    default:
        return false;
    }
}

static JSValue JS_GetPropertyValue(JSContext *ctx, JSValueConst this_obj,
                                   JSValue prop)
{
    JSAtom atom;
    JSValue ret;
    uint32_t tag;

    tag = JS_VALUE_GET_TAG(this_obj);
    if (likely(tag == JS_TAG_OBJECT)) {
        if (JS_VALUE_GET_TAG(prop) == JS_TAG_INT) {
            JSObject *p = JS_VALUE_GET_OBJ(this_obj);
            uint32_t idx = JS_VALUE_GET_INT(prop);
            JSValue val;
            /* fast path for array and typed array access */
            if (js_get_fast_array_element(ctx, p, idx, &val))
                return val;
        }
    } else if (unlikely(tag == JS_TAG_NULL || tag == JS_TAG_UNDEFINED)) {
        // per spec: not allowed to call ToPropertyKey before ToObject
        // so we must ensure to not invoke JS anything that's observable
        // from JS code
        atom = JS_ValueToAtomInternal(ctx, prop, JS_TO_STRING_NO_SIDE_EFFECTS);
        JS_FreeValue(ctx, prop);
        if (unlikely(atom == JS_ATOM_NULL))
            return JS_EXCEPTION;
        if (tag == JS_TAG_NULL) {
            JS_ThrowTypeErrorAtom(ctx, "cannot read property '%s' of null", atom);
        } else {
            JS_ThrowTypeErrorAtom(ctx, "cannot read property '%s' of undefined", atom);
        }
        JS_FreeAtom(ctx, atom);
        return JS_EXCEPTION;
    }
    atom = JS_ValueToAtom(ctx, prop);
    JS_FreeValue(ctx, prop);
    if (unlikely(atom == JS_ATOM_NULL))
        return JS_EXCEPTION;
    ret = JS_GetProperty(ctx, this_obj, atom);
    JS_FreeAtom(ctx, atom);
    return ret;
}

JSValue JS_GetPropertyUint32(JSContext *ctx, JSValueConst this_obj,
                             uint32_t idx)
{
    return JS_GetPropertyInt64(ctx, this_obj, idx);
}

/* Check if an object has a generalized numeric property. Return value:
   -1 for exception, *pval set to JS_EXCEPTION
   true if property exists, stored into *pval,
   false if property does not exist. *pval set to JS_UNDEFINED.
 */
static int JS_TryGetPropertyInt64(JSContext *ctx, JSValueConst obj, int64_t idx, JSValue *pval)
{
    JSValue val;
    JSAtom prop;
    int present;

    if (likely(JS_VALUE_GET_TAG(obj) == JS_TAG_OBJECT &&
               (uint64_t)idx <= INT32_MAX)) {
        /* fast path for array and typed array access */
        JSObject *p = JS_VALUE_GET_OBJ(obj);
        if (js_get_fast_array_element(ctx, p, idx, pval))
            return true;
    }
    val = JS_EXCEPTION;
    present = -1;
    prop = JS_NewAtomInt64(ctx, idx);
    if (likely(prop != JS_ATOM_NULL)) {
        present = JS_HasProperty(ctx, obj, prop);
        if (present > 0) {
            val = JS_GetProperty(ctx, obj, prop);
            if (unlikely(JS_IsException(val)))
                present = -1;
        } else if (present == false) {
            val = JS_UNDEFINED;
        }
        JS_FreeAtom(ctx, prop);
    }
    *pval = val;
    return present;
}

JSValue JS_GetPropertyInt64(JSContext *ctx, JSValueConst obj, int64_t idx)
{
    JSAtom prop;
    JSValue val;

    if (likely(JS_VALUE_GET_TAG(obj) == JS_TAG_OBJECT &&
               (uint64_t)idx <= INT32_MAX)) {
        /* fast path for array and typed array access */
        JSObject *p = JS_VALUE_GET_OBJ(obj);
        if (js_get_fast_array_element(ctx, p, idx, &val))
            return val;
    }
    prop = JS_NewAtomInt64(ctx, idx);
    if (prop == JS_ATOM_NULL)
        return JS_EXCEPTION;

    val = JS_GetProperty(ctx, obj, prop);
    JS_FreeAtom(ctx, prop);
    return val;
}

/* `prop` may be pure ASCII or UTF-8 encoded */
