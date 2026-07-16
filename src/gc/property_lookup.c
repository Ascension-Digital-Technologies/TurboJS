/* Engine domain source: gc/gc_exceptions_properties.inc -> property_lookup.
 * Ownership: gc subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

JSValue JS_GetPropertyStr(JSContext *ctx, JSValueConst this_obj,
                          const char *prop)
{
    JSAtom atom;
    JSValue ret;
    atom = JS_NewAtom(ctx, prop);
    if (atom == JS_ATOM_NULL)
        return JS_EXCEPTION;
    ret = JS_GetProperty(ctx, this_obj, atom);
    JS_FreeAtom(ctx, atom);
    return ret;
}

/* Note: the property value is not initialized. Return NULL if memory
   error. */
static JSProperty *add_property(JSContext *ctx,
                                JSObject *p, JSAtom prop, int prop_flags)
{
    JSShape *sh, *new_sh;

    if (unlikely(p->is_prototype)) {
        /* track addition of small integer properties to
           Array.prototype and Object.prototype */
        if (unlikely((p == JS_VALUE_GET_OBJ(ctx->class_proto[JS_CLASS_ARRAY]) ||
                      p == JS_VALUE_GET_OBJ(ctx->class_proto[JS_CLASS_OBJECT])) &&
                     __JS_AtomIsTaggedInt(prop))) {
            ctx->std_array_prototype = false;
        }
    }
    sh = p->shape;
    if (sh->is_hashed) {
        /* try to find an existing shape */
        new_sh = find_hashed_shape_prop(ctx->rt, sh, prop, prop_flags);
        if (new_sh) {
            /* matching shape found: use it */
            /*  the property array may need to be resized */
            if (new_sh->prop_size != sh->prop_size) {
                JSProperty *new_prop;
                new_prop = js_realloc(ctx, p->prop, sizeof(p->prop[0]) *
                                      new_sh->prop_size);
                if (!new_prop)
                    return NULL;
                p->prop = new_prop;
            }
            p->shape = js_dup_shape(new_sh);
            js_free_shape(ctx->rt, sh);
            return &p->prop[new_sh->prop_count - 1];
        } else if (JS_REF_COUNT(sh) != 1) {
            /* if the shape is shared, clone it */
            new_sh = js_clone_shape(ctx, sh);
            if (!new_sh)
                return NULL;
            /* hash the cloned shape */
            new_sh->is_hashed = true;
            js_shape_hash_link(ctx->rt, new_sh);
            js_free_shape(ctx->rt, p->shape);
            p->shape = new_sh;
        }
    }
    assert(JS_REF_COUNT(p->shape) == 1);
    if (add_shape_property(ctx, &p->shape, p, prop, prop_flags))
        return NULL;
    return &p->prop[p->shape->prop_count - 1];
}

/* can be called on JS_CLASS_ARRAY, JS_CLASS_ARGUMENTS or
   JS_CLASS_MAPPED_ARGUMENTS objects. return < 0 if memory alloc
   error. */
static no_inline __exception int convert_fast_array_to_array(JSContext *ctx,
                                                             JSObject *p)
{
    JSProperty *pr;
    JSShape *sh;
    uint32_t i, len, new_count;

    /* track modification of Array.prototype */
    if (unlikely(p == JS_VALUE_GET_OBJ(ctx->class_proto[JS_CLASS_ARRAY]))) {
        ctx->std_array_prototype = false;
    }
    if (js_shape_prepare_update(ctx, p, NULL))
        return -1;
    len = p->u.array.count;
    /* resize the properties once to simplify the error handling */
    sh = p->shape;
    new_count = sh->prop_count + len;
    if (new_count > sh->prop_size) {
        if (resize_properties(ctx, &p->shape, p, new_count))
            return -1;
    }

    if (p->class_id == JS_CLASS_MAPPED_ARGUMENTS) {
        JSVarRef **tab = p->u.array.u.var_refs;
        for(i = 0; i < len; i++) {
            /* add_property cannot fail here but
               __JS_AtomFromUInt32(i) fails for i > INT32_MAX */
            pr = add_property(ctx, p, __JS_AtomFromUInt32(i), JS_PROP_C_W_E | JS_PROP_VARREF);
            pr->u.var_ref = *tab++;
        }
    } else {
        JSValue *tab = p->u.array.u.values;
        for(i = 0; i < len; i++) {
            /* add_property cannot fail here but
               __JS_AtomFromUInt32(i) fails for i > INT32_MAX */
            pr = add_property(ctx, p, __JS_AtomFromUInt32(i), JS_PROP_C_W_E);
            pr->u.value = *tab++;
        }
    }
    js_free(ctx, p->u.array.u.values);
    p->u.array.count = 0;
    p->u.array.u.values = NULL; /* fail safe */
    p->u.array.u1.size = 0;
    p->fast_array = 0;
    return 0;
}

