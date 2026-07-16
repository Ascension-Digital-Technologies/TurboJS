/* Engine domain source: core/engine_core.inc -> allocation_core.
 * Ownership: core subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static size_t js_arena_usable_size(JSRuntime *rt, const void *ptr)
{
    const JSMallocBlockHeader *b;

    if (!ptr)
        return 0;
    b = container_of(ptr, JSMallocBlockHeader, user_data);
    if (b->u.block_idx == JS_ARENA_FREE_NIL) {
        if (b == arena_zero_block(rt)) {
            return 0;
        } else {
            size_t size = rt->mf.js_malloc_usable_size(b);
            if (size != 0)
                size -= sizeof(JSMallocBlockHeader);
            return size;
        }
    } else {
        return arena_block_sizes[b->block_size_idx] - sizeof(JSMallocBlockHeader);
    }
}

static void *js_arena_realloc(JSRuntime *rt, void *ptr, size_t size)
{
    JSMallocBlockHeader *b;

    /* js_realloc_rt already handles ptr == NULL and size == 0 */
    b = container_of(ptr, JSMallocBlockHeader, user_data);
    if (b->u.block_idx == JS_ARENA_FREE_NIL) {
        if (b == arena_zero_block(rt)) {
            return js_arena_malloc(rt, size);
        } else {
            JSMallocBlockHeader *nb;
            nb = rt->mf.js_realloc(rt->malloc_state.opaque, b,
                                   sizeof(JSMallocBlockHeader) + size);
            if (!nb)
                return NULL;
            nb->u.block_idx = JS_ARENA_FREE_NIL;
            nb->block_size_idx = 0xff;
            return nb->user_data;
        }
    } else {
        unsigned int block_size = arena_block_sizes[b->block_size_idx];
        size_t total_size, old_usable;
        void *new_ptr;

        total_size = ((size + JS_ARENA_ALIGN - 1) & ~(size_t)(JS_ARENA_ALIGN - 1)) +
            sizeof(JSMallocBlockHeader);
        if (total_size <= block_size)
            return ptr; /* still fits the current size class */
        new_ptr = js_arena_malloc(rt, size);
        if (!new_ptr)
            return NULL;
        {
            /* carry the merged GC/refcount fields to the relocated block */
            JSMallocBlockHeader *nb = container_of(new_ptr, JSMallocBlockHeader, user_data);
            nb->gc_obj_type = b->gc_obj_type;
            nb->mark = b->mark;
            nb->ref_count = b->ref_count;
        }
        old_usable = block_size - sizeof(JSMallocBlockHeader);
        if (size > old_usable)
            size = old_usable;
        memcpy(new_ptr, ptr, size);
        js_arena_free(rt, ptr);
        return new_ptr;
    }
}

static void *js_arena_calloc(JSRuntime *rt, size_t count, size_t size)
{
    size_t n = count * size; /* overflow already checked by js_calloc_rt */
    size_t total_size = ((n + JS_ARENA_ALIGN - 1) & ~(size_t)(JS_ARENA_ALIGN - 1)) +
        sizeof(JSMallocBlockHeader);
    if (!JS_ARENA_LARGE_BLOCKS_ONLY && total_size <= JS_ARENA_MAX_SMALL_SIZE) {
        /* Small blocks are carved from recycled (dirty) arena memory, so they
           must be zeroed explicitly. */
        void *ptr = js_arena_malloc(rt, n);
        if (unlikely(!ptr))
            return NULL;
        return memset(ptr, 0, n);
    }
    /* Large blocks come straight from the backing allocator, so let js_calloc
       do the zeroing. */
    return arena_calloc_large(rt, n);
}

/* free any arenas still mapped at runtime teardown (normally none: empty
   arenas are released eagerly as their last block is freed) */
