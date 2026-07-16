/* Engine domain source: builtins/collections_promises_date.inc -> promise_builtin.
 * Ownership: builtins subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static JSValue js_sync_dispose_wrapper(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv,
                                       int magic, JSValueConst *func_data)
{
    JSValueConst method = func_data[0];
    JSValue ret = JS_Call(ctx, method, this_val, 0, NULL);
    if (JS_IsException(ret))
        return JS_EXCEPTION;
    JS_FreeValue(ctx, ret);
    return JS_UNDEFINED;
}

static JSValue js_get_dispose_method(JSContext *ctx, JSValueConst value, int hint)
{
    JSValue method;

    if (hint == 1) {
        /* async: try Symbol.asyncDispose first */
        method = JS_GetProperty(ctx, value, JS_ATOM_Symbol_asyncDispose);
        if (JS_IsException(method))
            return JS_EXCEPTION;
        if (!JS_IsUndefined(method) && !JS_IsNull(method)) {
            if (!JS_IsFunction(ctx, method)) {
                JS_FreeValue(ctx, method);
                return JS_ThrowTypeError(ctx, "property is not a function");
            }
            return method;
        }
        JS_FreeValue(ctx, method);
        /* Fall back to Symbol.dispose, but wrap it so its return value is
           NOT awaited (per spec GetDisposeMethod). */
        method = JS_GetProperty(ctx, value, JS_ATOM_Symbol_dispose);
        if (JS_IsException(method))
            return JS_EXCEPTION;
        if (JS_IsUndefined(method) || JS_IsNull(method))
            return JS_ThrowTypeError(ctx, "property is not a function");
        if (!JS_IsFunction(ctx, method)) {
            JS_FreeValue(ctx, method);
            return JS_ThrowTypeError(ctx, "property is not a function");
        }

        {
            JSValue data[1], wrapped;
            data[0] = method;
            wrapped = JS_NewCFunctionData(ctx, js_sync_dispose_wrapper, 0, 0,
                                          1, vc(data));
            JS_FreeValue(ctx, method);
            return wrapped;
        }
    }

    /* sync dispose */
    method = JS_GetProperty(ctx, value, JS_ATOM_Symbol_dispose);
    if (JS_IsException(method))
        return JS_EXCEPTION;
    if (JS_IsUndefined(method) || JS_IsNull(method))
        return JS_ThrowTypeError(ctx, "property is not a function");
    if (!JS_IsFunction(ctx, method)) {
        JS_FreeValue(ctx, method);
        return JS_ThrowTypeError(ctx, "property is not a function");
    }
    return method;
}

static JSValue js_disposable_stack_constructor(JSContext *ctx,
                                               JSValueConst new_target,
                                               int argc, JSValueConst *argv,
                                               int class_id)
{
    JSDisposableStack *s;
    JSValue obj;

    if (JS_IsUndefined(new_target))
        return JS_ThrowTypeError(ctx, "Constructor requires 'new'");
    obj = js_create_from_ctor(ctx, new_target, class_id);
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    s = js_mallocz(ctx, sizeof(*s));
    if (!s) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    JS_SetOpaqueInternal(obj, s);
    return obj;
}

static void js_disposable_stack_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSObject *p;
    JSDisposableStack *s;

    p = JS_VALUE_GET_OBJ(val);
    s = p->u.opaque;
    if (s) {
        js_disposable_stack_clear(rt, s);
        js_free_rt(rt, s);
    }
}

static void js_disposable_stack_mark(JSRuntime *rt, JSValueConst val,
                                     JS_MarkFunc *mark_func)
{
    JSObject *p;
    JSDisposableStack *s;
    int i;

    p = JS_VALUE_GET_OBJ(val);
    s = p->u.opaque;
    if (s) {
        for (i = 0; i < s->resource_count; i++) {
            JS_MarkValue(rt, s->resources[i].value, mark_func);
            JS_MarkValue(rt, s->resources[i].method, mark_func);
        }
    }
}

static JSDisposableStack *js_disposable_stack_get(JSContext *ctx,
                                                  JSValueConst this_val,
                                                  int class_id)
{
    JSDisposableStack *s;
    s = JS_GetOpaque2(ctx, this_val, class_id);
    if (!s)
        return NULL;
    if (s->disposed) {
        JS_ThrowReferenceError(ctx, "DisposableStack has been disposed");
        return NULL;
    }
    return s;
}

static JSValue js_disposable_stack_use(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv, int class_id)
{
    JSDisposableStack *s;
    JSValueConst value;
    JSValue method;
    int hint = (class_id == JS_CLASS_ASYNC_DISPOSABLE_STACK) ? 1 : 0;

    s = js_disposable_stack_get(ctx, this_val, class_id);
    if (!s)
        return JS_EXCEPTION;
    value = argv[0];
    if (JS_IsNull(value) || JS_IsUndefined(value)) {
        /* For async stacks, a null/undefined resource still needs a record
           so disposeAsync performs the required Await(undefined). */
        if (class_id == JS_CLASS_ASYNC_DISPOSABLE_STACK) {
            if (js_disposable_stack_add(ctx, s, JS_UNDEFINED, JS_UNDEFINED,
                                        JS_DISPOSE_HINT_SYNC) < 0)
                return JS_EXCEPTION;
        }
        return js_dup(value);
    }
    if (!JS_IsObject(value))
        return JS_ThrowTypeError(ctx, "not an object");
    method = js_get_dispose_method(ctx, value, hint);
    if (JS_IsException(method))
        return JS_EXCEPTION;
    if (js_disposable_stack_add(ctx, s, value, method, JS_DISPOSE_HINT_SYNC) < 0) {
        JS_FreeValue(ctx, method);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, method);
    return js_dup(value);
}

static JSValue js_disposable_stack_adopt(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv, int class_id)
{
    JSDisposableStack *s;
    JSValueConst value, on_dispose;

    s = js_disposable_stack_get(ctx, this_val, class_id);
    if (!s)
        return JS_EXCEPTION;
    value = argv[0];
    on_dispose = argv[1];
    if (!JS_IsFunction(ctx, on_dispose))
        return JS_ThrowTypeError(ctx, "not a function");
    if (js_disposable_stack_add(ctx, s, value, on_dispose, JS_DISPOSE_HINT_ADOPT) < 0)
        return JS_EXCEPTION;
    return js_dup(value);
}

static JSValue js_disposable_stack_defer(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv, int class_id)
{
    JSDisposableStack *s;
    JSValueConst on_dispose;

    s = js_disposable_stack_get(ctx, this_val, class_id);
    if (!s)
        return JS_EXCEPTION;
    on_dispose = argv[0];
    if (!JS_IsFunction(ctx, on_dispose))
        return JS_ThrowTypeError(ctx, "not a function");
    if (js_disposable_stack_add(ctx, s, JS_UNDEFINED, on_dispose, JS_DISPOSE_HINT_DEFER) < 0)
        return JS_EXCEPTION;
    return JS_UNDEFINED;
}

/* Simple .then handler that discards its argument and returns undefined;
   used to normalize the chain's resolved value after all disposals. */
static JSValue js_async_dispose_to_undef(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv,
                                         int magic, JSValueConst *func_data)
{
    return JS_UNDEFINED;
}

static JSValue js_async_dispose_rethrow(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv,
                                        int magic, JSValueConst *func_data)
{
    JSValue prev_err = js_dup(func_data[0]);
    if (magic == 0) {
        return JS_Throw(ctx, prev_err);
    } else {
        JSValue se = js_new_suppressed_error(ctx, argv[0], prev_err);
        JS_FreeValue(ctx, prev_err);
        if (JS_IsException(se))
            return JS_EXCEPTION;
        return JS_Throw(ctx, se);
    }
}

