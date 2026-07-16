/* Engine domain source: builtins/bigint_typedarray.inc -> bigint_builtin.
 * Ownership: builtins subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

/* BigInt */

static JSValue JS_ToBigIntCtorFree(JSContext *ctx, JSValue val)
{
    uint32_t tag;

 redo:
    tag = JS_VALUE_GET_NORM_TAG(val);
    switch(tag) {
    case JS_TAG_INT:
    case JS_TAG_BOOL:
        val = JS_NewBigInt64(ctx, JS_VALUE_GET_INT(val));
        break;
    case JS_TAG_SHORT_BIG_INT:
    case JS_TAG_BIG_INT:
        break;
    case JS_TAG_FLOAT64:
        {
            double d = JS_VALUE_GET_FLOAT64(val);
            JSBigInt *r;
            int res;
            r = js_bigint_from_float64(ctx, &res, d);
            if (!r) {
                if (res == 0) {
                    val = JS_EXCEPTION;
                } else if (res == 1) {
                    val = JS_ThrowRangeError(ctx, "cannot convert to BigInt: not an integer");
                } else {
                    val = JS_ThrowRangeError(ctx, "cannot convert NaN or Infinity to BigInt");                }
            } else {
                val = JS_CompactBigInt(ctx, r);
            }
        }
        break;
    case JS_TAG_STRING:
    case JS_TAG_STRING_ROPE:
        val = JS_StringToBigIntErr(ctx, val);
        break;
    case JS_TAG_OBJECT:
        val = JS_ToPrimitiveFree(ctx, val, HINT_NUMBER);
        if (JS_IsException(val))
            break;
        goto redo;
    case JS_TAG_NULL:
    case JS_TAG_UNDEFINED:
    default:
        JS_FreeValue(ctx, val);
        return JS_ThrowTypeError(ctx, "cannot convert to BigInt");
    }
    return val;
}

static JSValue js_bigint_constructor(JSContext *ctx,
                                     JSValueConst new_target,
                                     int argc, JSValueConst *argv)
{
    if (!JS_IsUndefined(new_target))
        return JS_ThrowTypeErrorNotAConstructor(ctx, new_target);
    return JS_ToBigIntCtorFree(ctx, js_dup(argv[0]));
}

static JSValue js_thisBigIntValue(JSContext *ctx, JSValueConst this_val)
{
    if (JS_IsBigInt(this_val))
        return js_dup(this_val);

    if (JS_VALUE_GET_TAG(this_val) == JS_TAG_OBJECT) {
        JSObject *p = JS_VALUE_GET_OBJ(this_val);
        if (p->class_id == JS_CLASS_BIG_INT) {
            if (JS_IsBigInt(p->u.object_data))
                return js_dup(p->u.object_data);
        }
    }
    return JS_ThrowTypeError(ctx, "not a BigInt");
}

static JSValue js_bigint_toString(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    JSValue val;
    int base;
    JSValue ret;

    val = js_thisBigIntValue(ctx, this_val);
    if (JS_IsException(val))
        return val;
    if (argc == 0 || JS_IsUndefined(argv[0])) {
        base = 10;
    } else {
        base = js_get_radix(ctx, argv[0]);
        if (base < 0)
            goto fail;
    }
    ret = js_bigint_to_string1(ctx, val, base);
    JS_FreeValue(ctx, val);
    return ret;
 fail:
    JS_FreeValue(ctx, val);
    return JS_EXCEPTION;
}

static JSValue js_bigint_valueOf(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    return js_thisBigIntValue(ctx, this_val);
}

static JSValue js_bigint_asUintN(JSContext *ctx,
                                  JSValueConst this_val,
                                  int argc, JSValueConst *argv, int asIntN)
{
    uint64_t bits;
    JSValue res, a;

    if (JS_ToIndex(ctx, &bits, argv[0]))
        return JS_EXCEPTION;
    a = JS_ToBigInt(ctx, argv[1]);
    if (JS_IsException(a))
        return JS_EXCEPTION;
    if (bits == 0) {
        JS_FreeValue(ctx, a);
        res = __JS_NewShortBigInt(ctx, 0);
    } else if (JS_VALUE_GET_TAG(a) == JS_TAG_SHORT_BIG_INT) {
        /* fast case */
        if (bits >= JS_SHORT_BIG_INT_BITS) {
            res = a;
        } else {
            uint64_t v;
            int shift;
            shift = 64 - bits;
            v = JS_VALUE_GET_SHORT_BIG_INT(a);
            v = v << shift;
            if (asIntN)
                v = (int64_t)v >> shift;
            else
                v = v >> shift;
            res = __JS_NewShortBigInt(ctx, v);
        }
    } else {
        JSBigInt *r, *p = JS_VALUE_GET_PTR(a);
        if (bits >= p->len * JS_LIMB_BITS) {
            res = a;
        } else {
            int len, shift, i;
            js_limb_t v;
            len = (bits + JS_LIMB_BITS - 1) / JS_LIMB_BITS;
            r = js_bigint_new(ctx, len);
            if (!r) {
                JS_FreeValue(ctx, a);
                return JS_EXCEPTION;
            }
            r->len = len;
            for(i = 0; i < len - 1; i++)
                r->tab[i] = p->tab[i];
            shift = (-bits) & (JS_LIMB_BITS - 1);
            /* 0 <= shift <= JS_LIMB_BITS - 1 */
            v = p->tab[len - 1] << shift;
            if (asIntN)
                v = (js_slimb_t)v >> shift;
            else
                v = v >> shift;
            r->tab[len - 1] = v;
            r = js_bigint_normalize(ctx, r);
            JS_FreeValue(ctx, a);
            res = JS_CompactBigInt(ctx, r);
        }
    }
    return res;
}

static const JSCFunctionListEntry js_bigint_funcs[] = {
    JS_CFUNC_MAGIC_DEF("asUintN", 2, js_bigint_asUintN, 0 ),
    JS_CFUNC_MAGIC_DEF("asIntN", 2, js_bigint_asUintN, 1 ),
};

static const JSCFunctionListEntry js_bigint_proto_funcs[] = {
    JS_CFUNC_DEF("toString", 0, js_bigint_toString ),
    JS_CFUNC_DEF("valueOf", 0, js_bigint_valueOf ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "BigInt", JS_PROP_CONFIGURABLE ),
};

int JS_AddIntrinsicBigInt(JSContext *ctx)
{
    JSValue obj1;

    obj1 = JS_NewCConstructor(ctx, JS_CLASS_BIG_INT, "BigInt",
                              js_bigint_constructor, 1, JS_CFUNC_constructor_or_func, 0,
                              JS_UNDEFINED,
                              js_bigint_funcs, countof(js_bigint_funcs),
                              js_bigint_proto_funcs, countof(js_bigint_proto_funcs),
                              0);
    if (JS_IsException(obj1))
        return -1;
    JS_FreeValue(ctx, obj1);
    return 0;
}

static const char * const native_error_name[JS_NATIVE_ERROR_COUNT] = {
    "EvalError", "RangeError", "ReferenceError",
    "SyntaxError", "TypeError", "URIError",
    "InternalError", "AggregateError",
    "SuppressedError",
};

/* Minimum amount of objects to be able to compile code and display
   error messages. No JSAtom should be allocated by this function. */
/* Minimum amount of objects to be able to compile code and display
   error messages. */