static void js_arena_free_all(JSRuntime *rt)
{
    JSArenaState *s = &rt->arena_state;
    struct list_head *el, *el1;
    int i;
    for (i = 0; i < JS_ARENA_BLOCK_SIZE_COUNT; i++) {
        list_for_each_safe(el, el1, &s->arena_list[i]) {
            JSArena *ar = list_entry(el, JSArena, link);
            rt->mf.js_free(rt->malloc_state.opaque, ar);
        }
        init_list_head(&s->arena_list[i]);
        init_list_head(&s->free_arena_list[i]);
    }
}

void *js_calloc_rt(JSRuntime *rt, size_t count, size_t size)
{
    void *ptr;
    JSMallocState *s;

    /* Do not allocate zero bytes: behavior is platform dependent */
    assert(count != 0 && size != 0);

    if (size > 0)
        if (unlikely(count != (count * size) / size))
            return NULL;

    s = &rt->malloc_state;
    /* When malloc_limit is 0 (unlimited), malloc_limit - 1 will be SIZE_MAX. */
    if (unlikely(s->malloc_size + (count * size) > s->malloc_limit - 1))
        return NULL;

    ptr = js_arena_calloc(rt, count, size);
    if (!ptr)
        return NULL;

    s->malloc_count++;
    s->malloc_size += js_arena_usable_size(rt, ptr) + MALLOC_OVERHEAD;
    return ptr;
}

void *js_malloc_rt(JSRuntime *rt, size_t size)
{
    void *ptr;
    JSMallocState *s;

    /* Do not allocate zero bytes: behavior is platform dependent */
    if (unlikely(size == 0))
        return NULL;

    s = &rt->malloc_state;
    /* When malloc_limit is 0 (unlimited), malloc_limit - 1 will be SIZE_MAX. */
    if (unlikely(s->malloc_size + size > s->malloc_limit - 1))
        return NULL;

    ptr = js_arena_malloc(rt, size);
    if (!ptr)
        return NULL;

    s->malloc_count++;
    s->malloc_size += js_arena_usable_size(rt, ptr) + MALLOC_OVERHEAD;
    return ptr;
}

void js_free_rt(JSRuntime *rt, void *ptr)
{
    JSMallocState *s;

    if (!ptr)
        return;

    s = &rt->malloc_state;
    size_t free_size = js_arena_usable_size(rt, ptr) + MALLOC_OVERHEAD;
    if (unlikely(free_size > s->malloc_size)) {
        printf("js_free_rt: malloc_size underflow: freeing %zu but only %zu tracked\n", free_size, s->malloc_size);
        abort();
    }
    s->malloc_count--;
    s->malloc_size -= free_size;
    js_arena_free(rt, ptr);
}

void *js_realloc_rt(JSRuntime *rt, void *ptr, size_t size)
{
    size_t old_size;
    JSMallocState *s;

    if (!ptr) {
        if (size == 0)
            return NULL;
        return js_malloc_rt(rt, size);
    }
    if (unlikely(size == 0)) {
        js_free_rt(rt, ptr);
        return NULL;
    }
    old_size = js_arena_usable_size(rt, ptr);
    s = &rt->malloc_state;
    /* When malloc_limit is 0 (unlimited), malloc_limit - 1 will be SIZE_MAX. */
    if (s->malloc_size + size - old_size > s->malloc_limit - 1)
        return NULL;

    ptr = js_arena_realloc(rt, ptr, size);
    if (!ptr)
        return NULL;

    s->malloc_size += js_arena_usable_size(rt, ptr) - old_size;
    return ptr;
}

size_t js_malloc_usable_size_rt(JSRuntime *rt, const void *ptr)
{
    return js_arena_usable_size(rt, ptr);
}

/**
 * This used to be implemented as malloc + memset, but using calloc
 * yields better performance in initial, bursty allocations, something useful
 * for QuickJS.
 *
 * More information: https://github.com/quickjs-ng/quickjs/pull/519
 */
void *js_mallocz_rt(JSRuntime *rt, size_t size)
{
    return js_calloc_rt(rt, 1, size);
}