static JSValue js_async_dispose_step(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv,
                                     int magic, JSValueConst *func_data)
{
    JSValueConst value = func_data[0];
    JSValueConst method = func_data[1];
    int hint = JS_VALUE_GET_INT(func_data[2]);
    bool has_prev_err = (magic == 1);
    JSValue ret;

    if (JS_IsUndefined(method)) {
        /* null/undefined resource on async stack: Await(undefined) */
        if (has_prev_err)
            return JS_Throw(ctx, js_dup(argv[0]));
        return JS_UNDEFINED;
    }

    switch (hint) {
    case JS_DISPOSE_HINT_ADOPT:
        ret = JS_Call(ctx, method, JS_UNDEFINED, 1, &value);
        break;
    case JS_DISPOSE_HINT_DEFER:
        ret = JS_Call(ctx, method, JS_UNDEFINED, 0, NULL);
        break;
    default:
        ret = JS_Call(ctx, method, value, 0, NULL);
        break;
    }

    if (JS_IsException(ret)) {
        JSValue new_err = JS_GetException(ctx);
        if (has_prev_err) {
            JSValue se = js_new_suppressed_error(ctx, new_err, argv[0]);
            JS_FreeValue(ctx, new_err);
            if (JS_IsException(se))
                return JS_EXCEPTION;
            return JS_Throw(ctx, se);
        }
        return JS_Throw(ctx, new_err);
    }

    if (!has_prev_err) {
        /* Propagate method result; next .then awaits it */
        return ret;
    }

    /* Await ret, then rethrow the stored error (possibly wrapped) */
    {
        JSValueConst prev_err = argv[0];
        JSValue ret_promise, resolve_fn, reject_fn, then_args[2], result;
        ret_promise = js_promise_resolve(ctx, ctx->promise_ctor, 1, vc(&ret), 0);
        JS_FreeValue(ctx, ret);
        if (JS_IsException(ret_promise))
            return JS_EXCEPTION;
        resolve_fn = JS_NewCFunctionData(ctx, js_async_dispose_rethrow, 0, 0,
                                         1, &prev_err);
        reject_fn = JS_NewCFunctionData(ctx, js_async_dispose_rethrow, 0, 1,
                                        1, &prev_err);
        then_args[0] = resolve_fn;
        then_args[1] = reject_fn;
        result = JS_Invoke(ctx, ret_promise, JS_ATOM_then, 2, vc(then_args));
        JS_FreeValue(ctx, resolve_fn);
        JS_FreeValue(ctx, reject_fn);
        JS_FreeValue(ctx, ret_promise);
        return result;
    }
}

static JSValue js_disposable_stack_dispose(JSContext *ctx,
                                           JSValueConst this_val,
                                           int argc,
                                           JSValueConst *argv,
                                           int class_id)
{
    JSDisposableStack *s;

    s = JS_GetOpaque2(ctx, this_val, class_id);
    if (!s) {
        if (class_id == JS_CLASS_ASYNC_DISPOSABLE_STACK) {
            JSValue exc = JS_GetException(ctx);
            JSValue p = js_promise_resolve(ctx, ctx->promise_ctor, 1, vc(&exc),
                                           /*is_reject*/1);
            JS_FreeValue(ctx, exc);
            return p;
        }
        return JS_EXCEPTION;
    }
    if (s->disposed) {
        if (class_id == JS_CLASS_ASYNC_DISPOSABLE_STACK) {
            JSValue undef = JS_UNDEFINED;
            return js_promise_resolve(ctx, ctx->promise_ctor, 1, vc(&undef),
                                      /*is_reject*/0);
        }
        return JS_UNDEFINED;
    }
    if (class_id == JS_CLASS_ASYNC_DISPOSABLE_STACK) {
        /* Per spec DisposeResources: iterate resources in LIFO order and
           for each do Call + Await. The first Call happens synchronously
           inside disposeAsync(); subsequent Calls fire in microtasks via a
           Promise.then() chain so that each call sees the previous
           dispose's promise already settled. */
        int i, count = s->resource_count;
        JSValue chain, undef, ret;

        s->disposed = true;

        /* First (top-of-stack) resource: synchronous Call. */
        undef = JS_UNDEFINED;
        i = count - 1;
        if (i < 0) {
            chain = js_promise_resolve(ctx, ctx->promise_ctor, 1, vc(&undef),
                                       /*is_reject*/0);
            if (JS_IsException(chain))
                goto async_dispose_fail;
        } else {
            JSDisposableResource *res = &s->resources[i];
            if (JS_IsUndefined(res->method)) {
                /* null/undefined resource: Await(undefined) */
                chain = js_promise_resolve(ctx, ctx->promise_ctor, 1,
                                           vc(&undef), /*is_reject*/0);
            } else {
                switch (res->hint) {
                case JS_DISPOSE_HINT_ADOPT:
                    ret = JS_Call(ctx, res->method, JS_UNDEFINED, 1,
                                  vc(&res->value));
                    break;
                case JS_DISPOSE_HINT_DEFER:
                    ret = JS_Call(ctx, res->method, JS_UNDEFINED, 0, NULL);
                    break;
                default:
                    ret = JS_Call(ctx, res->method, res->value, 0, NULL);
                    break;
                }
                if (JS_IsException(ret)) {
                    JSValue err = JS_GetException(ctx);
                    chain = js_promise_resolve(ctx, ctx->promise_ctor, 1,
                                               vc(&err), /*is_reject*/1);
                    JS_FreeValue(ctx, err);
                } else {
                    chain = js_promise_resolve(ctx, ctx->promise_ctor, 1,
                                               vc(&ret), /*is_reject*/0);
                    JS_FreeValue(ctx, ret);
                }
            }
            JS_FreeValue(ctx, res->value);
            JS_FreeValue(ctx, res->method);
            res->value = JS_UNDEFINED;
            res->method = JS_UNDEFINED;
            if (JS_IsException(chain)) {
                i--;
                goto async_dispose_fail;
            }
            i--;
        }

        /* Remaining resources: chain lazy steps. */
        for (; i >= 0; i--) {
            JSDisposableResource *res = &s->resources[i];
            JSValueConst data[3];
            JSValue hint_val, resolve_fn, reject_fn, then_args[2], new_chain;

            hint_val = JS_NewInt32(ctx, res->hint);
            data[0] = res->value;
            data[1] = res->method;
            data[2] = hint_val;
            resolve_fn = JS_NewCFunctionData(ctx, js_async_dispose_step, 0, 0,
                                             3, data);
            reject_fn = JS_NewCFunctionData(ctx, js_async_dispose_step, 0, 1,
                                            3, data);
            JS_FreeValue(ctx, hint_val);
            JS_FreeValue(ctx, res->value);
            JS_FreeValue(ctx, res->method);
            res->value = JS_UNDEFINED;
            res->method = JS_UNDEFINED;
            if (JS_IsException(resolve_fn) || JS_IsException(reject_fn)) {
                JS_FreeValue(ctx, resolve_fn);
                JS_FreeValue(ctx, reject_fn);
                JS_FreeValue(ctx, chain);
                chain = JS_EXCEPTION;
                goto async_dispose_fail;
            }
            then_args[0] = resolve_fn;
            then_args[1] = reject_fn;
            new_chain = JS_Invoke(ctx, chain, JS_ATOM_then, 2, vc(then_args));
            JS_FreeValue(ctx, resolve_fn);
            JS_FreeValue(ctx, reject_fn);
            JS_FreeValue(ctx, chain);
            if (JS_IsException(new_chain)) {
                chain = JS_EXCEPTION;
                goto async_dispose_fail;
            }
            chain = new_chain;
        }
        s->resource_count = 0;

        if (count > 0) {
            JSValue undef_fn, then_args[1], new_chain;
            undef_fn = JS_NewCFunctionData(ctx, js_async_dispose_to_undef,
                                           0, 0, 0, NULL);
            if (JS_IsException(undef_fn)) {
                JS_FreeValue(ctx, chain);
                return JS_EXCEPTION;
            }
            then_args[0] = undef_fn;
            new_chain = JS_Invoke(ctx, chain, JS_ATOM_then, 1, vc(then_args));
            JS_FreeValue(ctx, undef_fn);
            JS_FreeValue(ctx, chain);
            return new_chain;
        }
        return chain;

    async_dispose_fail:
        for (; i >= 0; i--) {
            JSDisposableResource *res = &s->resources[i];
            JS_FreeValue(ctx, res->value);
            JS_FreeValue(ctx, res->method);
            res->value = JS_UNDEFINED;
            res->method = JS_UNDEFINED;
        }
        s->resource_count = 0;
        return JS_EXCEPTION;
    }
    if (js_dispose_resources(ctx, s, JS_UNDEFINED) < 0)
        return JS_EXCEPTION;
    return JS_UNDEFINED;
}