static int JS_AddIntrinsicBasicObjects(JSContext *ctx)
{
    JSValue proto;
    int i;

    /* warning: ordering is tricky */
    ctx->class_proto[JS_CLASS_OBJECT] =
        JS_NewObjectProtoClassAlloc(ctx, JS_NULL, JS_CLASS_OBJECT,
                                    countof(js_object_proto_funcs) + 1);
    if (JS_IsException(ctx->class_proto[JS_CLASS_OBJECT]))
        return -1;

    /* 2 more properties: caller and arguments */
    ctx->function_proto = JS_NewCFunction3(ctx, js_function_proto, "", 0,
                                           JS_CFUNC_generic, 0,
                                           ctx->class_proto[JS_CLASS_OBJECT],
                                           countof(js_function_proto_funcs) + 3 + 2);
    if (JS_IsException(ctx->function_proto))
        return -1;
    ctx->class_proto[JS_CLASS_BYTECODE_FUNCTION] = js_dup(ctx->function_proto);

    ctx->global_obj = JS_NewObjectProtoClassAlloc(ctx, ctx->class_proto[JS_CLASS_OBJECT],
                                                  JS_CLASS_OBJECT, 64);
    if (JS_IsException(ctx->global_obj))
        return -1;
    ctx->global_var_obj = JS_NewObjectProtoClassAlloc(ctx, JS_NULL,
                                                      JS_CLASS_OBJECT, 16);
    if (JS_IsException(ctx->global_var_obj))
        return -1;

    ctx->class_proto[JS_CLASS_ERROR] = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, ctx->class_proto[JS_CLASS_ERROR],
                               js_error_proto_funcs,
                               countof(js_error_proto_funcs));

    for(i = 0; i < JS_NATIVE_ERROR_COUNT; i++) {
        proto = JS_NewObjectProto(ctx, ctx->class_proto[JS_CLASS_ERROR]);
        JS_DefinePropertyValue(ctx, proto, JS_ATOM_name,
                               JS_NewAtomString(ctx, native_error_name[i]),
                               JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
        JS_DefinePropertyValue(ctx, proto, JS_ATOM_message,
                               js_empty_string(ctx->rt),
                               JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
        ctx->native_error_proto[i] = proto;
    }

    /* the array prototype is an array */
    ctx->class_proto[JS_CLASS_ARRAY] =
        JS_NewObjectProtoClass(ctx, ctx->class_proto[JS_CLASS_OBJECT],
                               JS_CLASS_ARRAY);
    ctx->std_array_prototype = true;

    static const JSShapeProperty array_props[] = {
        {.atom=JS_ATOM_length,          .flags=JS_PROP_WRITABLE|JS_PROP_LENGTH},
    };
    static const JSShapeProperty arguments_props[] = {
        {.atom=JS_ATOM_length,          .flags=JS_PROP_WRITABLE|JS_PROP_CONFIGURABLE},
        {.atom=JS_ATOM_Symbol_iterator, .flags=JS_PROP_WRITABLE|JS_PROP_CONFIGURABLE},
        {.atom=JS_ATOM_callee,          .flags=JS_PROP_GETSET},
    };
    static const JSShapeProperty mapped_arguments_props[] = {
        {.atom=JS_ATOM_length,          .flags=JS_PROP_WRITABLE|JS_PROP_CONFIGURABLE},
        {.atom=JS_ATOM_Symbol_iterator, .flags=JS_PROP_WRITABLE|JS_PROP_CONFIGURABLE},
        {.atom=JS_ATOM_callee,          .flags=JS_PROP_WRITABLE|JS_PROP_CONFIGURABLE},
    };
    if (js_new_shape_with(ctx, &ctx->array_shape,
                          ctx->class_proto[JS_CLASS_ARRAY],
                          countof(array_props), array_props)) {
        return -1;
    }
    if (js_new_shape_with(ctx, &ctx->arguments_shape,
                          ctx->class_proto[JS_CLASS_OBJECT],
                          countof(arguments_props), arguments_props)) {
        return -1;
    }
    if (js_new_shape_with(ctx, &ctx->mapped_arguments_shape,
                          ctx->class_proto[JS_CLASS_OBJECT],
                          countof(mapped_arguments_props), mapped_arguments_props)) {
        return -1;
    }

    return 0;
}

int JS_AddIntrinsicBaseObjects(JSContext *ctx)
{
    JSValue obj1, obj2;
    JSCFunctionType ft;

    ctx->throw_type_error = JS_NewCFunction(ctx, js_throw_type_error, NULL, 0);
    if (JS_IsException(ctx->throw_type_error))
        return -1;
    /* add caller and arguments properties to throw a TypeError */
    if (JS_DefineProperty(ctx, ctx->function_proto, JS_ATOM_caller, JS_UNDEFINED,
                          ctx->throw_type_error, ctx->throw_type_error,
                          JS_PROP_HAS_GET | JS_PROP_HAS_SET |
                          JS_PROP_HAS_CONFIGURABLE | JS_PROP_CONFIGURABLE) < 0)
        return -1;
    if (JS_DefineProperty(ctx, ctx->function_proto, JS_ATOM_arguments, JS_UNDEFINED,
                          ctx->throw_type_error, ctx->throw_type_error,
                          JS_PROP_HAS_GET | JS_PROP_HAS_SET |
                          JS_PROP_HAS_CONFIGURABLE | JS_PROP_CONFIGURABLE) < 0)
        return -1;
    JS_FreeValue(ctx, js_object_seal(ctx, JS_UNDEFINED, 1, vc(&ctx->throw_type_error), 1));

    /* Object */
    obj1 = JS_NewCConstructor(ctx, JS_CLASS_OBJECT, "Object",
                              js_object_constructor, 1, JS_CFUNC_constructor_or_func, 0,
                              JS_UNDEFINED,
                              js_object_funcs, countof(js_object_funcs),
                              js_object_proto_funcs, countof(js_object_proto_funcs),
                              JS_NEW_CTOR_PROTO_EXIST);
    if (JS_IsException(obj1))
        return -1;
    JS_FreeValue(ctx, obj1);

    /* Function */
    ft.generic_magic = js_function_constructor;
    obj1 = JS_NewCConstructor(ctx, JS_CLASS_BYTECODE_FUNCTION, "Function",
                              ft.generic, 1, JS_CFUNC_constructor_or_func_magic, JS_FUNC_NORMAL,
                              JS_UNDEFINED,
                              NULL, 0,
                              js_function_proto_funcs, countof(js_function_proto_funcs),
                              JS_NEW_CTOR_PROTO_EXIST);
    if (JS_IsException(obj1))
        return -1;
    ctx->function_ctor = obj1;

    /* Error */
    ft.generic_magic = js_error_constructor;
    ctx->error_ctor = JS_NewCFunctionMagic(ctx, js_error_constructor,
                                           "Error", 1, JS_CFUNC_constructor_or_func_magic, -1);
    if (JS_IsException(ctx->error_ctor))
        return -1;
    JS_NewGlobalCConstructor2(ctx, js_dup(ctx->error_ctor),
                              "Error", ctx->class_proto[JS_CLASS_ERROR]);
    if (JS_SetPropertyFunctionList(ctx, ctx->error_ctor, js_error_funcs, countof(js_error_funcs)))
        return -1;

    for(int i = 0; i < JS_NATIVE_ERROR_COUNT; i++) {
        JSValue func_obj;
        int n_args;
        switch (i) {
        case JS_AGGREGATE_ERROR:
            n_args = 2;
            break;
        case JS_SUPPRESSED_ERROR:
            n_args = 3;
            break;
        default:
            n_args = 1;
        }
        func_obj = JS_NewCFunction3(ctx, ft.generic,
                                    native_error_name[i], n_args,
                                    JS_CFUNC_constructor_or_func_magic, i,
                                    ctx->error_ctor, 0);
        if (JS_IsException(func_obj))
            return -1;
        JS_NewGlobalCConstructor2(ctx, func_obj, native_error_name[i],
                                  ctx->native_error_proto[i]);
    }

    /* CallSite */
    if (_JS_AddIntrinsicCallSite(ctx))
        return -1;

    /* Iterator */
    obj2 = JS_NewCConstructor(ctx, JS_CLASS_ITERATOR, "Iterator",
                              js_iterator_constructor, 0, JS_CFUNC_constructor_or_func, 0,
                              JS_UNDEFINED,
                              js_iterator_funcs, countof(js_iterator_funcs),
                              js_iterator_proto_funcs, countof(js_iterator_proto_funcs),
                              0);
    if (JS_IsException(obj2))
        return -1;
    // quirk: Iterator.prototype.constructor is an accessor property. The getter
    // (magic == 0) returns %Iterator%; the setter (magic == 1, length 1)
    // implements SetterThatIgnoresPrototypeProperties. They must be separate
    // function objects so that magic, not argc, distinguishes get from set.
    // TODO(bnoordhuis) mildly inefficient because JS_NewCConstructor
    // first creates a .constructor value property that we then replace with
    // an accessor
    {
        JSValue getter, setter;
        getter = JS_NewCFunctionData(ctx, js_iterator_constructor_getset,
                                     0, 0, 1, vc(&obj2));
        if (JS_IsException(getter)) {
            JS_FreeValue(ctx, obj2);
            return -1;
        }
        setter = JS_NewCFunctionData(ctx, js_iterator_constructor_getset,
                                     1, 1, 1, vc(&obj2));
        if (JS_IsException(setter)) {
            JS_FreeValue(ctx, getter);
            JS_FreeValue(ctx, obj2);
            return -1;
        }
        ctx->iterator_ctor_getset = js_dup(getter);
        if (JS_DefineProperty(ctx, ctx->class_proto[JS_CLASS_ITERATOR],
                              JS_ATOM_constructor, JS_UNDEFINED,
                              getter, setter,
                              JS_PROP_HAS_GET | JS_PROP_HAS_SET | JS_PROP_CONFIGURABLE) < 0) {
            JS_FreeValue(ctx, getter);
            JS_FreeValue(ctx, setter);
            JS_FreeValue(ctx, obj2);
            return -1;
        }
        JS_FreeValue(ctx, getter);
        JS_FreeValue(ctx, setter);
    }
    ctx->iterator_ctor = obj2;
    JS_DefineAutoInitProperty(ctx, obj2, JS_ATOM_zip, JS_AUTOINIT_ID_BYTECODE,
                              (void *)(uintptr_t)JS_BUILTIN_ITERATOR_ZIP,
                              JS_PROP_WRITABLE|JS_PROP_CONFIGURABLE);
    JS_DefineAutoInitProperty(ctx, obj2, JS_ATOM_zipKeyed, JS_AUTOINIT_ID_BYTECODE,
                              (void *)(uintptr_t)JS_BUILTIN_ITERATOR_ZIP_KEYED,
                              JS_PROP_WRITABLE|JS_PROP_CONFIGURABLE);

    ctx->class_proto[JS_CLASS_ITERATOR_CONCAT] =
        JS_NewObjectProtoList(ctx, ctx->class_proto[JS_CLASS_ITERATOR],
                              js_iterator_concat_proto_funcs,
                              countof(js_iterator_concat_proto_funcs));
    if (JS_IsException(ctx->class_proto[JS_CLASS_ITERATOR_CONCAT]))
        return -1;

    ctx->class_proto[JS_CLASS_ITERATOR_HELPER] =
        JS_NewObjectProtoList(ctx, ctx->class_proto[JS_CLASS_ITERATOR],
                              js_iterator_helper_proto_funcs,
                              countof(js_iterator_helper_proto_funcs));
    if (JS_IsException(ctx->class_proto[JS_CLASS_ITERATOR_HELPER]))
        return -1;

    ctx->class_proto[JS_CLASS_ITERATOR_WRAP] =
        JS_NewObjectProtoList(ctx, ctx->class_proto[JS_CLASS_ITERATOR],
                              js_iterator_wrap_proto_funcs,
                              countof(js_iterator_wrap_proto_funcs));
    if (JS_IsException(ctx->class_proto[JS_CLASS_ITERATOR_WRAP]))
        return -1;

    /* Array */
    obj1 = JS_NewCConstructor(ctx, JS_CLASS_ARRAY, "Array",
                              js_array_constructor, 1, JS_CFUNC_constructor_or_func, 0,
                              JS_UNDEFINED,
                              js_array_funcs, countof(js_array_funcs),
                              js_array_proto_funcs, countof(js_array_proto_funcs),
                              JS_NEW_CTOR_PROTO_EXIST);
    if (JS_IsException(obj1))
        return -1;
    ctx->array_ctor = obj1;
    JS_DefineAutoInitProperty(ctx, obj1, JS_ATOM_fromAsync,
                              JS_AUTOINIT_ID_BYTECODE,
                              (void *)(uintptr_t)JS_BUILTIN_ARRAY_FROMASYNC,
                              JS_PROP_WRITABLE|JS_PROP_CONFIGURABLE);

    /* needed to initialize arguments[Symbol.iterator] */
    ctx->array_proto_values =
        JS_GetProperty(ctx, ctx->class_proto[JS_CLASS_ARRAY], JS_ATOM_values);
    if (JS_IsException(ctx->array_proto_values))
        return -1;

    ctx->class_proto[JS_CLASS_ARRAY_ITERATOR] =
        JS_NewObjectProtoList(ctx, ctx->class_proto[JS_CLASS_ITERATOR],
                              js_array_iterator_proto_funcs,
                              countof(js_array_iterator_proto_funcs));
    if (JS_IsException(ctx->class_proto[JS_CLASS_ARRAY_ITERATOR]))
        return -1;

    /* parseFloat and parseInteger must be defined before Number
       because of the Number.parseFloat and Number.parseInteger
       aliases */
    if (JS_SetPropertyFunctionList(ctx, ctx->global_obj, js_global_funcs,
                                   countof(js_global_funcs)))
        return -1;

    /* Number */
    obj1 = JS_NewCConstructor(ctx, JS_CLASS_NUMBER, "Number",
                              js_number_constructor, 1, JS_CFUNC_constructor_or_func, 0,
                              JS_UNDEFINED,
                              js_number_funcs, countof(js_number_funcs),
                              js_number_proto_funcs, countof(js_number_proto_funcs),
                              JS_NEW_CTOR_PROTO_CLASS);
    if (JS_IsException(obj1))
        return -1;
    JS_FreeValue(ctx, obj1);
    if (JS_SetObjectData(ctx, ctx->class_proto[JS_CLASS_NUMBER], js_int32(0)))
        return -1;

    /* Boolean */
    obj1 = JS_NewCConstructor(ctx, JS_CLASS_BOOLEAN, "Boolean",
                              js_boolean_constructor, 1, JS_CFUNC_constructor_or_func, 0,
                              JS_UNDEFINED,
                              NULL, 0,
                              js_boolean_proto_funcs, countof(js_boolean_proto_funcs),
                              JS_NEW_CTOR_PROTO_CLASS);
    if (JS_IsException(obj1))
        return -1;
    JS_FreeValue(ctx, obj1);
    if (JS_SetObjectData(ctx, ctx->class_proto[JS_CLASS_BOOLEAN], JS_FALSE))
        return -1;

    /* String */
    obj1 = JS_NewCConstructor(ctx, JS_CLASS_STRING, "String",
                              js_string_constructor, 1, JS_CFUNC_constructor_or_func, 0,
                              JS_UNDEFINED,
                              js_string_funcs, countof(js_string_funcs),
                              js_string_proto_funcs, countof(js_string_proto_funcs),
                              JS_NEW_CTOR_PROTO_CLASS);
    if (JS_IsException(obj1))
        return -1;
    JS_FreeValue(ctx, obj1);
    if (JS_SetObjectData(ctx, ctx->class_proto[JS_CLASS_STRING], js_empty_string(ctx->rt)))
        return -1;

    ctx->class_proto[JS_CLASS_STRING_ITERATOR] =
        JS_NewObjectProtoList(ctx, ctx->class_proto[JS_CLASS_ITERATOR],
                              js_string_iterator_proto_funcs,
                              countof(js_string_iterator_proto_funcs));
    if (JS_IsException(ctx->class_proto[JS_CLASS_STRING_ITERATOR]))
        return -1;

    /* Math: create as autoinit object */
    js_random_init(ctx);
    if (JS_SetPropertyFunctionList(ctx, ctx->global_obj, js_math_obj, countof(js_math_obj)))
        return -1;

    /* ES6 Reflect: create as autoinit object */
    if (JS_SetPropertyFunctionList(ctx, ctx->global_obj, js_reflect_obj, countof(js_reflect_obj)))
        return -1;

    /* ES6 Symbol */
    obj1 = JS_NewCConstructor(ctx, JS_CLASS_SYMBOL, "Symbol",
                              js_symbol_constructor, 0, JS_CFUNC_constructor_or_func, 0,
                              JS_UNDEFINED,
                              js_symbol_funcs, countof(js_symbol_funcs),
                              js_symbol_proto_funcs, countof(js_symbol_proto_funcs),
                              0);
    if (JS_IsException(obj1))
        return -1;
    JS_FreeValue(ctx, obj1);

    /* ES6 Generator */
    ctx->class_proto[JS_CLASS_GENERATOR] =
        JS_NewObjectProtoList(ctx, ctx->class_proto[JS_CLASS_ITERATOR],
                              js_generator_proto_funcs,
                              countof(js_generator_proto_funcs));
    if (JS_IsException(ctx->class_proto[JS_CLASS_GENERATOR]))
        return -1;

    ft.generic_magic = js_function_constructor;
    obj1 = JS_NewCConstructor(ctx, JS_CLASS_GENERATOR_FUNCTION, "GeneratorFunction",
                              ft.generic, 1, JS_CFUNC_constructor_or_func_magic, JS_FUNC_GENERATOR,
                              ctx->function_ctor,
                              NULL, 0,
                              js_generator_function_proto_funcs,
                              countof(js_generator_function_proto_funcs),
                              JS_NEW_CTOR_NO_GLOBAL | JS_NEW_CTOR_READONLY);
    if (JS_IsException(obj1))
        return -1;
    JS_FreeValue(ctx, obj1);
    if (JS_SetConstructor2(ctx, ctx->class_proto[JS_CLASS_GENERATOR_FUNCTION],
                           ctx->class_proto[JS_CLASS_GENERATOR],
                           JS_PROP_CONFIGURABLE, JS_PROP_CONFIGURABLE))
        return -1;

    /* explicit resource management */
    ctx->class_proto[JS_CLASS_DISPOSABLE_STACK] = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, ctx->class_proto[JS_CLASS_DISPOSABLE_STACK],
                               js_disposable_stack_proto_funcs,
                               countof(js_disposable_stack_proto_funcs));
    JS_NewGlobalCConstructorMagic(ctx, "DisposableStack",
                                  js_disposable_stack_constructor, 0,
                                  ctx->class_proto[JS_CLASS_DISPOSABLE_STACK],
                                  JS_CLASS_DISPOSABLE_STACK);

    /* global properties */
    ctx->eval_obj = JS_GetProperty(ctx, ctx->global_obj, JS_ATOM_eval);
    if (JS_IsException(ctx->eval_obj))
        return -1;

    if (JS_DefinePropertyValue(ctx, ctx->global_obj, JS_ATOM_globalThis,
                               js_dup(ctx->global_obj),
                               JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE) < 0)
        return -1;

    /* BigInt */
    if (JS_AddIntrinsicBigInt(ctx))
        return -1;
    return 0;
}