/* Throw out of memory in case of error */
void *js_calloc(JSContext *ctx, size_t count, size_t size)
{
    void *ptr;
    ptr = js_calloc_rt(ctx->rt, count, size);
    if (unlikely(!ptr)) {
        JS_ThrowOutOfMemory(ctx);
        return NULL;
    }
    return ptr;
}

/* Throw out of memory in case of error */
void *js_malloc(JSContext *ctx, size_t size)
{
    void *ptr;
    ptr = js_malloc_rt(ctx->rt, size);
    if (unlikely(!ptr)) {
        JS_ThrowOutOfMemory(ctx);
        return NULL;
    }
    return ptr;
}

/* Throw out of memory in case of error */
void *js_mallocz(JSContext *ctx, size_t size)
{
    void *ptr;
    ptr = js_mallocz_rt(ctx->rt, size);
    if (unlikely(!ptr)) {
        JS_ThrowOutOfMemory(ctx);
        return NULL;
    }
    return ptr;
}

void js_free(JSContext *ctx, void *ptr)
{
    js_free_rt(ctx->rt, ptr);
}

/* Throw out of memory in case of error */
void *js_realloc(JSContext *ctx, void *ptr, size_t size)
{
    void *ret;
    ret = js_realloc_rt(ctx->rt, ptr, size);
    if (unlikely(!ret && size != 0)) {
        JS_ThrowOutOfMemory(ctx);
        return NULL;
    }
    return ret;
}

/* store extra allocated size in *pslack if successful */
void *js_realloc2(JSContext *ctx, void *ptr, size_t size, size_t *pslack)
{
    void *ret;
    ret = js_realloc_rt(ctx->rt, ptr, size);
    if (unlikely(!ret && size != 0)) {
        JS_ThrowOutOfMemory(ctx);
        return NULL;
    }
    if (pslack) {
        size_t new_size = js_malloc_usable_size_rt(ctx->rt, ret);
        *pslack = (new_size > size) ? new_size - size : 0;
    }
    return ret;
}

size_t js_malloc_usable_size(JSContext *ctx, const void *ptr)
{
    return js_malloc_usable_size_rt(ctx->rt, ptr);
}

/* Throw out of memory exception in case of error */
char *js_strndup(JSContext *ctx, const char *s, size_t n)
{
    char *ptr;
    ptr = js_malloc(ctx, n + 1);
    if (ptr) {
        memcpy(ptr, s, n);
        ptr[n] = '\0';
    }
    return ptr;
}

char *js_strdup(JSContext *ctx, const char *str)
{
    return js_strndup(ctx, str, strlen(str));
}

static no_inline int js_realloc_array(JSContext *ctx, void **parray,
                                      int elem_size, int *psize, int req_size)
{
    int new_size;
    size_t slack;
    void *new_array;
    /* XXX: potential arithmetic overflow */
    new_size = max_int(req_size, *psize * 3 / 2);
    new_array = js_realloc2(ctx, *parray, new_size * elem_size, &slack);
    if (!new_array)
        return -1;
    new_size += slack / elem_size;
    *psize = new_size;
    *parray = new_array;
    return 0;
}

/* resize the array and update its size if req_size > *psize */
static inline int js_resize_array(JSContext *ctx, void **parray, int elem_size,
                                  int *psize, int req_size)
{
    if (unlikely(req_size > *psize))
        return js_realloc_array(ctx, parray, elem_size, psize, req_size);
    else
        return 0;
}

static void *js_dbuf_realloc(void *ctx, void *ptr, size_t size)
{
    return js_realloc(ctx, ptr, size);
}

static inline void js_dbuf_init(JSContext *ctx, DynBuf *s)
{
    dbuf_init2(s, ctx, js_dbuf_realloc);
}

static inline int is_digit(int c) {
    return c >= '0' && c <= '9';
}

static inline int string_get(JSString *p, int idx) {
    return p->is_wide_char ? str16(p)[idx] : str8(p)[idx];
}