static JSValue js_disposable_stack_move(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv, int class_id)
{
    JSDisposableStack *s, *ns;
    JSValue new_obj;

    s = js_disposable_stack_get(ctx, this_val, class_id);
    if (!s)
        return JS_EXCEPTION;
    /* Use the intrinsic prototype directly so tampering with the global
       binding or subclassing cannot redirect move(). */
    new_obj = JS_NewObjectProtoClass(ctx, ctx->class_proto[class_id],
                                     class_id);
    if (JS_IsException(new_obj))
        return JS_EXCEPTION;
    ns = js_mallocz(ctx, sizeof(*ns));
    if (!ns) {
        JS_FreeValue(ctx, new_obj);
        return JS_EXCEPTION;
    }
    JS_SetOpaqueInternal(new_obj, ns);
    /* Transfer resources to new stack */
    ns->resources = s->resources;
    ns->resource_count = s->resource_count;
    ns->resource_capacity = s->resource_capacity;
    /* Reset original stack */
    s->resources = NULL;
    s->resource_count = 0;
    s->resource_capacity = 0;
    s->disposed = true;
    return new_obj;
}

static JSValue js_disposable_stack_get_disposed(JSContext *ctx,
                                                JSValueConst this_val, int class_id)
{
    JSDisposableStack *s;

    s = JS_GetOpaque2(ctx, this_val, class_id);
    if (!s)
        return JS_EXCEPTION;
    return js_bool(s->disposed);
}

static const JSCFunctionListEntry js_disposable_stack_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("adopt", 2, js_disposable_stack_adopt, JS_CLASS_DISPOSABLE_STACK ),
    JS_CFUNC_MAGIC_DEF("defer", 1, js_disposable_stack_defer, JS_CLASS_DISPOSABLE_STACK ),
    JS_CFUNC_MAGIC_DEF("dispose", 0, js_disposable_stack_dispose, JS_CLASS_DISPOSABLE_STACK ),
    JS_CFUNC_MAGIC_DEF("move", 0, js_disposable_stack_move, JS_CLASS_DISPOSABLE_STACK ),
    JS_CFUNC_MAGIC_DEF("use", 1, js_disposable_stack_use, JS_CLASS_DISPOSABLE_STACK ),
    JS_CGETSET_MAGIC_DEF("disposed", js_disposable_stack_get_disposed, NULL, JS_CLASS_DISPOSABLE_STACK ),
    JS_ALIAS_DEF("[Symbol.dispose]", "dispose" ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "DisposableStack", JS_PROP_CONFIGURABLE ),
};

static const JSCFunctionListEntry js_async_disposable_stack_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("adopt", 2, js_disposable_stack_adopt, JS_CLASS_ASYNC_DISPOSABLE_STACK ),
    JS_CFUNC_MAGIC_DEF("defer", 1, js_disposable_stack_defer, JS_CLASS_ASYNC_DISPOSABLE_STACK ),
    JS_CFUNC_MAGIC_DEF("disposeAsync", 0, js_disposable_stack_dispose, JS_CLASS_ASYNC_DISPOSABLE_STACK ),
    JS_CFUNC_MAGIC_DEF("move", 0, js_disposable_stack_move, JS_CLASS_ASYNC_DISPOSABLE_STACK ),
    JS_CFUNC_MAGIC_DEF("use", 1, js_disposable_stack_use, JS_CLASS_ASYNC_DISPOSABLE_STACK ),
    JS_CGETSET_MAGIC_DEF("disposed", js_disposable_stack_get_disposed, NULL, JS_CLASS_ASYNC_DISPOSABLE_STACK ),
    JS_ALIAS_DEF("[Symbol.asyncDispose]", "disposeAsync" ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AsyncDisposableStack", JS_PROP_CONFIGURABLE),
};

/* Promise */

typedef struct JSPromiseData {
    JSPromiseStateEnum promise_state;
    /* 0=fulfill, 1=reject, list of JSPromiseReactionData.link */
    struct list_head promise_reactions[2];
    bool is_handled; /* Note: only useful to debug */
    JSValue promise_result;
} JSPromiseData;

typedef struct JSPromiseFunctionDataResolved {
    int ref_count;
    bool already_resolved;
} JSPromiseFunctionDataResolved;

typedef struct JSPromiseFunctionData {
    JSValue promise;
    JSPromiseFunctionDataResolved *presolved;
} JSPromiseFunctionData;

typedef struct JSPromiseReactionData {
    struct list_head link; /* not used in promise_reaction_job */
    JSValue resolving_funcs[2];
    JSValue handler;
} JSPromiseReactionData;

JSPromiseStateEnum JS_PromiseState(JSContext *ctx, JSValueConst promise)
{
    JSPromiseData *s = JS_GetOpaque(promise, JS_CLASS_PROMISE);
    if (!s)
        return JS_PROMISE_NOT_A_PROMISE;
    return s->promise_state;
}

JSValue JS_PromiseResult(JSContext *ctx, JSValueConst promise)
{
    JSPromiseData *s = JS_GetOpaque(promise, JS_CLASS_PROMISE);
    if (!s)
        return JS_UNDEFINED;
    return js_dup(s->promise_result);
}

bool JS_IsPromise(JSValueConst val)
{
    if (JS_VALUE_GET_TAG(val) != JS_TAG_OBJECT)
        return false;
    return JS_VALUE_GET_OBJ(val)->class_id == JS_CLASS_PROMISE;
}

JSValue JS_NewSettledPromise(JSContext *ctx, bool is_reject, JSValueConst value)
{
    return js_promise_resolve(ctx, ctx->promise_ctor, 1, &value, is_reject);
}

static int js_create_resolving_functions(JSContext *ctx, JSValue *args,
                                         JSValueConst promise);

static void promise_reaction_data_free(JSRuntime *rt,
                                       JSPromiseReactionData *rd)
{
    JS_FreeValueRT(rt, rd->resolving_funcs[0]);
    JS_FreeValueRT(rt, rd->resolving_funcs[1]);
    JS_FreeValueRT(rt, rd->handler);
    js_free_rt(rt, rd);
}

#ifdef ENABLE_DUMPS // JS_DUMP_PROMISE
#define promise_trace(ctx, ...) \
   do { \
     if (check_dump_flag(ctx->rt, JS_DUMP_PROMISE)) \
       printf(__VA_ARGS__); \
   } while (0)
#else
#define promise_trace(...)
#endif

static JSValue promise_reaction_job(JSContext *ctx, int argc,
                                    JSValueConst *argv)
{
    JSValueConst handler, func;
    JSValue res, res2;
    JSValueConst arg;
    bool is_reject;

    assert(argc == 5);
    handler = argv[2];
    is_reject = JS_ToBool(ctx, argv[3]);
    arg = argv[4];

    promise_trace(ctx, "promise_reaction_job: is_reject=%d\n", is_reject);

    if (JS_IsUndefined(handler)) {
        if (is_reject) {
            res = JS_Throw(ctx, js_dup(arg));
        } else {
            res = js_dup(arg);
        }
    } else {
        res = JS_Call(ctx, handler, JS_UNDEFINED, 1, &arg);
    }
    is_reject = JS_IsException(res);
    if (is_reject) {
        if (unlikely(JS_IsUncatchableError(ctx->rt->current_exception)))
            return JS_EXCEPTION;
        res = JS_GetException(ctx);
    }
    func = argv[is_reject];
    /* as an extension, we support undefined as value to avoid
       creating a dummy promise in the 'await' implementation of async
       functions */
    if (!JS_IsUndefined(func)) {
        res2 = JS_Call(ctx, func, JS_UNDEFINED, 1, vc(&res));
    } else {
        res2 = JS_UNDEFINED;
    }
    JS_FreeValue(ctx, res);

    return res2;
}