/* Typed Arrays */

static uint8_t const typed_array_size_log2[JS_TYPED_ARRAY_COUNT] = {
    0, 0, 0, 1, 1, 2, 2,
    3, 3,                   // BigInt64Array, BigUint64Array
    1, 2, 3                 // Float16Array, Float32Array, Float64Array
};

static JSValue js_array_buffer_constructor3(JSContext *ctx,
                                            JSValueConst new_target,
                                            uint64_t len, uint64_t *max_len,
                                            JSClassID class_id,
                                            uint8_t *buf,
                                            JSFreeArrayBufferDataFunc *free_func,
                                            void *opaque, bool alloc_flag)
{
    JSRuntime *rt = ctx->rt;
    JSValue obj;
    JSArrayBuffer *abuf = NULL;
    uint64_t sab_alloc_len;

    if (!alloc_flag && buf && max_len && free_func != js_array_buffer_free) {
        // not observable from JS land, only through C API misuse;
        // JS code cannot create externally managed buffers directly
        return JS_ThrowInternalError(ctx,
                                     "resizable ArrayBuffers not supported "
                                     "for externally managed buffers");
    }
    obj = js_create_from_ctor(ctx, new_target, class_id);
    if (JS_IsException(obj))
        return obj;
    /* XXX: we are currently limited to 2 GB */
    if (len > INT32_MAX) {
        JS_ThrowRangeError(ctx, "invalid array buffer length");
        goto fail;
    }
    if (max_len && *max_len > INT32_MAX) {
        JS_ThrowRangeError(ctx, "invalid max array buffer length");
        goto fail;
    }
    if (alloc_flag && class_id == JS_CLASS_SHARED_ARRAY_BUFFER && max_len &&
        *max_len > len && !rt->sab_funcs.sab_alloc) {
        JS_ThrowTypeError(ctx,
                          "growable SharedArrayBuffer requires "
                          "SAB allocator hooks");
        goto fail;
    }
    abuf = js_malloc(ctx, sizeof(*abuf));
    if (!abuf)
        goto fail;
    abuf->byte_length = len;
    abuf->max_byte_length = max_len ? *max_len : -1;
    if (alloc_flag) {
        if (class_id == JS_CLASS_SHARED_ARRAY_BUFFER &&
            rt->sab_funcs.sab_alloc) {
            // TOOD(bnoordhuis) resizing backing memory for SABs atomically
            // is hard so we cheat and allocate |maxByteLength| bytes upfront
            sab_alloc_len = max_len ? *max_len : len;
            abuf->data = rt->sab_funcs.sab_alloc(rt->sab_funcs.sab_opaque,
                                                 max_int(sab_alloc_len, 1));
            if (!abuf->data)
                goto fail;
            memset(abuf->data, 0, sab_alloc_len);
        } else {
            /* the allocation must be done after the object creation */
            abuf->data = js_mallocz(ctx, max_int(len, 1));
            if (!abuf->data)
                goto fail;
        }
    } else {
        if (class_id == JS_CLASS_SHARED_ARRAY_BUFFER &&
            rt->sab_funcs.sab_dup) {
            rt->sab_funcs.sab_dup(rt->sab_funcs.sab_opaque, buf);
        }
        if (buf) {
            abuf->data = buf;
        } else {
            abuf->data = js_mallocz(ctx, 1);
            if (!abuf->data)
                goto fail;
            free_func = js_array_buffer_free;
        }
    }
    init_list_head(&abuf->array_list);
    abuf->detached = false;
    abuf->immutable = false;
    abuf->shared = (class_id == JS_CLASS_SHARED_ARRAY_BUFFER);
    abuf->opaque = opaque;
    abuf->free_func = free_func;
    if (alloc_flag && buf)
        memcpy(abuf->data, buf, len);
    JS_SetOpaqueInternal(obj, abuf);
    return obj;
 fail:
    JS_FreeValue(ctx, obj);
    js_free(ctx, abuf);
    return JS_EXCEPTION;
}