static int delete_property(JSContext *ctx, JSObject *p, JSAtom atom)
{
    JSShape *sh;
    JSShapeProperty *pr, *lpr, *prop;
    JSProperty *pr1;
    uint32_t lpr_idx;
    intptr_t h, h1;

 redo:
    sh = p->shape;
    h1 = atom & sh->prop_hash_mask;
    h = prop_hash_end(sh)[-h1 - 1];
    prop = get_shape_prop(sh);
    lpr = NULL;
    lpr_idx = 0;   /* prevent warning */
    while (h != 0) {
        pr = &prop[h - 1];
        if (likely(pr->atom == atom)) {
            /* found ! */
            if (!(pr->flags & JS_PROP_CONFIGURABLE))
                return false;
            /* realloc the shape if needed */
            if (lpr)
                lpr_idx = lpr - get_shape_prop(sh);
            if (js_shape_prepare_update(ctx, p, &pr))
                return -1;
            sh = p->shape;
            /* remove property */
            if (lpr) {
                lpr = &get_shape_prop(sh)[lpr_idx];
                lpr->hash_next = pr->hash_next;
            } else {
                prop_hash_end(sh)[-h1 - 1] = pr->hash_next;
            }
            sh->deleted_prop_count++;
            /* free the entry */
            pr1 = &p->prop[h - 1];
            free_property(ctx->rt, pr1, pr->flags);
            JS_FreeAtom(ctx, pr->atom);
            /* put default values */
            pr->flags = 0;
            pr->atom = JS_ATOM_NULL;
            pr1->u.value = JS_UNDEFINED;

            /* compact the properties if too many deleted properties */
            if (sh->deleted_prop_count >= 8 &&
                sh->deleted_prop_count >= ((unsigned)sh->prop_count / 2)) {
                compact_properties(ctx, p);
            }
            return true;
        }
        lpr = pr;
        h = pr->hash_next;
    }

    if (p->is_exotic) {
        if (p->fast_array) {
            uint32_t idx;
            if (JS_AtomIsArrayIndex(ctx, &idx, atom) &&
                idx < p->u.array.count) {
                if (p->class_id == JS_CLASS_ARRAY ||
                    p->class_id == JS_CLASS_ARGUMENTS ||
                    p->class_id == JS_CLASS_MAPPED_ARGUMENTS) {
                    if (convert_fast_array_to_array(ctx, p))
                        return -1;
                    goto redo;
                } else {
                    return false;
                }
            }
        } else {
            const JSClassExoticMethods *em = ctx->rt->class_array[p->class_id].exotic;
            if (em && em->delete_property) {
                return em->delete_property(ctx, JS_MKPTR(JS_TAG_OBJECT, p), atom);
            }
        }
    }
    /* not found */
    return true;
}

static int call_setter(JSContext *ctx, JSObject *setter,
                       JSValueConst this_obj, JSValue val, int flags)
{
    JSValue ret, func;
    if (likely(setter)) {
        func = JS_MKPTR(JS_TAG_OBJECT, setter);
        /* Note: the field could be removed in the setter */
        func = js_dup(func);
        ret = JS_CallFree(ctx, func, this_obj, 1, vc(&val));
        JS_FreeValue(ctx, val);
        if (JS_IsException(ret))
            return -1;
        JS_FreeValue(ctx, ret);
        return true;
    } else {
        JS_FreeValue(ctx, val);
        if ((flags & JS_PROP_THROW) ||
            ((flags & JS_PROP_THROW_STRICT) && is_strict_mode(ctx))) {
            JS_ThrowTypeError(ctx, "no setter for property");
            return -1;
        }
        return false;
    }
}