void turbojs_internal_set_promise_hook(JSRuntime *rt, JSPromiseHook promise_hook, void *opaque)
{
    rt->promise_hook = promise_hook;
    rt->promise_hook_opaque = opaque;
}

void turbojs_internal_set_host_promise_rejection_tracker(JSRuntime *rt,
                                                        JSHostPromiseRejectionTracker *cb,
                                                        void *opaque)
{
    rt->host_promise_rejection_tracker = cb;
    rt->host_promise_rejection_tracker_opaque = opaque;
}

static void fulfill_or_reject_promise(JSContext *ctx, JSValueConst promise,
                                      JSValueConst value, bool is_reject)
{
    JSPromiseData *s = JS_GetOpaque(promise, JS_CLASS_PROMISE);
    struct list_head *el, *el1;
    JSPromiseReactionData *rd;
    JSValueConst args[5];

    if (!s || s->promise_state != JS_PROMISE_PENDING)
        return; /* should never happen */
    set_value(ctx, &s->promise_result, js_dup(value));
    s->promise_state = JS_PROMISE_FULFILLED + is_reject;

    promise_trace(ctx, "fulfill_or_reject_promise: is_reject=%d\n", is_reject);

    if (s->promise_state == JS_PROMISE_FULFILLED) {
        JSRuntime *rt = ctx->rt;
        if (rt->promise_hook) {
            rt->promise_hook(ctx, JS_PROMISE_HOOK_RESOLVE, promise,
                             JS_UNDEFINED, rt->promise_hook_opaque);
        }
    }

    if (s->promise_state == JS_PROMISE_REJECTED && !s->is_handled) {
        JSRuntime *rt = ctx->rt;
        if (rt->host_promise_rejection_tracker)
            rt->host_promise_rejection_tracker(ctx, promise, value, false,
                                               rt->host_promise_rejection_tracker_opaque);
    }

    list_for_each_safe(el, el1, &s->promise_reactions[is_reject]) {
        rd = list_entry(el, JSPromiseReactionData, link);
        args[0] = rd->resolving_funcs[0];
        args[1] = rd->resolving_funcs[1];
        args[2] = rd->handler;
        args[3] = js_bool(is_reject);
        args[4] = value;
        JS_EnqueueJob(ctx, promise_reaction_job, 5, args);
        list_del(&rd->link);
        promise_reaction_data_free(ctx->rt, rd);
    }

    list_for_each_safe(el, el1, &s->promise_reactions[1 - is_reject]) {
        rd = list_entry(el, JSPromiseReactionData, link);
        list_del(&rd->link);
        promise_reaction_data_free(ctx->rt, rd);
    }
}

static JSValue js_promise_resolve_thenable_job(JSContext *ctx,
                                               int argc, JSValueConst *argv)
{
    JSValueConst promise, thenable, then;
    JSValue args[2], res;
    JSRuntime *rt;

    promise_trace(ctx, "js_promise_resolve_thenable_job\n");

    assert(argc == 3);
    promise = argv[0];
    thenable = argv[1];
    then = argv[2];
    if (js_create_resolving_functions(ctx, args, promise) < 0)
        return JS_EXCEPTION;
    rt = ctx->rt;
    if (rt->promise_hook) {
        rt->promise_hook(ctx, JS_PROMISE_HOOK_BEFORE, promise, JS_UNDEFINED,
                         rt->promise_hook_opaque);
    }
    res = JS_Call(ctx, then, thenable, 2, vc(args));
    if (rt->promise_hook) {
        rt->promise_hook(ctx, JS_PROMISE_HOOK_AFTER, promise, JS_UNDEFINED,
                         rt->promise_hook_opaque);
    }
    if (JS_IsException(res)) {
        JSValue error = JS_GetException(ctx);
        res = JS_Call(ctx, args[1], JS_UNDEFINED, 1, vc(&error));
        JS_FreeValue(ctx, error);
    }
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    return res;
}

static void js_promise_resolve_function_free_resolved(JSRuntime *rt,
                                                      JSPromiseFunctionDataResolved *sr)
{
    if (--sr->ref_count == 0) {
        js_free_rt(rt, sr);
    }
}

static int js_create_resolving_functions(JSContext *ctx,
                                         JSValue *resolving_funcs,
                                         JSValueConst promise)

{
    JSValue obj;
    JSPromiseFunctionData *s;
    JSPromiseFunctionDataResolved *sr;
    int i, ret;

    sr = js_malloc(ctx, sizeof(*sr));
    if (!sr)
        return -1;
    sr->ref_count = 1;
    sr->already_resolved = false; /* must be shared between the two functions */
    ret = 0;
    for(i = 0; i < 2; i++) {
        obj = JS_NewObjectProtoClass(ctx, ctx->function_proto,
                                     JS_CLASS_PROMISE_RESOLVE_FUNCTION + i);
        if (JS_IsException(obj))
            goto fail;
        s = js_malloc(ctx, sizeof(*s));
        if (!s) {
            JS_FreeValue(ctx, obj);
        fail:

            if (i != 0)
                JS_FreeValue(ctx, resolving_funcs[0]);
            ret = -1;
            break;
        }
        sr->ref_count++;
        s->presolved = sr;
        s->promise = js_dup(promise);
        JS_SetOpaqueInternal(obj, s);
        js_function_set_properties(ctx, obj, JS_ATOM_empty_string, 1);
        resolving_funcs[i] = obj;
    }
    js_promise_resolve_function_free_resolved(ctx->rt, sr);
    return ret;
}

static void js_promise_resolve_function_finalizer(JSRuntime *rt,
                                                  JSValueConst val)
{
    JSPromiseFunctionData *s = JS_VALUE_GET_OBJ(val)->u.promise_function_data;
    if (s) {
        js_promise_resolve_function_free_resolved(rt, s->presolved);
        JS_FreeValueRT(rt, s->promise);
        js_free_rt(rt, s);
    }
}

static void js_promise_resolve_function_mark(JSRuntime *rt, JSValueConst val,
                                             JS_MarkFunc *mark_func)
{
    JSPromiseFunctionData *s = JS_VALUE_GET_OBJ(val)->u.promise_function_data;
    if (s) {
        JS_MarkValue(rt, s->promise, mark_func);
    }
}

static JSValue js_promise_resolve_function_call(JSContext *ctx,
                                                JSValueConst func_obj,
                                                JSValueConst this_val,
                                                int argc, JSValueConst *argv,
                                                int flags)
{
    JSObject *p = JS_VALUE_GET_OBJ(func_obj);
    JSPromiseFunctionData *s;
    JSValueConst args[3];
    JSValueConst resolution;
    JSValue then;
    bool is_reject;

    s = p->u.promise_function_data;
    if (!s || s->presolved->already_resolved)
        return JS_UNDEFINED;
    s->presolved->already_resolved = true;
    is_reject = p->class_id - JS_CLASS_PROMISE_RESOLVE_FUNCTION;
    if (argc > 0)
        resolution = argv[0];
    else
        resolution = JS_UNDEFINED;
#ifdef ENABLE_DUMPS // JS_DUMP_PROMISE
    if (check_dump_flag(ctx->rt, JS_DUMP_PROMISE)) {
        printf("js_promise_resolving_function_call: is_reject=%d resolution=", is_reject);
        JS_DumpValue(ctx->rt, resolution);
        printf("\n");
    }
#endif
    if (is_reject || !JS_IsObject(resolution)) {
        goto done;
    } else if (js_same_value(ctx, resolution, s->promise)) {
        JS_ThrowTypeError(ctx, "promise self resolution");
        goto fail_reject;
    }
    then = JS_GetProperty(ctx, resolution, JS_ATOM_then);
    if (JS_IsException(then)) {
        JSValue error;
    fail_reject:
        error = JS_GetException(ctx);
        fulfill_or_reject_promise(ctx, s->promise, error, true);
        JS_FreeValue(ctx, error);
    } else if (!JS_IsFunction(ctx, then)) {
        JS_FreeValue(ctx, then);
    done:
        fulfill_or_reject_promise(ctx, s->promise, resolution, is_reject);
    } else {
        args[0] = s->promise;
        args[1] = resolution;
        args[2] = then;
        JS_EnqueueJob(ctx, js_promise_resolve_thenable_job, 3, args);
        JS_FreeValue(ctx, then);
    }
    return JS_UNDEFINED;
}