static void js_array_buffer_free(JSRuntime *rt, void *opaque, void *ptr)
{
    js_free_rt(rt, ptr);
}

static JSValue js_array_buffer_constructor2(JSContext *ctx,
                                            JSValueConst new_target,
                                            uint64_t len, uint64_t *max_len,
                                            JSClassID class_id)
{
    return js_array_buffer_constructor3(ctx, new_target, len, max_len,
                                        class_id, NULL, js_array_buffer_free,
                                        NULL, true);
}

static JSValue js_array_buffer_constructor1(JSContext *ctx,
                                            JSValueConst new_target,
                                            uint64_t len, uint64_t *max_len)
{
    return js_array_buffer_constructor2(ctx, new_target, len, max_len,
                                        JS_CLASS_ARRAY_BUFFER);
}

JSValue JS_NewArrayBuffer(JSContext *ctx, uint8_t *buf, size_t len,
                          JSFreeArrayBufferDataFunc *free_func, void *opaque,
                          bool is_shared)
{
    JSClassID class_id =
        is_shared ? JS_CLASS_SHARED_ARRAY_BUFFER : JS_CLASS_ARRAY_BUFFER;
    return js_array_buffer_constructor3(ctx, JS_UNDEFINED, len, NULL, class_id,
                                        buf, free_func, opaque, false);
}

bool JS_IsArrayBuffer(JSValueConst obj) {
    return JS_GetClassID(obj) == JS_CLASS_ARRAY_BUFFER;
}