typedef struct JSClassShortDef {
    JSAtom class_name;
    JSClassFinalizer *finalizer;
    JSClassGCMark *gc_mark;
} JSClassShortDef;

static JSClassShortDef const js_std_class_def[] = {
    { JS_ATOM_Object, NULL, NULL },                             /* JS_CLASS_OBJECT */
    { JS_ATOM_Array, js_array_finalizer, js_array_mark },       /* JS_CLASS_ARRAY */
    { JS_ATOM_Error, js_object_data_finalizer, js_object_data_mark }, /* JS_CLASS_ERROR */
    { JS_ATOM_Number, js_object_data_finalizer, js_object_data_mark }, /* JS_CLASS_NUMBER */
    { JS_ATOM_String, js_object_data_finalizer, js_object_data_mark }, /* JS_CLASS_STRING */
    { JS_ATOM_Boolean, js_object_data_finalizer, js_object_data_mark }, /* JS_CLASS_BOOLEAN */
    { JS_ATOM_Symbol, js_object_data_finalizer, js_object_data_mark }, /* JS_CLASS_SYMBOL */
    { JS_ATOM_Arguments, js_array_finalizer, js_array_mark },   /* JS_CLASS_ARGUMENTS */
    { JS_ATOM_Arguments, js_mapped_arguments_finalizer, js_mapped_arguments_mark }, /* JS_CLASS_MAPPED_ARGUMENTS */
    { JS_ATOM_Date, js_object_data_finalizer, js_object_data_mark }, /* JS_CLASS_DATE */
    { JS_ATOM_Object, NULL, NULL },                             /* JS_CLASS_MODULE_NS */
    { JS_ATOM_Function, js_c_function_finalizer, js_c_function_mark }, /* JS_CLASS_C_FUNCTION */
    { JS_ATOM_Function, js_bytecode_function_finalizer, js_bytecode_function_mark }, /* JS_CLASS_BYTECODE_FUNCTION */
    { JS_ATOM_Function, js_bound_function_finalizer, js_bound_function_mark }, /* JS_CLASS_BOUND_FUNCTION */
    { JS_ATOM_Function, js_c_function_data_finalizer, js_c_function_data_mark }, /* JS_CLASS_C_FUNCTION_DATA */
    { JS_ATOM_Function, js_c_closure_finalizer, NULL},                           /* JS_CLASS_C_CLOSURE */
    { JS_ATOM_GeneratorFunction, js_bytecode_function_finalizer, js_bytecode_function_mark },  /* JS_CLASS_GENERATOR_FUNCTION */
    { JS_ATOM_ForInIterator, js_for_in_iterator_finalizer, js_for_in_iterator_mark },      /* JS_CLASS_FOR_IN_ITERATOR */
    { JS_ATOM_RegExp, js_regexp_finalizer, NULL },                              /* JS_CLASS_REGEXP */
    { JS_ATOM_ArrayBuffer, js_array_buffer_finalizer, NULL },                   /* JS_CLASS_ARRAY_BUFFER */
    { JS_ATOM_SharedArrayBuffer, js_array_buffer_finalizer, NULL },             /* JS_CLASS_SHARED_ARRAY_BUFFER */
    { JS_ATOM_Uint8ClampedArray, js_typed_array_finalizer, js_typed_array_mark }, /* JS_CLASS_UINT8C_ARRAY */
    { JS_ATOM_Int8Array, js_typed_array_finalizer, js_typed_array_mark },       /* JS_CLASS_INT8_ARRAY */
    { JS_ATOM_Uint8Array, js_typed_array_finalizer, js_typed_array_mark },      /* JS_CLASS_UINT8_ARRAY */
    { JS_ATOM_Int16Array, js_typed_array_finalizer, js_typed_array_mark },      /* JS_CLASS_INT16_ARRAY */
    { JS_ATOM_Uint16Array, js_typed_array_finalizer, js_typed_array_mark },     /* JS_CLASS_UINT16_ARRAY */
    { JS_ATOM_Int32Array, js_typed_array_finalizer, js_typed_array_mark },      /* JS_CLASS_INT32_ARRAY */
    { JS_ATOM_Uint32Array, js_typed_array_finalizer, js_typed_array_mark },     /* JS_CLASS_UINT32_ARRAY */
    { JS_ATOM_BigInt64Array, js_typed_array_finalizer, js_typed_array_mark },   /* JS_CLASS_BIG_INT64_ARRAY */
    { JS_ATOM_BigUint64Array, js_typed_array_finalizer, js_typed_array_mark },  /* JS_CLASS_BIG_UINT64_ARRAY */
    { JS_ATOM_Float16Array, js_typed_array_finalizer, js_typed_array_mark },    /* JS_CLASS_FLOAT16_ARRAY */
    { JS_ATOM_Float32Array, js_typed_array_finalizer, js_typed_array_mark },    /* JS_CLASS_FLOAT32_ARRAY */
    { JS_ATOM_Float64Array, js_typed_array_finalizer, js_typed_array_mark },    /* JS_CLASS_FLOAT64_ARRAY */
    { JS_ATOM_DataView, js_typed_array_finalizer, js_typed_array_mark },        /* JS_CLASS_DATAVIEW */
    { JS_ATOM_BigInt, js_object_data_finalizer, js_object_data_mark },      /* JS_CLASS_BIG_INT */
    { JS_ATOM_Map, js_map_finalizer, js_map_mark },             /* JS_CLASS_MAP */
    { JS_ATOM_Set, js_map_finalizer, js_map_mark },             /* JS_CLASS_SET */
    { JS_ATOM_WeakMap, js_map_finalizer, NULL },         /* JS_CLASS_WEAKMAP */
    { JS_ATOM_WeakSet, js_map_finalizer, NULL },         /* JS_CLASS_WEAKSET */
    { JS_ATOM_Iterator, NULL, NULL },                           /* JS_CLASS_ITERATOR */
    { JS_ATOM_IteratorConcat, js_iterator_concat_finalizer, js_iterator_concat_mark }, /* JS_CLASS_ITERATOR_CONCAT */
    { JS_ATOM_IteratorHelper, js_iterator_helper_finalizer, js_iterator_helper_mark }, /* JS_CLASS_ITERATOR_HELPER */
    { JS_ATOM_IteratorWrap, js_iterator_wrap_finalizer, js_iterator_wrap_mark }, /* JS_CLASS_ITERATOR_WRAP */
    { JS_ATOM_Map_Iterator, js_map_iterator_finalizer, js_map_iterator_mark }, /* JS_CLASS_MAP_ITERATOR */
    { JS_ATOM_Set_Iterator, js_map_iterator_finalizer, js_map_iterator_mark }, /* JS_CLASS_SET_ITERATOR */
    { JS_ATOM_Array_Iterator, js_array_iterator_finalizer, js_array_iterator_mark }, /* JS_CLASS_ARRAY_ITERATOR */
    { JS_ATOM_String_Iterator, js_array_iterator_finalizer, js_array_iterator_mark }, /* JS_CLASS_STRING_ITERATOR */
    { JS_ATOM_RegExp_String_Iterator, js_regexp_string_iterator_finalizer, js_regexp_string_iterator_mark }, /* JS_CLASS_REGEXP_STRING_ITERATOR */
    { JS_ATOM_Generator, js_generator_finalizer, js_generator_mark }, /* JS_CLASS_GENERATOR */
    { JS_ATOM_DisposableStack, js_disposable_stack_finalizer, js_disposable_stack_mark }, /* JS_CLASS_DISPOSABLE_STACK */
};