static void js_promise_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSPromiseData *s = JS_GetOpaque(val, JS_CLASS_PROMISE);
    struct list_head *el, *el1;
    int i;

    if (!s)
        return;
    for(i = 0; i < 2; i++) {
        list_for_each_safe(el, el1, &s->promise_reactions[i]) {
            JSPromiseReactionData *rd =
                list_entry(el, JSPromiseReactionData, link);
            promise_reaction_data_free(rt, rd);
        }
    }
    JS_FreeValueRT(rt, s->promise_result);
    js_free_rt(rt, s);
}

static void js_promise_mark(JSRuntime *rt, JSValueConst val,
                            JS_MarkFunc *mark_func)
{
    JSPromiseData *s = JS_GetOpaque(val, JS_CLASS_PROMISE);
    struct list_head *el;
    int i;

    if (!s)
        return;
    for(i = 0; i < 2; i++) {
        list_for_each(el, &s->promise_reactions[i]) {
            JSPromiseReactionData *rd =
                list_entry(el, JSPromiseReactionData, link);
            JS_MarkValue(rt, rd->resolving_funcs[0], mark_func);
            JS_MarkValue(rt, rd->resolving_funcs[1], mark_func);
            JS_MarkValue(rt, rd->handler, mark_func);
        }
    }
    JS_MarkValue(rt, s->promise_result, mark_func);
}

/* Create a new promise object with resolving functions. Returns the promise
   and sets resolving_funcs[0] (resolve) and resolving_funcs[1] (reject). */
static JSValue js_promise_new(JSContext *ctx, JSValueConst new_target,
                               JSValue *resolving_funcs)
{
    JSValue obj;
    JSPromiseData *s;
    JSRuntime *rt;

    obj = js_create_from_ctor(ctx, new_target, JS_CLASS_PROMISE);
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    s = js_mallocz(ctx, sizeof(*s));
    if (!s) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    s->promise_state = JS_PROMISE_PENDING;
    s->is_handled = false;
    init_list_head(&s->promise_reactions[0]);
    init_list_head(&s->promise_reactions[1]);
    s->promise_result = JS_UNDEFINED;
    JS_SetOpaqueInternal(obj, s);
    if (js_create_resolving_functions(ctx, resolving_funcs, obj)) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    rt = ctx->rt;
    if (rt->promise_hook) {
        JSValueConst parent_promise = JS_UNDEFINED;
        if (rt->parent_promise)
            parent_promise = rt->parent_promise->value;
        rt->promise_hook(ctx, JS_PROMISE_HOOK_INIT, obj, parent_promise,
                         rt->promise_hook_opaque);
    }
    return obj;
}

static JSValue js_promise_constructor(JSContext *ctx, JSValueConst new_target,
                                      int argc, JSValueConst *argv)
{
    JSValueConst executor;
    JSValue obj;
    JSValue args[2], ret;

    executor = argv[0];
    if (check_function(ctx, executor))
        return JS_EXCEPTION;
    obj = js_promise_new(ctx, new_target, args);
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    ret = JS_Call(ctx, executor, JS_UNDEFINED, 2, vc(args));
    if (JS_IsException(ret)) {
        JSValue ret2, error;
        error = JS_GetException(ctx);
        ret2 = JS_Call(ctx, args[1], JS_UNDEFINED, 1, vc(&error));
        JS_FreeValue(ctx, error);
        if (JS_IsException(ret2))
            goto fail;
        JS_FreeValue(ctx, ret2);
    }
    JS_FreeValue(ctx, ret);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    return obj;
 fail:
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue js_promise_executor(JSContext *ctx,
                                   JSValueConst this_val,
                                   int argc, JSValueConst *argv,
                                   int magic, JSValueConst *func_data)
{
    int i;

    for(i = 0; i < 2; i++) {
        if (!JS_IsUndefined(func_data[i]))
            return JS_ThrowTypeError(ctx, "resolving function already set");
        func_data[i] = js_dup(argv[i]);
    }
    return JS_UNDEFINED;
}

static JSValue js_promise_executor_new(JSContext *ctx)
{
    JSValueConst func_data[2];

    func_data[0] = JS_UNDEFINED;
    func_data[1] = JS_UNDEFINED;
    return JS_NewCFunctionData(ctx, js_promise_executor, 2,
                               0, 2, func_data);
}

static JSValue js_new_promise_capability(JSContext *ctx,
                                         JSValue *resolving_funcs,
                                         JSValueConst ctor)
{
    JSValue executor, result_promise;
    JSCFunctionDataRecord *s;
    int i;

    if (JS_IsUndefined(ctor) || js_same_value(ctx, ctor, ctx->promise_ctor))
        return js_promise_new(ctx, JS_UNDEFINED, resolving_funcs);
    executor = js_promise_executor_new(ctx);
    if (JS_IsException(executor))
        return JS_EXCEPTION;
    result_promise = JS_CallConstructor(ctx, ctor, 1, vc(&executor));
    if (JS_IsException(result_promise))
        goto fail;
    s = JS_GetOpaque(executor, JS_CLASS_C_FUNCTION_DATA);
    for(i = 0; i < 2; i++) {
        if (check_function(ctx, s->data[i]))
            goto fail;
    }
    for(i = 0; i < 2; i++)
        resolving_funcs[i] = js_dup(s->data[i]);
    JS_FreeValue(ctx, executor);
    return result_promise;
 fail:
    JS_FreeValue(ctx, executor);
    JS_FreeValue(ctx, result_promise);
    return JS_EXCEPTION;
}

JSValue JS_NewPromiseCapability(JSContext *ctx, JSValue *resolving_funcs)
{
    return js_new_promise_capability(ctx, resolving_funcs, JS_UNDEFINED);
}

static JSValue js_promise_resolve(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv, int magic)
{
    JSValue result_promise, resolving_funcs[2], ret;
    bool is_reject = magic;

    if (!JS_IsObject(this_val))
        return JS_ThrowTypeErrorNotAnObject(ctx);
    if (!is_reject && JS_GetOpaque(argv[0], JS_CLASS_PROMISE)) {
        JSValue ctor;
        bool is_same;
        ctor = JS_GetProperty(ctx, argv[0], JS_ATOM_constructor);
        if (JS_IsException(ctor))
            return ctor;
        is_same = js_same_value(ctx, ctor, this_val);
        JS_FreeValue(ctx, ctor);
        if (is_same)
            return js_dup(argv[0]);
    }
    result_promise = js_new_promise_capability(ctx, resolving_funcs, this_val);
    if (JS_IsException(result_promise))
        return result_promise;
    ret = JS_Call(ctx, resolving_funcs[is_reject], JS_UNDEFINED, 1, argv);
    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);
    if (JS_IsException(ret)) {
        JS_FreeValue(ctx, result_promise);
        return ret;
    }
    JS_FreeValue(ctx, ret);
    return result_promise;
}