/* create a new ArrayBuffer of length 'len' and copy 'buf' to it */
JSValue JS_NewArrayBufferCopy(JSContext *ctx, const uint8_t *buf, size_t len)
{
    return js_array_buffer_constructor3(ctx, JS_UNDEFINED, len, NULL,
                                        JS_CLASS_ARRAY_BUFFER,
                                        (uint8_t *)buf,
                                        js_array_buffer_free, NULL,
                                        true);
}

static JSValue js_array_buffer_constructor0(JSContext *ctx, JSValueConst new_target,
                                            int argc, JSValueConst *argv,
                                            JSClassID class_id)
{
    uint64_t len, max_len, *pmax_len = NULL;
    JSValue obj, val;
    int64_t i;

    if (JS_ToIndex(ctx, &len, argv[0]))
        return JS_EXCEPTION;
    if (argc < 2)
        goto next;
    if (!JS_IsObject(argv[1]))
        goto next;
    obj = JS_ToObject(ctx, argv[1]);
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    val = JS_GetProperty(ctx, obj, JS_ATOM_maxByteLength);
    JS_FreeValue(ctx, obj);
    if (JS_IsException(val))
        return JS_EXCEPTION;
    if (JS_IsUndefined(val))
        goto next;
    if (JS_ToInt64Free(ctx, &i, val))
        return JS_EXCEPTION;
    // don't have to check i < 0 because len >= 0
    if (len > i || i > MAX_SAFE_INTEGER)
        return JS_ThrowRangeError(ctx, "invalid array buffer max length");
    max_len = i;
    pmax_len = &max_len;
next:
    return js_array_buffer_constructor2(ctx, new_target, len, pmax_len,
                                        class_id);
}

static JSValue js_array_buffer_constructor(JSContext *ctx, JSValueConst new_target,
                                           int argc, JSValueConst *argv)
{
    return js_array_buffer_constructor0(ctx, new_target, argc, argv,
                                        JS_CLASS_ARRAY_BUFFER);
}

static JSValue js_shared_array_buffer_constructor(JSContext *ctx,
                                                  JSValueConst new_target,
                                                  int argc, JSValueConst *argv)
{
    return js_array_buffer_constructor0(ctx, new_target, argc, argv,
                                        JS_CLASS_SHARED_ARRAY_BUFFER);
}

/* also used for SharedArrayBuffer */
static void js_array_buffer_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSObject *p = JS_VALUE_GET_OBJ(val);
    JSArrayBuffer *abuf = p->u.array_buffer;
    struct list_head *el, *el1;

    if (abuf) {
        /* The ArrayBuffer finalizer may be called before the typed
           array finalizers using it, so abuf->array_list is not
           necessarily empty. */
        list_for_each_safe(el, el1, &abuf->array_list) {
            JSTypedArray *ta;
            JSObject *p1;

            ta = list_entry(el, JSTypedArray, link);
            ta->link.prev = NULL;
            ta->link.next = NULL;
            p1 = ta->obj;
            /* Note: the typed array length and offset fields are not modified */
            if (p1->class_id != JS_CLASS_DATAVIEW) {
                p1->u.array.count = 0;
                p1->u.array.u.ptr = NULL;
            }
        }
        if (abuf->shared && rt->sab_funcs.sab_free) {
            rt->sab_funcs.sab_free(rt->sab_funcs.sab_opaque, abuf->data);
        } else {
            if (abuf->free_func)
                abuf->free_func(rt, abuf->opaque, abuf->data);
        }
        js_free_rt(rt, abuf);
    }
}

static JSValue js_array_buffer_isView(JSContext *ctx,
                                      JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    JSObject *p;

    if (JS_VALUE_GET_TAG(argv[0]) == JS_TAG_OBJECT) {
        p = JS_VALUE_GET_OBJ(argv[0]);
        return js_bool(is_typed_array(p->class_id) ||
                       p->class_id == JS_CLASS_DATAVIEW);
    }
    return JS_FALSE;
}

static const JSCFunctionListEntry js_array_buffer_funcs[] = {
    JS_CFUNC_DEF("isView", 1, js_array_buffer_isView ),
    JS_CGETSET_DEF("[Symbol.species]", js_get_this, NULL ),
};

static JSValue JS_ThrowTypeErrorDetachedArrayBuffer(JSContext *ctx)
{
    return JS_ThrowTypeError(ctx, "ArrayBuffer is detached");
}

static JSValue JS_ThrowTypeErrorImmutableArrayBuffer(JSContext *ctx)
{
    return JS_ThrowTypeError(ctx, "ArrayBuffer is immutable");
}

static JSValue JS_ThrowTypeErrorArrayBufferOOB(JSContext *ctx)
{
    return JS_ThrowTypeError(ctx, "ArrayBuffer is detached or resized");
}

// #sec-get-arraybuffer.prototype.detached
static JSValue js_array_buffer_get_detached(JSContext *ctx,
                                            JSValueConst this_val)
{
    JSArrayBuffer *abuf = JS_GetOpaque2(ctx, this_val, JS_CLASS_ARRAY_BUFFER);
    if (!abuf)
        return JS_EXCEPTION;
    if (abuf->shared)
        return JS_ThrowTypeError(ctx, "detached called on SharedArrayBuffer");
    return js_bool(abuf->detached);
}

static JSValue js_array_buffer_get_immutable(JSContext *ctx,
                                             JSValueConst this_val)
{
    JSArrayBuffer *abuf = JS_GetOpaque2(ctx, this_val, JS_CLASS_ARRAY_BUFFER);
    if (!abuf)
        return JS_EXCEPTION;
    return js_bool(abuf->immutable);
}

static JSValue js_array_buffer_get_byteLength(JSContext *ctx,
                                              JSValueConst this_val,
                                              int class_id)
{
    JSArrayBuffer *abuf = JS_GetOpaque2(ctx, this_val, class_id);
    if (!abuf)
        return JS_EXCEPTION;
    /* return 0 if detached */
    return js_uint32(abuf->byte_length);
}

static JSValue js_array_buffer_get_maxByteLength(JSContext *ctx,
                                                 JSValueConst this_val,
                                                 int class_id)
{
    JSArrayBuffer *abuf = JS_GetOpaque2(ctx, this_val, class_id);
    if (!abuf)
        return JS_EXCEPTION;
    if (array_buffer_is_resizable(abuf))
        return js_uint32(abuf->max_byte_length);
    return js_uint32(abuf->byte_length);
}

static JSValue js_array_buffer_get_resizable(JSContext *ctx,
                                             JSValueConst this_val,
                                             int class_id)
{
    JSArrayBuffer *abuf = JS_GetOpaque2(ctx, this_val, class_id);
    if (!abuf)
        return JS_EXCEPTION;
    return js_bool(array_buffer_is_resizable(abuf));
}

void JS_DetachArrayBuffer(JSContext *ctx, JSValueConst obj)
{
    JSArrayBuffer *abuf = JS_GetOpaque(obj, JS_CLASS_ARRAY_BUFFER);
    struct list_head *el;

    if (!abuf || abuf->detached)
        return;
    if (abuf->free_func)
        abuf->free_func(ctx->rt, abuf->opaque, abuf->data);
    abuf->data = NULL;
    abuf->byte_length = 0;
    abuf->detached = true;

    list_for_each(el, &abuf->array_list) {
        JSTypedArray *ta;
        JSObject *p;

        ta = list_entry(el, JSTypedArray, link);
        p = ta->obj;
        /* Note: the typed array length and offset fields are not modified */
        if (p->class_id != JS_CLASS_DATAVIEW) {
            p->u.array.count = 0;
            p->u.array.u.ptr = NULL;
        }
    }
}

int JS_IsImmutableArrayBuffer(JSValueConst obj)
{
    JSArrayBuffer *abuf = JS_GetOpaque(obj, JS_CLASS_ARRAY_BUFFER);
    if (!abuf)
        return -1;
    return abuf->immutable;
}

int JS_SetImmutableArrayBuffer(JSValueConst obj, bool immutable)
{
    JSArrayBuffer *abuf = JS_GetOpaque(obj, JS_CLASS_ARRAY_BUFFER);
    if (!abuf)
        return -1;
    abuf->immutable = immutable;
    return 0;
}