/* set the array length and remove the array elements if necessary. */
static int set_array_length(JSContext *ctx, JSObject *p, JSValue val,
                            int flags)
{
    uint32_t len, idx, cur_len;
    int i, ret;

    /* Note: this call can reallocate the properties of 'p' */
    ret = JS_ToArrayLengthFree(ctx, &len, val, false);
    if (ret)
        return -1;
    /* JS_ToArrayLengthFree() must be done before the read-only test */
    if (unlikely(!(get_shape_prop(p->shape)[0].flags & JS_PROP_WRITABLE)))
        return JS_ThrowTypeErrorReadOnly(ctx, flags, JS_ATOM_length);

    if (likely(p->fast_array)) {
        uint32_t old_len = p->u.array.count;
        if (len < old_len) {
            for(i = len; i < old_len; i++) {
                JS_FreeValue(ctx, p->u.array.u.values[i]);
                p->u.array.u.values[i] = JS_UNDEFINED;
            }
            p->u.array.count = len;
        }
        p->prop[0].u.value = js_uint32(len);
    } else {
        /* Note: length is always a uint32 because the object is an
           array */
        JS_ToUint32(ctx, &cur_len, p->prop[0].u.value);
        if (len < cur_len) {
            uint32_t d;
            JSShape *sh;
            JSShapeProperty *pr;

            d = cur_len - len;
            sh = p->shape;
            if (d <= sh->prop_count) {
                JSAtom atom;

                /* faster to iterate */
                while (cur_len > len) {
                    atom = JS_NewAtomUInt32(ctx, cur_len - 1);
                    ret = delete_property(ctx, p, atom);
                    JS_FreeAtom(ctx, atom);
                    if (unlikely(!ret)) {
                        /* unlikely case: property is not
                           configurable */
                        break;
                    }
                    cur_len--;
                }
            } else {
                /* faster to iterate thru all the properties. Need two
                   passes in case one of the property is not
                   configurable */
                cur_len = len;
                for(i = 0, pr = get_shape_prop(sh); i < sh->prop_count;
                    i++, pr++) {
                    if (pr->atom != JS_ATOM_NULL &&
                        JS_AtomIsArrayIndex(ctx, &idx, pr->atom)) {
                        if (idx >= cur_len &&
                            !(pr->flags & JS_PROP_CONFIGURABLE)) {
                            cur_len = idx + 1;
                        }
                    }
                }

                for(i = 0, pr = get_shape_prop(sh); i < sh->prop_count;
                    i++, pr++) {
                    if (pr->atom != JS_ATOM_NULL &&
                        JS_AtomIsArrayIndex(ctx, &idx, pr->atom)) {
                        if (idx >= cur_len) {
                            /* remove the property */
                            delete_property(ctx, p, pr->atom);
                            /* WARNING: the shape may have been modified */
                            sh = p->shape;
                            pr = &get_shape_prop(sh)[i];
                        }
                    }
                }
            }
        } else {
            cur_len = len;
        }
        set_value(ctx, &p->prop[0].u.value, js_uint32(cur_len));
        if (unlikely(cur_len > len)) {
            return JS_ThrowTypeErrorOrFalse(ctx, flags, "not configurable");
        }
    }
    return true;
}

/* return -1 if exception */
static int expand_fast_array(JSContext *ctx, JSObject *p, uint32_t new_len)
{
    uint32_t old_size, new_size;
    size_t slack;
    JSValue *new_array_prop;

    if (unlikely(new_len > (uint32_t)INT32_MAX)) {
        JS_ThrowOutOfMemory(ctx);
        return -1;
    }

    old_size = p->u.array.u1.size;
    new_size = old_size + old_size/2;
    if (new_size < old_size) {
        JS_ThrowOutOfMemory(ctx);
        return -1;
    }
    new_size = max_uint32(new_len, new_size);
    new_array_prop = js_realloc2(ctx, p->u.array.u.values, sizeof(JSValue) * new_size, &slack);
    if (!new_array_prop)
        return -1;
    new_size += slack / sizeof(*new_array_prop);
    p->u.array.u.values = new_array_prop;
    p->u.array.u1.size = new_size;
    return 0;
}

/* Preconditions: 'p' must be of class JS_CLASS_ARRAY, p->fast_array =
   true and p->extensible = true */
static int add_fast_array_element(JSContext *ctx, JSObject *p,
                                  JSValue val, int flags)
{
    uint32_t new_len, array_len;
    /* extend the array by one */
    /* XXX: convert to slow array if new_len > 2^31-1 elements */
    new_len = p->u.array.count + 1;
    /* update the length if necessary. We assume that if the length is
       not an integer, then if it >= 2^31.  */
    if (likely(JS_VALUE_GET_TAG(p->prop[0].u.value) == JS_TAG_INT)) {
        array_len = JS_VALUE_GET_INT(p->prop[0].u.value);
        if (new_len > array_len) {
            if (unlikely(!(get_shape_prop(p->shape)->flags & JS_PROP_WRITABLE))) {
                JS_FreeValue(ctx, val);
                return JS_ThrowTypeErrorReadOnly(ctx, flags, JS_ATOM_length);
            }
            p->prop[0].u.value = js_int32(new_len);
        }
    }
    if (unlikely(new_len > p->u.array.u1.size)) {
        if (expand_fast_array(ctx, p, new_len)) {
            JS_FreeValue(ctx, val);
            return -1;
        }
    }
    p->u.array.u.values[new_len - 1] = val;
    p->u.array.count = new_len;
    return true;
}

/* Allocate a new fast array initialized to JS_UNDEFINED. Its maximum
   size is 2^31-1 elements. For convenience, 'len' is a 64 bit
   integer. */