static JSValue js_promise_withResolvers(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv)
{
    JSValue result_promise, resolving_funcs[2], obj;
    if (!JS_IsObject(this_val))
        return JS_ThrowTypeErrorNotAnObject(ctx);
    result_promise = js_new_promise_capability(ctx, resolving_funcs, this_val);
    if (JS_IsException(result_promise))
        return JS_EXCEPTION;
    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        goto exception;
    if (JS_DefinePropertyValue(ctx, obj, JS_ATOM_promise, result_promise,
                               JS_PROP_C_W_E) < 0) {
        goto exception;
    }
    result_promise = JS_UNDEFINED;
    if (JS_DefinePropertyValue(ctx, obj, JS_ATOM_resolve, resolving_funcs[0],
                               JS_PROP_C_W_E) < 0) {
        goto exception;
    }
    resolving_funcs[0] = JS_UNDEFINED;
    if (JS_DefinePropertyValue(ctx, obj, JS_ATOM_reject, resolving_funcs[1],
                               JS_PROP_C_W_E) < 0) {
        goto exception;
    }
    return obj;
exception:
    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);
    JS_FreeValue(ctx, result_promise);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue js_promise_try(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    JSValue result_promise, resolving_funcs[2], ret, ret2;
    bool is_reject = 0;

    if (!JS_IsObject(this_val))
        return JS_ThrowTypeErrorNotAnObject(ctx);
    result_promise = js_new_promise_capability(ctx, resolving_funcs, this_val);
    if (JS_IsException(result_promise))
        return result_promise;
    ret = JS_Call(ctx, argv[0], JS_UNDEFINED, argc - 1, argv + 1);
    if (JS_IsException(ret)) {
        is_reject = 1;
        ret = JS_GetException(ctx);
    }
    ret2 = JS_Call(ctx, resolving_funcs[is_reject], JS_UNDEFINED, 1, vc(&ret));
    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);
    JS_FreeValue(ctx, ret);
    if (JS_IsException(ret2)) {
        JS_FreeValue(ctx, result_promise);
        return ret2;
    }
    JS_FreeValue(ctx, ret2);
    return result_promise;
}

static __exception int remainingElementsCount_add(JSContext *ctx,
                                                  JSValueConst resolve_element_env,
                                                  int addend)
{
    JSValue val;
    int remainingElementsCount;

    val = JS_GetPropertyUint32(ctx, resolve_element_env, 0);
    if (JS_IsException(val))
        return -1;
    if (JS_ToInt32Free(ctx, &remainingElementsCount, val))
        return -1;
    remainingElementsCount += addend;
    if (JS_SetPropertyUint32(ctx, resolve_element_env, 0,
                             js_int32(remainingElementsCount)) < 0)
        return -1;
    return (remainingElementsCount == 0);
}

#define PROMISE_MAGIC_all        0
#define PROMISE_MAGIC_allSettled 1
#define PROMISE_MAGIC_any        2

static JSValue js_promise_all_resolve_element(JSContext *ctx,
                                              JSValueConst this_val,
                                              int argc, JSValueConst *argv,
                                              int magic,
                                              JSValueConst *func_data)
{
    int resolve_type = magic & 3;
    int is_reject = magic & 4;
    bool alreadyCalled = JS_ToBool(ctx, func_data[0]);
    JSValueConst values = func_data[2];
    JSValueConst resolve = func_data[3];
    JSValueConst resolve_element_env = func_data[4];
    JSValue ret, obj;
    int is_zero, index;

    if (JS_ToInt32(ctx, &index, func_data[1]))
        return JS_EXCEPTION;
    if (alreadyCalled)
        return JS_UNDEFINED;
    func_data[0] = JS_TRUE;

    if (resolve_type == PROMISE_MAGIC_allSettled) {
        JSValue str;

        obj = JS_NewObject(ctx);
        if (JS_IsException(obj))
            return JS_EXCEPTION;
        str = js_new_string8(ctx, is_reject ? "rejected" : "fulfilled");
        if (JS_IsException(str))
            goto fail1;
        if (JS_DefinePropertyValue(ctx, obj, JS_ATOM_status,
                                   str,
                                   JS_PROP_C_W_E) < 0)
            goto fail1;
        if (JS_DefinePropertyValue(ctx, obj,
                                   is_reject ? JS_ATOM_reason : JS_ATOM_value,
                                   js_dup(argv[0]),
                                   JS_PROP_C_W_E) < 0) {
        fail1:
            JS_FreeValue(ctx, obj);
            return JS_EXCEPTION;
        }
    } else {
        obj = js_dup(argv[0]);
    }
    if (JS_DefinePropertyValueUint32(ctx, values, index,
                                     obj, JS_PROP_C_W_E) < 0)
        return JS_EXCEPTION;

    is_zero = remainingElementsCount_add(ctx, resolve_element_env, -1);
    if (is_zero < 0)
        return JS_EXCEPTION;
    if (is_zero) {
        if (resolve_type == PROMISE_MAGIC_any) {
            JSValue error;
            error = js_aggregate_error_constructor(ctx, values);
            if (JS_IsException(error))
                return JS_EXCEPTION;
            ret = JS_Call(ctx, resolve, JS_UNDEFINED, 1, vc(&error));
            JS_FreeValue(ctx, error);
        } else {
            ret = JS_Call(ctx, resolve, JS_UNDEFINED, 1, &values);
        }
        if (JS_IsException(ret))
            return ret;
        JS_FreeValue(ctx, ret);
    }
    return JS_UNDEFINED;
}