/* get an ArrayBuffer or SharedArrayBuffer */
static JSArrayBuffer *js_get_array_buffer(JSContext *ctx, JSValueConst obj)
{
    JSObject *p;
    if (JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT)
        goto fail;
    p = JS_VALUE_GET_OBJ(obj);
    if (p->class_id != JS_CLASS_ARRAY_BUFFER &&
        p->class_id != JS_CLASS_SHARED_ARRAY_BUFFER) {
    fail:
        JS_ThrowTypeErrorInvalidClass(ctx, JS_CLASS_ARRAY_BUFFER);
        return NULL;
    }
    return p->u.array_buffer;
}

/* return NULL if exception. WARNING: any JS call can detach the
   buffer and render the returned pointer invalid */
uint8_t *JS_GetArrayBuffer(JSContext *ctx, size_t *psize, JSValueConst obj)
{
    JSArrayBuffer *abuf = js_get_array_buffer(ctx, obj);
    if (!abuf)
        goto fail;
    if (abuf->detached) {
        JS_ThrowTypeErrorDetachedArrayBuffer(ctx);
        goto fail;
    }
    *psize = abuf->byte_length;
    return abuf->data;
 fail:
    *psize = 0;
    return NULL;
}

static bool array_buffer_is_resizable(const JSArrayBuffer *abuf)
{
    return abuf->max_byte_length >= 0;
}

static void js_array_buffer_update_typed_arrays(JSArrayBuffer *abuf)
{
    uint32_t size_log2, size_elem;
    struct list_head *el;
    JSTypedArray *ta;
    JSObject *p;
    uint8_t *data;
    int64_t len;

    len = abuf->byte_length;
    data = abuf->data;
    list_for_each(el, &abuf->array_list) {
        ta = list_entry(el, JSTypedArray, link);
        p = ta->obj;
        if (p->class_id == JS_CLASS_DATAVIEW) {
            if (ta->track_rab && ta->offset < len)
                ta->length = len - ta->offset;
            continue;
        }
        p->u.array.count = 0;
        p->u.array.u.ptr = NULL;
        size_log2 = typed_array_size_log2(p->class_id);
        size_elem = 1 << size_log2;
        if (ta->track_rab) {
            if (len >= (int64_t)ta->offset + size_elem) {
                p->u.array.count = (len - ta->offset) >> size_log2;
                p->u.array.u.ptr = &data[ta->offset];
            }
        } else {
            if (len >= (int64_t)ta->offset + ta->length) {
                p->u.array.count = ta->length >> size_log2;
                p->u.array.u.ptr = &data[ta->offset];
            }
        }
    }
}

enum {
    JS_ARRAY_BUFFER_TRANSFER,
    JS_ARRAY_BUFFER_TRANSFER_TO_IMMUTABLE,
    JS_ARRAY_BUFFER_TRANSFER_TO_FIXED_LENGTH,
};

// ES #sec-arraybuffer.prototype.transfer
static JSValue js_array_buffer_transfer(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv, int magic)
{
    JSArrayBuffer *abuf;
    uint64_t new_len, old_len, max_len, *pmax_len;
    uint8_t *bs, *new_bs;
    JSValue ret;

    abuf = JS_GetOpaque2(ctx, this_val, JS_CLASS_ARRAY_BUFFER);
    if (!abuf)
        return JS_EXCEPTION;
    if (abuf->shared)
        return JS_ThrowTypeError(ctx, "cannot transfer a SharedArrayBuffer");
    // Spec (ArrayBufferCopyAndDetach): the newLength argument must be
    // coerced (its valueOf observed) before the buffer's detachability /
    // immutability is checked, so side effects of coercion are visible.
    if (argc < 1 || JS_IsUndefined(argv[0]))
        new_len = abuf->byte_length;
    else if (JS_ToIndex(ctx, &new_len, argv[0]))
        return JS_EXCEPTION;
    if (abuf->immutable)
        return JS_ThrowTypeErrorImmutableArrayBuffer(ctx);
    if (abuf->detached)
        return JS_ThrowTypeErrorDetachedArrayBuffer(ctx);
    pmax_len = NULL;
    if (magic == JS_ARRAY_BUFFER_TRANSFER) {
        if (array_buffer_is_resizable(abuf)) { // carry over maxByteLength
            max_len = abuf->max_byte_length;
            if (new_len > max_len)
                return JS_ThrowTypeError(ctx, "invalid array buffer length");
            // TODO(bnoordhuis) support externally managed RABs
            if (abuf->free_func == js_array_buffer_free)
                pmax_len = &max_len;
        }
    }
    /* create an empty AB */
    if (new_len == 0) {
        JS_DetachArrayBuffer(ctx, this_val);
        ret = js_array_buffer_constructor2(ctx, JS_UNDEFINED, 0, pmax_len,
                                           JS_CLASS_ARRAY_BUFFER);
        goto fini;
    }
    bs = abuf->data;
    old_len = abuf->byte_length;
    /* if length mismatch, realloc. Otherwise, use the same backing buffer. */
    if (new_len != old_len) {
        new_bs = js_realloc(ctx, bs, new_len);
        if (!new_bs)
            return JS_EXCEPTION;
        bs = new_bs;
        if (new_len > old_len)
            memset(bs + old_len, 0, new_len - old_len);
    }
    /* neuter the backing buffer */
    abuf->data = NULL;
    abuf->byte_length = 0;
    abuf->detached = true;
    js_array_buffer_update_typed_arrays(abuf);
    ret = js_array_buffer_constructor3(ctx, JS_UNDEFINED, new_len, pmax_len,
                                       JS_CLASS_ARRAY_BUFFER, bs,
                                       abuf->free_func, NULL,
                                       /*alloc_flag*/false);
fini:
    if (magic == JS_ARRAY_BUFFER_TRANSFER_TO_IMMUTABLE) {
        if (JS_IsException(ret))
            return JS_EXCEPTION;
        abuf = JS_GetOpaque2(ctx, ret, JS_CLASS_ARRAY_BUFFER);
        if (!abuf)
            return JS_EXCEPTION;
        abuf->immutable = true;
    }
    return ret;
}

static JSValue js_array_buffer_resize(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv, int class_id)
{
    JSArrayBuffer *abuf;
    uint8_t *data;
    int64_t len;

    abuf = JS_GetOpaque2(ctx, this_val, class_id);
    if (!abuf)
        return JS_EXCEPTION;
    if (abuf->immutable)
        return JS_ThrowTypeErrorImmutableArrayBuffer(ctx);
    if (JS_ToInt64(ctx, &len, argv[0]))
        return JS_EXCEPTION;
    if (abuf->detached)
        return JS_ThrowTypeErrorDetachedArrayBuffer(ctx);
    if (!array_buffer_is_resizable(abuf))
        return JS_ThrowTypeError(ctx, "array buffer is not resizable");
    // TODO(bnoordhuis) support externally managed RABs
    if (abuf->free_func != js_array_buffer_free)
        return JS_ThrowTypeError(ctx, "external array buffer is not resizable");
    if (len < 0 || len > abuf->max_byte_length) {
    bad_length:
        return JS_ThrowRangeError(ctx, "invalid array buffer length");
    }
    // SABs can only grow and we don't need to realloc because
    // js_array_buffer_constructor3 commits all memory upfront;
    // regular RABs are resizable both ways and realloc
    if (abuf->shared) {
        if (len < abuf->byte_length)
            goto bad_length;
        // Note this is off-spec; there's supposed to be a single atomic
        // |byteLength| property that's shared across SABs but we store
        // it per SAB instead. That means when thread A calls sab.grow(2)
        // at time t0, and thread B calls sab.grow(1) at time t1, we don't
        // throw a TypeError in thread B as the spec says we should,
        // instead both threads get their own view of the backing memory,
        // 2 bytes big in A, and 1 byte big in B
        abuf->byte_length = len;
    } else {
        data = js_realloc(ctx, abuf->data, max_int(len, 1));
        if (!data)
            return JS_EXCEPTION;
        if (len > abuf->byte_length)
            memset(&data[abuf->byte_length], 0, len - abuf->byte_length);
        abuf->byte_length = len;
        abuf->data = data;
    }
    js_array_buffer_update_typed_arrays(abuf);
    return JS_UNDEFINED;
}