static int init_class_range(JSRuntime *rt, JSClassShortDef const *tab,
                            int start, int count)
{
    JSClassDef cm_s, *cm = &cm_s;
    int i, class_id;

    for(i = 0; i < count; i++) {
        class_id = i + start;
        memset(cm, 0, sizeof(*cm));
        cm->finalizer = tab[i].finalizer;
        cm->gc_mark = tab[i].gc_mark;
        if (JS_NewClass1(rt, class_id, cm, tab[i].class_name) < 0)
            return -1;
    }
    return 0;
}

/* Uses code from LLVM project. */
static inline uintptr_t js_get_stack_pointer(void)
{
#if defined(__clang__) || defined(__GNUC__)
    return (uintptr_t)__builtin_frame_address(0);
#elif defined(_MSC_VER)
    return (uintptr_t)_AddressOfReturnAddress();
#else
    char CharOnStack = 0;
    // The volatile store here is intended to escape the local variable, to
    // prevent the compiler from optimizing CharOnStack into anything other
    // than a char on the stack.
    //
    // Tested on: MSVC 2015 - 2019, GCC 4.9 - 9, Clang 3.2 - 9, ICC 13 - 19.
    char *volatile Ptr = &CharOnStack;
    return (uintptr_t) Ptr;
#endif
}