/* magic = 0: Promise.all 1: Promise.allSettled */
static JSValue js_promise_all(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv, int magic)
{
    JSValue result_promise, resolving_funcs[2], item, next_promise, ret;
    JSValue next_method = JS_UNDEFINED, values = JS_UNDEFINED;
    JSValue resolve_element_env = JS_UNDEFINED, resolve_element, reject_element;
    JSValue promise_resolve = JS_UNDEFINED, iter = JS_UNDEFINED;
    JSValueConst then_args[2], resolve_element_data[5];
    int done, index, is_zero, is_promise_any = (magic == PROMISE_MAGIC_any);

    if (!JS_IsObject(this_val))
        return JS_ThrowTypeErrorNotAnObject(ctx);
    result_promise = js_new_promise_capability(ctx, resolving_funcs, this_val);
    if (JS_IsException(result_promise))
        return result_promise;
    promise_resolve = JS_GetProperty(ctx, this_val, JS_ATOM_resolve);
    if (JS_IsException(promise_resolve) ||
        check_function(ctx, promise_resolve))
        goto fail_reject;
    iter = JS_GetIterator(ctx, argv[0], false);
    if (JS_IsException(iter)) {
        JSValue error;
    fail_reject:
        error = JS_GetException(ctx);
        ret = JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, vc(&error));
        JS_FreeValue(ctx, error);
        if (JS_IsException(ret))
            goto fail;
        JS_FreeValue(ctx, ret);
    } else {
        next_method = JS_GetProperty(ctx, iter, JS_ATOM_next);
        if (JS_IsException(next_method))
            goto fail_reject;
        values = JS_NewArray(ctx);
        if (JS_IsException(values))
            goto fail_reject;
        resolve_element_env = JS_NewArray(ctx);
        if (JS_IsException(resolve_element_env))
            goto fail_reject;
        /* remainingElementsCount field */
        if (JS_DefinePropertyValueUint32(ctx, resolve_element_env, 0,
                                         js_int32(1),
                                         JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE | JS_PROP_WRITABLE) < 0)
            goto fail_reject;

        index = 0;
        for(;;) {
            /* XXX: conformance: should close the iterator if error on 'done'
               access, but not on 'value' access */
            item = JS_IteratorNext(ctx, iter, next_method, 0, NULL, &done);
            if (JS_IsException(item))
                goto fail_reject;
            if (done)
                break;
            next_promise = JS_Call(ctx, promise_resolve,
                                   this_val, 1, vc(&item));
            JS_FreeValue(ctx, item);
            if (JS_IsException(next_promise)) {
            fail_reject1:
                JS_IteratorClose(ctx, iter, true);
                goto fail_reject;
            }
            resolve_element_data[0] = JS_FALSE;
            resolve_element_data[1] = js_int32(index);
            resolve_element_data[2] = values;
            resolve_element_data[3] = resolving_funcs[is_promise_any];
            resolve_element_data[4] = resolve_element_env;
            resolve_element =
                JS_NewCFunctionData(ctx, js_promise_all_resolve_element, 1,
                                    magic, 5, resolve_element_data);
            if (JS_IsException(resolve_element)) {
                JS_FreeValue(ctx, next_promise);
                goto fail_reject1;
            }

            if (magic == PROMISE_MAGIC_allSettled) {
                reject_element =
                    JS_NewCFunctionData(ctx, js_promise_all_resolve_element, 1,
                                        magic | 4, 5, resolve_element_data);
                if (JS_IsException(reject_element)) {
                    JS_FreeValue(ctx, next_promise);
                    goto fail_reject1;
                }
            } else if (magic == PROMISE_MAGIC_any) {
                if (JS_DefinePropertyValueUint32(ctx, values, index,
                                                 JS_UNDEFINED, JS_PROP_C_W_E) < 0)
                    goto fail_reject1;
                reject_element = resolve_element;
                resolve_element = js_dup(resolving_funcs[0]);
            } else {
                reject_element = js_dup(resolving_funcs[1]);
            }

            if (remainingElementsCount_add(ctx, resolve_element_env, 1) < 0) {
                JS_FreeValue(ctx, next_promise);
                JS_FreeValue(ctx, resolve_element);
                JS_FreeValue(ctx, reject_element);
                goto fail_reject1;
            }

            then_args[0] = resolve_element;
            then_args[1] = reject_element;
            ret = JS_InvokeFree(ctx, next_promise, JS_ATOM_then, 2, then_args);
            JS_FreeValue(ctx, resolve_element);
            JS_FreeValue(ctx, reject_element);
            if (check_exception_free(ctx, ret))
                goto fail_reject1;
            index++;
        }

        is_zero = remainingElementsCount_add(ctx, resolve_element_env, -1);
        if (is_zero < 0)
            goto fail_reject;
        if (is_zero) {
            if (magic == PROMISE_MAGIC_any) {
                JSValue error;
                error = js_aggregate_error_constructor(ctx, values);
                if (JS_IsException(error))
                    goto fail_reject;
                JS_FreeValue(ctx, values);
                values = error;
            }
            ret = JS_Call(ctx, resolving_funcs[is_promise_any], JS_UNDEFINED,
                          1, vc(&values));
            if (check_exception_free(ctx, ret))
                goto fail_reject;
        }
    }
 done:
    JS_FreeValue(ctx, promise_resolve);
    JS_FreeValue(ctx, resolve_element_env);
    JS_FreeValue(ctx, values);
    JS_FreeValue(ctx, next_method);
    JS_FreeValue(ctx, iter);
    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);
    return result_promise;
 fail:
    JS_FreeValue(ctx, result_promise);
    result_promise = JS_EXCEPTION;
    goto done;
}

static JSValue js_promise_race(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    JSValue result_promise, resolving_funcs[2], item, next_promise, ret;
    JSValue next_method = JS_UNDEFINED, iter = JS_UNDEFINED;
    JSValue promise_resolve = JS_UNDEFINED;
    int done;

    if (!JS_IsObject(this_val))
        return JS_ThrowTypeErrorNotAnObject(ctx);
    result_promise = js_new_promise_capability(ctx, resolving_funcs, this_val);
    if (JS_IsException(result_promise))
        return result_promise;
    promise_resolve = JS_GetProperty(ctx, this_val, JS_ATOM_resolve);
    if (JS_IsException(promise_resolve) ||
        check_function(ctx, promise_resolve))
        goto fail_reject;
    iter = JS_GetIterator(ctx, argv[0], false);
    if (JS_IsException(iter)) {
        JSValue error;
    fail_reject:
        error = JS_GetException(ctx);
        ret = JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, vc(&error));
        JS_FreeValue(ctx, error);
        if (JS_IsException(ret))
            goto fail;
        JS_FreeValue(ctx, ret);
    } else {
        next_method = JS_GetProperty(ctx, iter, JS_ATOM_next);
        if (JS_IsException(next_method))
            goto fail_reject;

        for(;;) {
            /* XXX: conformance: should close the iterator if error on 'done'
               access, but not on 'value' access */
            item = JS_IteratorNext(ctx, iter, next_method, 0, NULL, &done);
            if (JS_IsException(item))
                goto fail_reject;
            if (done)
                break;
            next_promise = JS_Call(ctx, promise_resolve,
                                   this_val, 1, vc(&item));
            JS_FreeValue(ctx, item);
            if (JS_IsException(next_promise)) {
            fail_reject1:
                JS_IteratorClose(ctx, iter, true);
                goto fail_reject;
            }
            ret = JS_InvokeFree(ctx, next_promise, JS_ATOM_then, 2,
                                vc(resolving_funcs));
            if (check_exception_free(ctx, ret))
                goto fail_reject1;
        }
    }
 done:
    JS_FreeValue(ctx, promise_resolve);
    JS_FreeValue(ctx, next_method);
    JS_FreeValue(ctx, iter);
    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);
    return result_promise;
 fail:
    //JS_FreeValue(ctx, next_method); // why not???
    JS_FreeValue(ctx, result_promise);
    result_promise = JS_EXCEPTION;
    goto done;
}

static __exception int perform_promise_then(JSContext *ctx,
                                            JSValueConst promise,
                                            JSValueConst *resolve_reject,
                                            JSValueConst *cap_resolving_funcs)
{
    JSPromiseData *s = JS_GetOpaque(promise, JS_CLASS_PROMISE);
    JSPromiseReactionData *rd_array[2], *rd;
    int i, j;

    rd_array[0] = NULL;
    rd_array[1] = NULL;
    for(i = 0; i < 2; i++) {
        JSValueConst handler;
        rd = js_mallocz(ctx, sizeof(*rd));
        if (!rd) {
            if (i == 1)
                promise_reaction_data_free(ctx->rt, rd_array[0]);
            return -1;
        }
        for(j = 0; j < 2; j++)
            rd->resolving_funcs[j] = js_dup(cap_resolving_funcs[j]);
        handler = resolve_reject[i];
        if (!JS_IsFunction(ctx, handler))
            handler = JS_UNDEFINED;
        rd->handler = js_dup(handler);
        rd_array[i] = rd;
    }

    if (s->promise_state == JS_PROMISE_PENDING) {
        for(i = 0; i < 2; i++)
            list_add_tail(&rd_array[i]->link, &s->promise_reactions[i]);
    } else {
        JSValueConst args[5];
        if (s->promise_state == JS_PROMISE_REJECTED && !s->is_handled) {
            JSRuntime *rt = ctx->rt;
            if (rt->host_promise_rejection_tracker)
                rt->host_promise_rejection_tracker(ctx, promise, s->promise_result,
                                                   true, rt->host_promise_rejection_tracker_opaque);
        }
        i = s->promise_state - JS_PROMISE_FULFILLED;
        rd = rd_array[i];
        args[0] = rd->resolving_funcs[0];
        args[1] = rd->resolving_funcs[1];
        args[2] = rd->handler;
        args[3] = js_bool(i);
        args[4] = s->promise_result;
        JS_EnqueueJob(ctx, promise_reaction_job, 5, args);
        for(i = 0; i < 2; i++)
            promise_reaction_data_free(ctx->rt, rd_array[i]);
    }
    s->is_handled = true;
    return 0;
}