static JSValue js_array_buffer_slice(JSContext *ctx,
                                     JSValueConst this_val,
                                     int argc, JSValueConst *argv, int magic)
{
    JSArrayBuffer *abuf, *new_abuf;
    int64_t len, start, end, new_len;
    JSValue ctor, new_obj;
    bool immutable;
    int class_id;

    immutable = magic & 1;
    class_id = magic >> 1;
    abuf = JS_GetOpaque2(ctx, this_val, class_id);
    if (!abuf)
        return JS_EXCEPTION;
    if (abuf->detached)
        return JS_ThrowTypeErrorDetachedArrayBuffer(ctx);
    if (abuf->immutable)
        return JS_ThrowTypeErrorImmutableArrayBuffer(ctx);
    len = abuf->byte_length;

    if (JS_ToInt64Clamp(ctx, &start, argv[0], 0, len, len))
        return JS_EXCEPTION;

    end = len;
    if (!JS_IsUndefined(argv[1])) {
        if (JS_ToInt64Clamp(ctx, &end, argv[1], 0, len, len))
            return JS_EXCEPTION;
    }
    new_len = max_int64(end - start, 0);
    // note: difference between slice and sliceToImmutable is that the
    // latter does not have this step:
    //        1. Let _ctor_ be ? SpeciesConstructor(_O_, %ArrayBuffer%)
    ctor = JS_UNDEFINED;
    if (!immutable) {
        ctor = JS_SpeciesConstructor(ctx, this_val, JS_UNDEFINED);
        if (JS_IsException(ctor))
            return ctor;
    }
    if (JS_IsUndefined(ctor)) {
        new_obj = js_array_buffer_constructor2(ctx, JS_UNDEFINED, new_len,
                                               NULL, class_id);
    } else {
        JSValue args[1];
        args[0] = js_int64(new_len);
        new_obj = JS_CallConstructor(ctx, ctor, 1, vc(args));
        JS_FreeValue(ctx, ctor);
        JS_FreeValue(ctx, args[0]);
    }
    if (JS_IsException(new_obj))
        return new_obj;
    new_abuf = JS_GetOpaque2(ctx, new_obj, class_id);
    if (!new_abuf)
        goto fail;
    if (new_abuf->immutable) {
        JS_ThrowTypeErrorImmutableArrayBuffer(ctx);
        goto fail;
    }
    if (js_same_value(ctx, new_obj, this_val)) {
        JS_ThrowTypeError(ctx, "cannot use identical ArrayBuffer");
        goto fail;
    }
    if (new_abuf->detached) {
        JS_ThrowTypeErrorDetachedArrayBuffer(ctx);
        goto fail;
    }
    if (new_abuf->byte_length < new_len) {
        JS_ThrowTypeError(ctx, "new ArrayBuffer is too small");
        goto fail;
    }
    /* must test again because of side effects */
    if (abuf->detached) {
        JS_ThrowTypeErrorDetachedArrayBuffer(ctx);
        goto fail;
    }
    if (abuf->byte_length < start + new_len) {
        if (immutable)
            JS_ThrowRangeError(ctx, "invalid array buffer length");
        else
            JS_ThrowTypeErrorDetachedArrayBuffer(ctx);
        goto fail;
    }
    memcpy(new_abuf->data, abuf->data + start, new_len);
    new_abuf->immutable = immutable;
    return new_obj;
 fail:
    JS_FreeValue(ctx, new_obj);
    return JS_EXCEPTION;
}

static const JSCFunctionListEntry js_array_buffer_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("byteLength", js_array_buffer_get_byteLength, NULL, JS_CLASS_ARRAY_BUFFER ),
    JS_CGETSET_MAGIC_DEF("maxByteLength", js_array_buffer_get_maxByteLength, NULL, JS_CLASS_ARRAY_BUFFER ),
    JS_CGETSET_MAGIC_DEF("resizable", js_array_buffer_get_resizable, NULL, JS_CLASS_ARRAY_BUFFER ),
    JS_CGETSET_DEF("detached", js_array_buffer_get_detached, NULL ),
    JS_CGETSET_DEF("immutable", js_array_buffer_get_immutable, NULL ),
    JS_CFUNC_MAGIC_DEF("resize", 1, js_array_buffer_resize, JS_CLASS_ARRAY_BUFFER ),
    JS_CFUNC_MAGIC_DEF("slice", 2, js_array_buffer_slice, JS_CLASS_ARRAY_BUFFER*2 + /*immutable*/0 ),
    JS_CFUNC_MAGIC_DEF("sliceToImmutable", 2, js_array_buffer_slice, JS_CLASS_ARRAY_BUFFER*2 + /*immutable*/1 ),
    JS_CFUNC_MAGIC_DEF("transfer", 0, js_array_buffer_transfer, JS_ARRAY_BUFFER_TRANSFER ),
    JS_CFUNC_MAGIC_DEF("transferToImmutable", 0, js_array_buffer_transfer, JS_ARRAY_BUFFER_TRANSFER_TO_IMMUTABLE ),
    JS_CFUNC_MAGIC_DEF("transferToFixedLength", 0, js_array_buffer_transfer, JS_ARRAY_BUFFER_TRANSFER_TO_FIXED_LENGTH ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "ArrayBuffer", JS_PROP_CONFIGURABLE ),
};

/* SharedArrayBuffer */

static const JSCFunctionListEntry js_shared_array_buffer_funcs[] = {
    JS_CGETSET_DEF("[Symbol.species]", js_get_this, NULL ),
};

static const JSCFunctionListEntry js_shared_array_buffer_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("byteLength", js_array_buffer_get_byteLength, NULL, JS_CLASS_SHARED_ARRAY_BUFFER ),
    JS_CGETSET_MAGIC_DEF("maxByteLength", js_array_buffer_get_maxByteLength, NULL, JS_CLASS_SHARED_ARRAY_BUFFER ),
    JS_CGETSET_MAGIC_DEF("growable", js_array_buffer_get_resizable, NULL, JS_CLASS_SHARED_ARRAY_BUFFER ),
    JS_CFUNC_MAGIC_DEF("grow", 1, js_array_buffer_resize, JS_CLASS_SHARED_ARRAY_BUFFER ),
    JS_CFUNC_MAGIC_DEF("slice", 2, js_array_buffer_slice, JS_CLASS_SHARED_ARRAY_BUFFER*2 + /*immutable*/0 ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "SharedArrayBuffer", JS_PROP_CONFIGURABLE ),
};

static bool is_typed_array(JSClassID class_id)
{
    return class_id >= JS_CLASS_UINT8C_ARRAY && class_id <= JS_CLASS_FLOAT64_ARRAY;
}

// |p| must be a typed array, *not* a DataView
static bool typed_array_is_immutable(JSObject *p)
{
    JSArrayBuffer *abuf;
    JSTypedArray *ta;

    assert(is_typed_array(p->class_id));
    ta = p->u.typed_array;
    abuf = ta->buffer->u.array_buffer;
    return abuf->immutable;
}

// is the typed array detached or out of bounds relative to its RAB?
// |p| must be a typed array, *not* a DataView
static bool typed_array_is_oob(JSObject *p)
{
    JSArrayBuffer *abuf;
    JSTypedArray *ta;
    int len, size_elem;
    int64_t end;

    assert(is_typed_array(p->class_id));

    ta = p->u.typed_array;
    abuf = ta->buffer->u.array_buffer;
    if (abuf->detached)
        return true;
    len = abuf->byte_length;
    if (ta->offset > len)
        return true;
    if (ta->track_rab)
        return false;
    if (len < (int64_t)ta->offset + ta->length)
        return true;
    size_elem = 1 << typed_array_size_log2(p->class_id);
    end = (int64_t)ta->offset + (int64_t)p->u.array.count * size_elem;
    return end > len;
}

/* WARNING: 'p' must be a typed array. Works even if the array buffer
   is detached */
static uint32_t typed_array_length(JSObject *p)
{
    JSTypedArray *ta = p->u.typed_array;
    int size_log2 = typed_array_size_log2(p->class_id);
    return ta->length >> size_log2;
}