static JSValue js_allocate_fast_array(JSContext *ctx, int64_t len)
{
    JSValue arr;
    JSObject *p;
    int i;

    if (len > INT32_MAX)
        return JS_ThrowRangeError(ctx, "invalid array length");
    arr = JS_NewArray(ctx);
    if (JS_IsException(arr))
        return arr;
    if (len > 0) {
        p = JS_VALUE_GET_OBJ(arr);
        if (expand_fast_array(ctx, p, len) < 0) {
            JS_FreeValue(ctx, arr);
            return JS_EXCEPTION;
        }
        p->u.array.count = len;
        for(i = 0; i < len; i++)
            p->u.array.u.values[i] = JS_UNDEFINED;
        /* update the 'length' field */
        set_value(ctx, &p->prop[0].u.value, js_int32(len));
    }
    return arr;
}

static void js_free_desc(JSContext *ctx, JSPropertyDescriptor *desc)
{
    JS_FreeValue(ctx, desc->getter);
    JS_FreeValue(ctx, desc->setter);
    JS_FreeValue(ctx, desc->value);
}

/* return -1 in case of exception or true or false. Warning: 'val' is
   freed by the function. 'flags' is a bitmask of JS_PROP_NO_ADD,
   JS_PROP_THROW or JS_PROP_THROW_STRICT. If JS_PROP_NO_ADD is set,
   the new property is not added and an error is raised.
   'obj' must be an object when obj != this_obj.
   */