static JSValue js_promise_then(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    JSValue ctor, result_promise, resolving_funcs[2];
    bool have_promise_hook;
    JSValueLink link;
    JSPromiseData *s;
    JSRuntime *rt;
    int i, ret;

    s = JS_GetOpaque2(ctx, this_val, JS_CLASS_PROMISE);
    if (!s)
        return JS_EXCEPTION;

    ctor = JS_SpeciesConstructor(ctx, this_val, JS_UNDEFINED);
    if (JS_IsException(ctor))
        return ctor;
    rt = ctx->rt;
    // always restore, even if js_new_promise_capability callee removes hook
    have_promise_hook = (rt->promise_hook != NULL);
    if (have_promise_hook) {
        link = (JSValueLink){rt->parent_promise, this_val};
        rt->parent_promise = &link;
    }
    result_promise = js_new_promise_capability(ctx, resolving_funcs, ctor);
    if (have_promise_hook)
        rt->parent_promise = link.next;
    JS_FreeValue(ctx, ctor);
    if (JS_IsException(result_promise))
        return result_promise;
    ret = perform_promise_then(ctx, this_val, argv, vc(resolving_funcs));
    for(i = 0; i < 2; i++)
        JS_FreeValue(ctx, resolving_funcs[i]);
    if (ret) {
        JS_FreeValue(ctx, result_promise);
        return JS_EXCEPTION;
    }
    return result_promise;
}

static JSValue js_promise_catch(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    JSValueConst args[2];
    args[0] = JS_UNDEFINED;
    args[1] = argv[0];
    return JS_Invoke(ctx, this_val, JS_ATOM_then, 2, args);
}

static JSValue js_promise_finally_value_thunk(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv,
                                              int magic, JSValueConst *func_data)
{
    return js_dup(func_data[0]);
}

static JSValue js_promise_finally_thrower(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv,
                                          int magic, JSValueConst *func_data)
{
    return JS_Throw(ctx, js_dup(func_data[0]));
}

static JSValue js_promise_then_finally_func(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv,
                                            int magic, JSValueConst *func_data)
{
    JSValueConst ctor = func_data[0];
    JSValueConst onFinally = func_data[1];
    JSValue res, promise, ret, then_func;

    res = JS_Call(ctx, onFinally, JS_UNDEFINED, 0, NULL);
    if (JS_IsException(res))
        return res;
    promise = js_promise_resolve(ctx, ctor, 1, vc(&res), 0);
    JS_FreeValue(ctx, res);
    if (JS_IsException(promise))
        return promise;
    if (magic == 0) {
        then_func = JS_NewCFunctionData(ctx, js_promise_finally_value_thunk, 0,
                                        0, 1, argv);
    } else {
        then_func = JS_NewCFunctionData(ctx, js_promise_finally_thrower, 0,
                                        0, 1, argv);
    }
    if (JS_IsException(then_func)) {
        JS_FreeValue(ctx, promise);
        return then_func;
    }
    ret = JS_InvokeFree(ctx, promise, JS_ATOM_then, 1, vc(&then_func));
    JS_FreeValue(ctx, then_func);
    return ret;
}

static JSValue js_promise_finally(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    JSValueConst onFinally = argv[0];
    JSValue ctor, ret;
    JSValue then_funcs[2];
    JSValueConst func_data[2];
    int i;

    ctor = JS_SpeciesConstructor(ctx, this_val, JS_UNDEFINED);
    if (JS_IsException(ctor))
        return ctor;
    if (!JS_IsFunction(ctx, onFinally)) {
        then_funcs[0] = js_dup(onFinally);
        then_funcs[1] = js_dup(onFinally);
    } else {
        func_data[0] = ctor;
        func_data[1] = onFinally;
        for(i = 0; i < 2; i++) {
            then_funcs[i] = JS_NewCFunctionData(ctx, js_promise_then_finally_func, 1, i, 2, func_data);
            if (JS_IsException(then_funcs[i])) {
                if (i == 1)
                    JS_FreeValue(ctx, then_funcs[0]);
                JS_FreeValue(ctx, ctor);
                return JS_EXCEPTION;
            }
        }
    }
    JS_FreeValue(ctx, ctor);
    ret = JS_Invoke(ctx, this_val, JS_ATOM_then, 2, vc(then_funcs));
    JS_FreeValue(ctx, then_funcs[0]);
    JS_FreeValue(ctx, then_funcs[1]);
    return ret;
}

static const JSCFunctionListEntry js_promise_funcs[] = {
    JS_CFUNC_MAGIC_DEF("resolve", 1, js_promise_resolve, 0 ),
    JS_CFUNC_MAGIC_DEF("reject", 1, js_promise_resolve, 1 ),
    JS_CFUNC_MAGIC_DEF("all", 1, js_promise_all, PROMISE_MAGIC_all ),
    JS_CFUNC_MAGIC_DEF("allSettled", 1, js_promise_all, PROMISE_MAGIC_allSettled ),
    JS_CFUNC_MAGIC_DEF("any", 1, js_promise_all, PROMISE_MAGIC_any ),
    JS_CFUNC_DEF("try", 1, js_promise_try ),
    JS_CFUNC_DEF("race", 1, js_promise_race ),
    JS_CFUNC_DEF("withResolvers", 0, js_promise_withResolvers ),
    JS_CGETSET_DEF("[Symbol.species]", js_get_this, NULL),
};

static const JSCFunctionListEntry js_promise_proto_funcs[] = {
    JS_CFUNC_DEF("then", 2, js_promise_then ),
    JS_CFUNC_DEF("catch", 1, js_promise_catch ),
    JS_CFUNC_DEF("finally", 1, js_promise_finally ),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Promise", JS_PROP_CONFIGURABLE ),
};

/* AsyncFunction */
static const JSCFunctionListEntry js_async_function_proto_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AsyncFunction", JS_PROP_CONFIGURABLE ),
};

static JSValue js_async_from_sync_iterator_unwrap(JSContext *ctx,
                                                  JSValueConst this_val,
                                                  int argc, JSValueConst *argv,
                                                  int magic, JSValueConst *func_data)
{
    return js_create_iterator_result(ctx, js_dup(argv[0]),
                                     JS_ToBool(ctx, func_data[0]));
}

static JSValue js_async_from_sync_iterator_unwrap_func_create(JSContext *ctx,
                                                              bool done)
{
    JSValueConst func_data[1];

    func_data[0] = js_bool(done);
    return JS_NewCFunctionData(ctx, js_async_from_sync_iterator_unwrap,
                               1, 0, 1, func_data);
}

/* AsyncIteratorPrototype */

static const JSCFunctionListEntry js_async_iterator_proto_funcs[] = {
    JS_CFUNC_DEF("[Symbol.asyncIterator]", 0, js_iterator_proto_iterator ),
    JS_CFUNC_DEF("[Symbol.asyncDispose]", 0, js_async_iterator_proto_dispose ),
};

/* AsyncFromSyncIteratorPrototype */

typedef struct JSAsyncFromSyncIteratorData {
    JSValue sync_iter;
    JSValue next_method;
} JSAsyncFromSyncIteratorData;

static void js_async_from_sync_iterator_finalizer(JSRuntime *rt,
                                                  JSValueConst val)
{
    JSAsyncFromSyncIteratorData *s =
        JS_GetOpaque(val, JS_CLASS_ASYNC_FROM_SYNC_ITERATOR);
    if (s) {
        JS_FreeValueRT(rt, s->sync_iter);
        JS_FreeValueRT(rt, s->next_method);
        js_free_rt(rt, s);
    }
}

static void js_async_from_sync_iterator_mark(JSRuntime *rt, JSValueConst val,
                                             JS_MarkFunc *mark_func)
{
    JSAsyncFromSyncIteratorData *s =
        JS_GetOpaque(val, JS_CLASS_ASYNC_FROM_SYNC_ITERATOR);
    if (s) {
        JS_MarkValue(rt, s->sync_iter, mark_func);
        JS_MarkValue(rt, s->next_method, mark_func);
    }
}