static int validate_typed_array(JSContext *ctx, JSValueConst this_val)
{
    JSObject *p;
    p = get_typed_array(ctx, this_val);
    if (!p)
        return -1;
    if (typed_array_is_oob(p)) {
        JS_ThrowTypeErrorArrayBufferOOB(ctx);
        return -1;
    }
    return 0;
}

static JSValue js_typed_array_get_length(JSContext *ctx, JSValueConst this_val)
{
    JSObject *p;
    p = get_typed_array(ctx, this_val);
    if (!p)
        return JS_EXCEPTION;
    return js_int32(p->u.array.count);
}

static JSValue js_typed_array_get_buffer(JSContext *ctx, JSValueConst this_val)
{
    JSObject *p;
    JSTypedArray *ta;
    p = get_typed_array(ctx, this_val);
    if (!p)
        return JS_EXCEPTION;
    ta = p->u.typed_array;
    return js_dup(JS_MKPTR(JS_TAG_OBJECT, ta->buffer));
}

static JSValue js_typed_array_get_byteLength(JSContext *ctx, JSValueConst this_val)
{
    uint32_t size_log2;
    JSTypedArray *ta;
    JSObject *p;

    p = get_typed_array(ctx, this_val);
    if (!p)
        return JS_EXCEPTION;
    if (typed_array_is_oob(p))
        return js_int32(0);
    ta = p->u.typed_array;
    if (!ta->track_rab)
        return js_uint32(ta->length);
    size_log2 = typed_array_size_log2(p->class_id);
    return js_int64((int64_t)p->u.array.count << size_log2);
}

static JSValue js_typed_array_get_byteOffset(JSContext *ctx, JSValueConst this_val)
{
    JSObject *p;
    JSTypedArray *ta;
    p = get_typed_array(ctx, this_val);
    if (!p)
        return JS_EXCEPTION;
    if (typed_array_is_oob(p))
        return js_int32(0);
    ta = p->u.typed_array;
    return js_uint32(ta->offset);
}

JSValue JS_NewTypedArray(JSContext *ctx, int argc, JSValueConst *argv,
                         JSTypedArrayEnum type)
{
    if (type < JS_TYPED_ARRAY_UINT8C || type > JS_TYPED_ARRAY_FLOAT64)
        return JS_ThrowRangeError(ctx, "invalid typed array type");

    return js_typed_array_constructor(ctx, JS_UNDEFINED, argc, argv,
                                      JS_CLASS_UINT8C_ARRAY + type);
}

/* Return the buffer associated to the typed array or an exception if
   it is not a typed array or if the buffer is detached. pbyte_offset,
   pbyte_length or pbytes_per_element can be NULL. */
JSValue JS_GetTypedArrayBuffer(JSContext *ctx, JSValueConst obj,
                               size_t *pbyte_offset,
                               size_t *pbyte_length,
                               size_t *pbytes_per_element)
{
    JSObject *p;
    JSTypedArray *ta;
    p = get_typed_array(ctx, obj);
    if (!p)
        return JS_EXCEPTION;
    if (typed_array_is_oob(p))
        return JS_ThrowTypeErrorArrayBufferOOB(ctx);
    ta = p->u.typed_array;
    if (pbyte_offset)
        *pbyte_offset = ta->offset;
    if (pbyte_length)
        *pbyte_length = ta->length;
    if (pbytes_per_element) {
        *pbytes_per_element = 1 << typed_array_size_log2(p->class_id);
    }
    return js_dup(JS_MKPTR(JS_TAG_OBJECT, ta->buffer));
}

/* return NULL if exception. WARNING: any JS call can detach the
   buffer and render the returned pointer invalid */
uint8_t *JS_GetUint8Array(JSContext *ctx, size_t *psize, JSValueConst obj)
{
    JSObject *p;
    JSTypedArray *ta;
    JSArrayBuffer *abuf;
    p = get_typed_array(ctx, obj);
    if (!p)
        goto fail;
    if (typed_array_is_oob(p)) {
        JS_ThrowTypeErrorArrayBufferOOB(ctx);
        goto fail;
    }
    if (p->class_id != JS_CLASS_UINT8_ARRAY && p->class_id != JS_CLASS_UINT8C_ARRAY) {
        JS_ThrowTypeError(ctx, "not a Uint8Array");
        goto fail;
    }
    ta = p->u.typed_array;
    abuf = ta->buffer->u.array_buffer;

    *psize = ta->length;
    return abuf->data + ta->offset;
 fail:
    *psize = 0;
    return NULL;
}

static JSValue js_typed_array_get_toStringTag(JSContext *ctx,
                                              JSValueConst this_val)
{
    JSObject *p;
    if (JS_VALUE_GET_TAG(this_val) != JS_TAG_OBJECT)
        return JS_UNDEFINED;
    p = JS_VALUE_GET_OBJ(this_val);
    if (!is_typed_array(p->class_id))
        return JS_UNDEFINED;
    return JS_AtomToString(ctx, ctx->rt->class_array[p->class_id].class_name);
}

static JSValue js_typed_array_set_internal(JSContext *ctx,
                                           JSValueConst dst,
                                           JSValueConst src,
                                           JSValueConst off)
{
    JSObject *p;
    JSObject *src_p;
    uint32_t i;
    int64_t dst_len, src_len, offset;
    JSValue val, src_obj = JS_UNDEFINED;

    p = get_typed_array(ctx, dst);
    if (!p)
        goto fail;
    if (typed_array_is_immutable(p)) {
        JS_ThrowTypeErrorImmutableArrayBuffer(ctx);
        goto fail;
    }
    if (JS_ToInt64Sat(ctx, &offset, off))
        goto fail;
    if (offset < 0)
        goto range_error;
    if (typed_array_is_oob(p)) {
    detached:
        JS_ThrowTypeErrorArrayBufferOOB(ctx);
        goto fail;
    }
    dst_len = p->u.array.count;
    src_obj = JS_ToObject(ctx, src);
    if (JS_IsException(src_obj))
        goto fail;
    src_p = JS_VALUE_GET_OBJ(src_obj);
    if (is_typed_array(src_p->class_id)) {
        JSTypedArray *dest_ta = p->u.typed_array;
        JSArrayBuffer *dest_abuf = dest_ta->buffer->u.array_buffer;
        JSTypedArray *src_ta = src_p->u.typed_array;
        JSArrayBuffer *src_abuf = src_ta->buffer->u.array_buffer;
        int shift = typed_array_size_log2(p->class_id);

        if (typed_array_is_oob(src_p))
            goto detached;

        src_len = src_p->u.array.count;
        if (offset > dst_len - src_len)
            goto range_error;

        /* copying between typed objects */
        if (src_p->class_id == p->class_id) {
            /* same type, use memmove */
            memmove(dest_abuf->data + dest_ta->offset + (offset << shift),
                    src_abuf->data + src_ta->offset, src_len << shift);
            goto done;
        }
        if (dest_abuf->data == src_abuf->data) {
            /* copying between the same buffer using different types of mappings
               would require a temporary buffer */
        }
        /* otherwise, default behavior is slow but correct */
    } else {
        // can change |dst| as a side effect; per spec,
        // perform the range check against its old length
        if (js_get_length64(ctx, &src_len, src_obj))
            goto fail;
        if (offset > dst_len - src_len) {
        range_error:
            JS_ThrowRangeError(ctx, "invalid array length");
            goto fail;
        }
    }
    for(i = 0; i < src_len; i++) {
        val = JS_GetPropertyUint32(ctx, src_obj, i);
        if (JS_IsException(val))
            goto fail;
        // Per spec: detaching the TA mid-iteration is allowed and should
        // not throw an exception. Because iteration over the source array is
        // observable, we cannot bail out early when the TA is first detached.
        if (typed_array_is_oob(p)) {
            JS_FreeValue(ctx, val);
        } else if (JS_SetPropertyUint32(ctx, dst, offset + i, val) < 0) {
            goto fail;
        }
    }
done:
    JS_FreeValue(ctx, src_obj);
    return JS_UNDEFINED;
fail:
    JS_FreeValue(ctx, src_obj);
    return JS_EXCEPTION;
}