static inline bool js_check_stack_overflow(JSRuntime *rt, size_t alloca_size)
{
    uintptr_t sp;
    sp = js_get_stack_pointer() - alloca_size;
    return unlikely(sp < rt->stack_limit);
}

JSRuntime *turbojs_internal_new_runtime2(const JSMallocFunctions *mf, void *opaque)
{
    JSRuntime *rt;
    JSMallocState ms;

    memset(&ms, 0, sizeof(ms));
    ms.opaque = opaque;
    ms.malloc_limit = 0;

    rt = mf->js_calloc(opaque, 1, sizeof(JSRuntime));
    if (!rt)
        return NULL;
    rt->mf = *mf;
    if (!rt->mf.js_malloc_usable_size) {
        /* use dummy function if none provided */
        rt->mf.js_malloc_usable_size = js_malloc_usable_size_unknown;
    }
    /* Inline what js_malloc_rt does since we cannot use it here. */
    ms.malloc_count++;
    ms.malloc_size += rt->mf.js_malloc_usable_size(rt) + MALLOC_OVERHEAD;
    rt->malloc_state = ms;
    js_arena_init(rt);
    rt->malloc_gc_threshold = 256 * 1024;
    rt->jit_compile_threshold = 100;

    init_list_head(&rt->context_list);
    init_list_head(&rt->gc_obj_list);
    init_list_head(&rt->gc_zero_ref_count_list);
    rt->gc_phase = JS_GC_PHASE_NONE;

#ifdef ENABLE_DUMPS // JS_DUMP_LEAKS
    init_list_head(&rt->string_list);
#endif
    init_list_head(&rt->job_list);

    if (JS_InitAtoms(rt))
        goto fail;

    /* create the object, array and function classes */
    if (init_class_range(rt, js_std_class_def, JS_CLASS_OBJECT,
                         countof(js_std_class_def)) < 0)
        goto fail;
    rt->class_array[JS_CLASS_ARGUMENTS].exotic = &js_arguments_exotic_methods;
    rt->class_array[JS_CLASS_MAPPED_ARGUMENTS].exotic = &js_arguments_exotic_methods;
    rt->class_array[JS_CLASS_STRING].exotic = &js_string_exotic_methods;
    rt->class_array[JS_CLASS_MODULE_NS].exotic = &js_module_ns_exotic_methods;

    rt->class_array[JS_CLASS_C_FUNCTION].call = js_call_c_function;
    rt->class_array[JS_CLASS_C_FUNCTION_DATA].call = js_call_c_function_data;
    rt->class_array[JS_CLASS_C_CLOSURE].call = js_call_c_closure;
    rt->class_array[JS_CLASS_BOUND_FUNCTION].call = js_call_bound_function;
    rt->class_array[JS_CLASS_GENERATOR_FUNCTION].call = js_call_generator_function;
    if (init_shape_hash(rt))
        goto fail;

    rt->js_class_id_alloc = JS_CLASS_INIT_COUNT;

    rt->stack_size = JS_DEFAULT_STACK_SIZE;
#ifdef __wasi__
    rt->stack_size = 0;
#endif

    JS_UpdateStackTop(rt);

    rt->current_exception = JS_UNINITIALIZED;

    return rt;
 fail:
    JS_FreeRuntime(rt);
    return NULL;
}