static int JS_SetPropertyInternal2(JSContext *ctx, JSValueConst obj, JSAtom prop,
                                   JSValue val, JSValueConst this_obj, int flags)
{
    JSObject *p, *p1;
    JSShapeProperty *prs;
    JSProperty *pr;
    JSPropertyDescriptor desc;
    int desc_flags;
    int ret;

    switch(JS_VALUE_GET_TAG(this_obj)) {
    case JS_TAG_NULL:
        JS_ThrowTypeErrorAtom(ctx, "cannot set property '%s' of null", prop);
        goto fail;
    case JS_TAG_UNDEFINED:
        JS_ThrowTypeErrorAtom(ctx, "cannot set property '%s' of undefined", prop);
        goto fail;
    case JS_TAG_OBJECT:
        p = JS_VALUE_GET_OBJ(this_obj);
        p1 = JS_VALUE_GET_OBJ(obj);
        if (p == p1)
            break;
        goto retry2;
    default:
        if (JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT)
            obj = JS_GetPrototypePrimitive(ctx, obj);
        p = NULL;
        p1 = JS_VALUE_GET_OBJ(obj);
        goto prototype_lookup;
    }

retry:
    prs = find_own_property(&pr, p1, prop);
    if (prs) {
        if (likely((prs->flags & (JS_PROP_TMASK | JS_PROP_WRITABLE |
                                  JS_PROP_LENGTH)) == JS_PROP_WRITABLE)) {
            /* fast case */
            set_value(ctx, &pr->u.value, val);
            return true;
        } else if (prs->flags & JS_PROP_LENGTH) {
            assert(p->class_id == JS_CLASS_ARRAY);
            assert(prop == JS_ATOM_length);
            return set_array_length(ctx, p, val, flags);
        } else if ((prs->flags & JS_PROP_TMASK) == JS_PROP_GETSET) {
            return call_setter(ctx, pr->u.getset.setter, this_obj, val, flags);
        } else if ((prs->flags & JS_PROP_TMASK) == JS_PROP_VARREF) {
            /* JS_PROP_WRITABLE is always true for variable
               references, but they are write protected in module name
               spaces. */
            if (p->class_id == JS_CLASS_MODULE_NS)
                goto read_only_prop;
            set_value(ctx, pr->u.var_ref->pvalue, val);
            return true;
        } else if ((prs->flags & JS_PROP_TMASK) == JS_PROP_AUTOINIT) {
            /* Instantiate property and retry (potentially useless) */
            if (JS_AutoInitProperty(ctx, p, prop, pr, prs))
                goto fail;
            goto retry;
        } else {
            goto read_only_prop;
        }
    }

    for(;;) {
        if (p1->is_exotic) {
            if (p1->fast_array) {
                if (__JS_AtomIsTaggedInt(prop)) {
                    uint32_t idx = __JS_AtomToUInt32(prop);
                    if (idx < p1->u.array.count) {
                        if (unlikely(p == p1))
                            return JS_SetPropertyValue(ctx, this_obj, js_int32(idx), val, flags);
                        else
                            break;
                    } else if (is_typed_array(p1->class_id)) {
                        goto typed_array_oob;
                    }
                } else if (is_typed_array(p1->class_id)) {
                    ret = JS_AtomIsNumericIndex(ctx, prop);
                    if (ret != 0) {
                        if (ret < 0)
                            goto fail;
                    typed_array_oob:
                        // per spec: evaluate value for side effects
                        if (p1->class_id == JS_CLASS_BIG_INT64_ARRAY ||
                            p1->class_id == JS_CLASS_BIG_UINT64_ARRAY) {
                            int64_t v;
                            if (JS_ToBigInt64Free(ctx, &v, val))
                                return -1;
                        } else {
                            val = JS_ToNumberFree(ctx, val);
                            JS_FreeValue(ctx, val);
                            if (JS_IsException(val))
                                return -1;
                        }
                        return true;
                    }
                }
            } else {
                const JSClassExoticMethods *em = ctx->rt->class_array[p1->class_id].exotic;
                if (em) {
                    JSValue obj1;
                    if (em->set_property) {
                        /* set_property can free the prototype */
                        obj1 = js_dup(JS_MKPTR(JS_TAG_OBJECT, p1));
                        ret = em->set_property(ctx, obj1, prop,
                                               val, this_obj, flags);
                        JS_FreeValue(ctx, obj1);
                        JS_FreeValue(ctx, val);
                        return ret;
                    }
                    if (em->get_own_property) {
                        /* get_own_property can free the prototype */
                        obj1 = js_dup(JS_MKPTR(JS_TAG_OBJECT, p1));
                        ret = em->get_own_property(ctx, &desc,
                                                   obj1, prop);
                        JS_FreeValue(ctx, obj1);
                        if (ret < 0)
                            goto fail;
                        if (ret) {
                            if (desc.flags & JS_PROP_GETSET) {
                                JSObject *setter;
                                if (JS_IsUndefined(desc.setter))
                                    setter = NULL;
                                else
                                    setter = JS_VALUE_GET_OBJ(desc.setter);
                                ret = call_setter(ctx, setter, this_obj, val, flags);
                                JS_FreeValue(ctx, desc.getter);
                                JS_FreeValue(ctx, desc.setter);
                                return ret;
                            } else {
                                JS_FreeValue(ctx, desc.value);
                                if (!(desc.flags & JS_PROP_WRITABLE))
                                    goto read_only_prop;
                                if (likely(p == p1)) {
                                    ret = JS_DefineProperty(ctx, this_obj, prop, val,
                                                            JS_UNDEFINED, JS_UNDEFINED,
                                                            JS_PROP_HAS_VALUE);
                                    JS_FreeValue(ctx, val);
                                    return ret;
                                } else {
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        p1 = p1->shape->proto;
    prototype_lookup:
        if (!p1)
            break;

    retry2:
        prs = find_own_property(&pr, p1, prop);
        if (prs) {
            if ((prs->flags & JS_PROP_TMASK) == JS_PROP_GETSET) {
                return call_setter(ctx, pr->u.getset.setter, this_obj, val, flags);
            } else if ((prs->flags & JS_PROP_TMASK) == JS_PROP_AUTOINIT) {
                /* Instantiate property and retry (potentially useless) */
                if (JS_AutoInitProperty(ctx, p1, prop, pr, prs))
                    return -1;
                goto retry2;
            } else if (!(prs->flags & JS_PROP_WRITABLE)) {
                goto read_only_prop;
            } else {
                break;
            }
        }
    }

    if (unlikely(flags & JS_PROP_NO_ADD)) {
        JS_ThrowReferenceErrorNotDefined(ctx, prop);
        goto fail;
    }

    if (unlikely(!p)) {
        ret = JS_ThrowTypeErrorOrFalse(ctx, flags, "not an object");
        goto done;
    }

    if (p == JS_VALUE_GET_OBJ(obj)) {
        if (unlikely(!p->extensible)) {
            ret = JS_ThrowTypeErrorOrFalse(ctx, flags, "object is not extensible");
            goto done;
        }
        if (p->is_exotic) {
            if (p->class_id == JS_CLASS_ARRAY && p->fast_array &&
                __JS_AtomIsTaggedInt(prop)) {
                uint32_t idx = __JS_AtomToUInt32(prop);
                if (idx == p->u.array.count) {
                    /* fast case */
                    return add_fast_array_element(ctx, p, val, flags);
                }
            }
            goto generic_create_prop;
        } else {
            pr = add_property(ctx, p, prop, JS_PROP_C_W_E);
            if (!pr)
                goto fail;
            pr->u.value = val;
            return true;
        }
    }

    // TODO(bnoordhuis) return JSProperty slot and update in place
    // when plain property (not is_exotic/setter/etc.) to avoid
    // calling find_own_property() thrice?
    ret = JS_GetOwnPropertyFlagsInternal(ctx, &desc_flags, p, prop);
    if (ret < 0)
        goto fail;

    if (ret) {
        if (desc_flags & JS_PROP_GETSET) {
            ret = JS_ThrowTypeErrorOrFalse(ctx, flags, "setter is forbidden");
            goto done;
        } else if (!(desc_flags & JS_PROP_WRITABLE) ||
                   p->class_id == JS_CLASS_MODULE_NS) {
        read_only_prop:
            ret = JS_ThrowTypeErrorReadOnly(ctx, flags, prop);
            goto done;
        }
        ret = JS_DefineProperty(ctx, this_obj, prop, val,
                                JS_UNDEFINED, JS_UNDEFINED,
                                JS_PROP_HAS_VALUE);
    } else {
        if (unlikely(!p->extensible)) {
            ret = JS_ThrowTypeErrorOrFalse(ctx, flags, "object is not extensible");
            goto done;
        }
    generic_create_prop:
        ret = JS_CreateProperty(ctx, p, prop, val, JS_UNDEFINED, JS_UNDEFINED,
                                flags |
                                JS_PROP_HAS_VALUE |
                                JS_PROP_HAS_ENUMERABLE |
                                JS_PROP_HAS_WRITABLE |
                                JS_PROP_HAS_CONFIGURABLE |
                                JS_PROP_C_W_E);
    }

done:
    JS_FreeValue(ctx, val);
    return ret;
fail:
    JS_FreeValue(ctx, val);
    return -1;
}

static int JS_SetPropertyInternal(JSContext *ctx, JSValueConst obj, JSAtom prop,
                                  JSValue val, int flags)
{
    return JS_SetPropertyInternal2(ctx, obj, prop, val, obj, flags);
}

int JS_SetProperty(JSContext *ctx, JSValueConst this_obj, JSAtom prop, JSValue val)
{
    return JS_SetPropertyInternal(ctx, this_obj, prop, val, JS_PROP_THROW);
}

/* flags can be JS_PROP_THROW or JS_PROP_THROW_STRICT */
static int JS_SetPropertyValue(JSContext *ctx, JSValueConst this_obj,
                               JSValue prop, JSValue val, int flags)
{
    if (likely(JS_VALUE_GET_TAG(this_obj) == JS_TAG_OBJECT &&
               JS_VALUE_GET_TAG(prop) == JS_TAG_INT)) {
        JSObject *p;
        uint32_t idx;
        double d;
        int32_t v;

        /* fast path for array access */
        p = JS_VALUE_GET_OBJ(this_obj);
        idx = JS_VALUE_GET_INT(prop);
        switch(p->class_id) {
        case JS_CLASS_ARRAY:
            if (unlikely(idx >= (uint32_t)p->u.array.count)) {
                /* fast path to add an element to the array */
                if (unlikely(idx != (uint32_t)p->u.array.count ||
                             !p->fast_array ||
                             !p->extensible ||
                             p->shape->proto != JS_VALUE_GET_OBJ(ctx->class_proto[JS_CLASS_ARRAY]) ||
                             !ctx->std_array_prototype)) {
                    goto slow_path;
                }
                /* add element */
                return add_fast_array_element(ctx, p, val, flags);
            }
            set_value(ctx, &p->u.array.u.values[idx], val);
            break;
        case JS_CLASS_ARGUMENTS:
            if (unlikely(idx >= (uint32_t)p->u.array.count))
                goto slow_path;
            set_value(ctx, &p->u.array.u.values[idx], val);
            break;
        case JS_CLASS_MAPPED_ARGUMENTS:
            if (unlikely(idx >= (uint32_t)p->u.array.count))
                goto slow_path;
            set_value(ctx, p->u.array.u.var_refs[idx]->pvalue, val);
            break;
        case JS_CLASS_UINT8C_ARRAY:
            if (JS_ToUint8ClampFree(ctx, &v, val))
                goto ta_cvt_fail;
            if (typed_array_is_immutable(p))
                goto ta_immutable;
            /* Note: the conversion can detach the typed array, so the
               array bound check must be done after */
            if (unlikely(idx >= (uint32_t)p->u.array.count))
                goto ta_out_of_bound;
            p->u.array.u.uint8_ptr[idx] = v;
            break;
        case JS_CLASS_INT8_ARRAY:
        case JS_CLASS_UINT8_ARRAY:
            if (JS_ToInt32Free(ctx, &v, val))
                goto ta_cvt_fail;
            if (typed_array_is_immutable(p))
                goto ta_immutable;
            if (unlikely(idx >= (uint32_t)p->u.array.count))
                goto ta_out_of_bound;
            p->u.array.u.uint8_ptr[idx] = v;
            break;
        case JS_CLASS_INT16_ARRAY:
        case JS_CLASS_UINT16_ARRAY:
            if (JS_ToInt32Free(ctx, &v, val))
                goto ta_cvt_fail;
            if (typed_array_is_immutable(p))
                goto ta_immutable;
            if (unlikely(idx >= (uint32_t)p->u.array.count))
                goto ta_out_of_bound;
            p->u.array.u.uint16_ptr[idx] = v;
            break;
        case JS_CLASS_INT32_ARRAY:
        case JS_CLASS_UINT32_ARRAY:
            if (JS_ToInt32Free(ctx, &v, val))
                goto ta_cvt_fail;
            if (typed_array_is_immutable(p))
                goto ta_immutable;
            if (unlikely(idx >= (uint32_t)p->u.array.count))
                goto ta_out_of_bound;
            p->u.array.u.uint32_ptr[idx] = v;
            break;
        case JS_CLASS_BIG_INT64_ARRAY:
        case JS_CLASS_BIG_UINT64_ARRAY:
            /* XXX: need specific conversion function */
            {
                int64_t v;
                if (JS_ToBigInt64Free(ctx, &v, val))
                    goto ta_cvt_fail;
                if (typed_array_is_immutable(p))
                    goto ta_immutable;
                if (unlikely(idx >= (uint32_t)p->u.array.count))
                    goto ta_out_of_bound;
                p->u.array.u.uint64_ptr[idx] = v;
            }
            break;
        case JS_CLASS_FLOAT16_ARRAY:
            if (JS_ToFloat64Free(ctx, &d, val))
                goto ta_cvt_fail;
            if (typed_array_is_immutable(p))
                goto ta_immutable;
            if (unlikely(idx >= (uint32_t)p->u.array.count))
                goto ta_out_of_bound;
            p->u.array.u.fp16_ptr[idx] = tofp16(d);
            break;
        case JS_CLASS_FLOAT32_ARRAY:
            if (JS_ToFloat64Free(ctx, &d, val))
                goto ta_cvt_fail;
            if (typed_array_is_immutable(p))
                goto ta_immutable;
            if (unlikely(idx >= (uint32_t)p->u.array.count))
                goto ta_out_of_bound;
            p->u.array.u.float_ptr[idx] = d;
            break;
        case JS_CLASS_FLOAT64_ARRAY:
            if (JS_ToFloat64Free(ctx, &d, val)) {
            ta_cvt_fail:
                if (flags & JS_PROP_REFLECT_DEFINE_PROPERTY) {
                    JS_FreeValue(ctx, JS_GetException(ctx));
                    return false;
                }
                return -1;
            }
            if (typed_array_is_immutable(p)) {
            ta_immutable:
                return false;
            }
            if (unlikely(idx >= (uint32_t)p->u.array.count)) {
            ta_out_of_bound:
                if (typed_array_is_oob(p))
                    if (flags & JS_PROP_DEFINE_PROPERTY)
                        return JS_ThrowTypeErrorOrFalse(ctx, flags, "out-of-bound numeric index");
                return true; // per spec: no OOB exception
            }
            p->u.array.u.double_ptr[idx] = d;
            break;
        default:
            goto slow_path;
        }
        return true;
    } else {
        JSAtom atom;
        int ret;
    slow_path:
        atom = JS_ValueToAtom(ctx, prop);
        JS_FreeValue(ctx, prop);
        if (unlikely(atom == JS_ATOM_NULL)) {
            JS_FreeValue(ctx, val);
            return -1;
        }
        ret = JS_SetPropertyInternal(ctx, this_obj, atom, val, flags);
        JS_FreeAtom(ctx, atom);
        return ret;
    }
}

int JS_SetPropertyUint32(JSContext *ctx, JSValueConst this_obj,
                         uint32_t idx, JSValue val)
{
    return JS_SetPropertyValue(ctx, this_obj, js_uint32(idx), val,
                               JS_PROP_THROW);
}

int JS_SetPropertyInt64(JSContext *ctx, JSValueConst this_obj,
                        int64_t idx, JSValue val)
{
    JSAtom prop;
    int res;

    if ((uint64_t)idx <= INT32_MAX) {
        /* fast path for fast arrays */
        return JS_SetPropertyValue(ctx, this_obj, js_int32(idx), val,
                                   JS_PROP_THROW);
    }
    prop = JS_NewAtomInt64(ctx, idx);
    if (prop == JS_ATOM_NULL) {
        JS_FreeValue(ctx, val);
        return -1;
    }
    res = JS_SetProperty(ctx, this_obj, prop, val);
    JS_FreeAtom(ctx, prop);
    return res;
}

/* `prop` may be pure ASCII or UTF-8 encoded */
int JS_SetPropertyStr(JSContext *ctx, JSValueConst this_obj,
                      const char *prop, JSValue val)
{
    JSAtom atom;
    int ret;
    atom = JS_NewAtom(ctx, prop);
    if (atom == JS_ATOM_NULL) {
        JS_FreeValue(ctx, val);
        return -1;
    }
    ret = JS_SetPropertyInternal(ctx, this_obj, atom, val, JS_PROP_THROW);
    JS_FreeAtom(ctx, atom);
    return ret;
}

/* compute the property flags. For each flag: (JS_PROP_HAS_x forces
   it, otherwise def_flags is used)
   Note: makes assumption about the bit pattern of the flags
*/
static int get_prop_flags(int flags, int def_flags)
{
    int mask;
    mask = (flags >> JS_PROP_HAS_SHIFT) & JS_PROP_C_W_E;
    return (flags & mask) | (def_flags & ~mask);
}

static int JS_CreateProperty(JSContext *ctx, JSObject *p,
                             JSAtom prop, JSValueConst val,
                             JSValueConst getter, JSValueConst setter,
                             int flags)
{
    JSProperty *pr;
    int ret, prop_flags;

    /* add a new property or modify an existing exotic one */
    if (p->is_exotic) {
        if (p->class_id == JS_CLASS_ARRAY) {
            uint32_t idx, len;

            if (p->fast_array) {
                if (__JS_AtomIsTaggedInt(prop)) {
                    idx = __JS_AtomToUInt32(prop);
                    if (idx == p->u.array.count) {
                        if (!p->extensible)
                            goto not_extensible;
                        if (flags & (JS_PROP_HAS_GET | JS_PROP_HAS_SET))
                            goto convert_to_array;
                        prop_flags = get_prop_flags(flags, 0);
                        if (prop_flags != JS_PROP_C_W_E)
                            goto convert_to_array;
                        return add_fast_array_element(ctx, p,
                                                      js_dup(val), flags);
                    } else {
                        goto convert_to_array;
                    }
                } else if (JS_AtomIsArrayIndex(ctx, &idx, prop)) {
                    /* convert the fast array to normal array */
                convert_to_array:
                    if (convert_fast_array_to_array(ctx, p))
                        return -1;
                    goto generic_array;
                }
            } else if (JS_AtomIsArrayIndex(ctx, &idx, prop)) {
                JSProperty *plen;
                JSShapeProperty *pslen;
            generic_array:
                /* update the length field */
                plen = &p->prop[0];
                JS_ToUint32(ctx, &len, plen->u.value);
                if ((idx + 1) > len) {
                    pslen = get_shape_prop(p->shape);
                    if (unlikely(!(pslen->flags & JS_PROP_WRITABLE)))
                        return JS_ThrowTypeErrorReadOnly(ctx, flags, JS_ATOM_length);
                    /* XXX: should update the length after defining
                       the property */
                    len = idx + 1;
                    set_value(ctx, &plen->u.value, js_uint32(len));
                }
            }
        } else if (is_typed_array(p->class_id)) {
            ret = JS_AtomIsNumericIndex(ctx, prop);
            if (ret != 0) {
                if (ret < 0)
                    return -1;
                return JS_ThrowTypeErrorOrFalse(ctx, flags, "cannot create numeric index in typed array");
            }
        } else if (!(flags & JS_PROP_NO_EXOTIC)) {
            const JSClassExoticMethods *em = ctx->rt->class_array[p->class_id].exotic;
            if (em) {
                if (em->define_own_property) {
                    return em->define_own_property(ctx, JS_MKPTR(JS_TAG_OBJECT, p),
                                                   prop, val, getter, setter, flags);
                }
                ret = JS_IsExtensible(ctx, JS_MKPTR(JS_TAG_OBJECT, p));
                if (ret < 0)
                    return -1;
                if (!ret)
                    goto not_extensible;
            }
        }
    }

    if (!p->extensible) {
    not_extensible:
        return JS_ThrowTypeErrorOrFalse(ctx, flags, "object is not extensible");
    }

    if (flags & (JS_PROP_HAS_GET | JS_PROP_HAS_SET)) {
        prop_flags = (flags & (JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE)) |
            JS_PROP_GETSET;
    } else {
        prop_flags = flags & JS_PROP_C_W_E;
    }
    pr = add_property(ctx, p, prop, prop_flags);
    if (unlikely(!pr))
        return -1;
    if (flags & (JS_PROP_HAS_GET | JS_PROP_HAS_SET)) {
        pr->u.getset.getter = NULL;
        if ((flags & JS_PROP_HAS_GET) && JS_IsFunction(ctx, getter)) {
            pr->u.getset.getter =
                JS_VALUE_GET_OBJ(js_dup(getter));
        }
        pr->u.getset.setter = NULL;
        if ((flags & JS_PROP_HAS_SET) && JS_IsFunction(ctx, setter)) {
            pr->u.getset.setter =
                JS_VALUE_GET_OBJ(js_dup(setter));
        }
    } else {
        if (flags & JS_PROP_HAS_VALUE) {
            pr->u.value = js_dup(val);
        } else {
            pr->u.value = JS_UNDEFINED;
        }
    }
    return true;
}

/* return false if not OK */