void *turbojs_internal_get_runtime_opaque(JSRuntime *rt)
{
    return rt->user_opaque;
}

void turbojs_internal_set_runtime_opaque(JSRuntime *rt, void *opaque)
{
    rt->user_opaque = opaque;
}

int turbojs_internal_add_runtime_finalizer(JSRuntime *rt, JSRuntimeFinalizer *finalizer,
                           void *arg)
{
    JSRuntimeFinalizerState *fs = js_malloc_rt(rt, sizeof(*fs));
    if (!fs)
        return -1;
    fs->next       = rt->finalizers;
    fs->finalizer  = finalizer;
    fs->arg        = arg;
    rt->finalizers = fs;
    return 0;
}

static void *js_def_calloc(void *opaque, size_t count, size_t size)
{
    return calloc(count, size);
}

static void *js_def_malloc(void *opaque, size_t size)
{
    return malloc(size);
}

static void js_def_free(void *opaque, void *ptr)
{
    free(ptr);
}

static void *js_def_realloc(void *opaque, void *ptr, size_t size)
{
    return realloc(ptr, size);
}

static const JSMallocFunctions def_malloc_funcs = {
    js_def_calloc,
    js_def_malloc,
    js_def_free,
    js_def_realloc,
    js__malloc_usable_size
};

JSRuntime *turbojs_internal_new_runtime(void)
{
    return JS_NewRuntime2(&def_malloc_funcs, NULL);
}

void turbojs_internal_set_memory_limit(JSRuntime *rt, size_t limit)
{
    rt->malloc_state.malloc_limit = limit;
}

void turbojs_internal_set_dump_flags(JSRuntime *rt, uint64_t flags)
{
#ifdef ENABLE_DUMPS
    rt->dump_flags = flags;
#endif
}

uint64_t turbojs_internal_get_dump_flags(JSRuntime *rt)
{
#ifdef ENABLE_DUMPS
    return rt->dump_flags;
#else
    return 0;
#endif
}

size_t turbojs_internal_get_gc_threshold(JSRuntime *rt) {
    return rt->malloc_gc_threshold;
}

/* use -1 to disable automatic GC */
void turbojs_internal_set_gc_threshold(JSRuntime *rt, size_t gc_threshold)
{
    rt->malloc_gc_threshold = gc_threshold;
}

#define malloc(s) malloc_is_forbidden(s)
#define free(p) free_is_forbidden(p)
#define realloc(p,s) realloc_is_forbidden(p,s)

void turbojs_internal_set_interrupt_handler(JSRuntime *rt, JSInterruptHandler *cb, void *opaque)
{
    rt->interrupt_handler = cb;
    rt->interrupt_opaque = opaque;
}

void turbojs_internal_set_can_block(JSRuntime *rt, bool can_block)
{
    rt->can_block = can_block;
}

void turbojs_internal_set_shared_array_buffer_functions(JSRuntime *rt,
                                                    const JSSharedArrayBufferFunctions *sf)
{
    rt->sab_funcs = *sf;
}

/* return 0 if OK, < 0 if exception */
int turbojs_internal_enqueue_job(JSContext *ctx, JSJobFunc *job_func,
                  int argc, JSValueConst *argv)
{
    JSRuntime *rt = ctx->rt;
    JSJobEntry *e;
    int i;

    assert(!rt->in_free);

    e = js_malloc(ctx, sizeof(*e) + argc * sizeof(JSValue));
    if (!e)
        return -1;
    e->ctx = ctx;
    e->job_func = job_func;
    e->argc = argc;
    for(i = 0; i < argc; i++) {
        e->argv[i] = js_dup(argv[i]);
    }
    list_add_tail(&e->link, &rt->job_list);
    return 0;
}

