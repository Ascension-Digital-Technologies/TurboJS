#ifndef QJS_INTERNAL_FOUNDATION_TYPES_H
#define QJS_INTERNAL_FOUNDATION_TYPES_H

/*
 * Shared private engine data model.
 *
 * Owns the foundational runtime, context, value-support, allocator, atom,
 * string, object, shape, module, bytecode, and GC structure declarations.
 * This header is private and intentionally ABI-unstable.
 */

static inline JSValueConst *vc(JSValue *vals)
{
    return (JSValueConst *)vals;
}

static inline JSValue unsafe_unconst(JSValueConst v)
{
#ifdef JS_CHECK_JSVALUE
    return (JSValue)v;
#else
    return v;
#endif
}

static inline JSValueConst safe_const(JSValue v)
{
#ifdef JS_CHECK_JSVALUE
    return (JSValueConst)v;
#else
    return v;
#endif
}

enum {
    /* classid tag        */    /* union usage   | properties */
    JS_CLASS_OBJECT = 1,        /* must be first */
    JS_CLASS_ARRAY,             /* u.array       | length */
    JS_CLASS_ERROR,
    JS_CLASS_NUMBER,            /* u.object_data */
    JS_CLASS_STRING,            /* u.object_data */
    JS_CLASS_BOOLEAN,           /* u.object_data */
    JS_CLASS_SYMBOL,            /* u.object_data */
    JS_CLASS_ARGUMENTS,         /* u.array       | length */
    JS_CLASS_MAPPED_ARGUMENTS,  /*               | length */
    JS_CLASS_DATE,              /* u.object_data */
    JS_CLASS_MODULE_NS,
    JS_CLASS_C_FUNCTION,        /* u.cfunc */
    JS_CLASS_BYTECODE_FUNCTION, /* u.func */
    JS_CLASS_BOUND_FUNCTION,    /* u.bound_function */
    JS_CLASS_C_FUNCTION_DATA,   /* u.c_function_data_record */
    JS_CLASS_C_CLOSURE,         /* u.c_closure_record */
    JS_CLASS_GENERATOR_FUNCTION, /* u.func */
    JS_CLASS_FOR_IN_ITERATOR,   /* u.for_in_iterator */
    JS_CLASS_REGEXP,            /* u.regexp */
    JS_CLASS_ARRAY_BUFFER,      /* u.array_buffer */
    JS_CLASS_SHARED_ARRAY_BUFFER, /* u.array_buffer */
    JS_CLASS_UINT8C_ARRAY,      /* u.array (typed_array) */
    JS_CLASS_INT8_ARRAY,        /* u.array (typed_array) */
    JS_CLASS_UINT8_ARRAY,       /* u.array (typed_array) */
    JS_CLASS_INT16_ARRAY,       /* u.array (typed_array) */
    JS_CLASS_UINT16_ARRAY,      /* u.array (typed_array) */
    JS_CLASS_INT32_ARRAY,       /* u.array (typed_array) */
    JS_CLASS_UINT32_ARRAY,      /* u.array (typed_array) */
    JS_CLASS_BIG_INT64_ARRAY,   /* u.array (typed_array) */
    JS_CLASS_BIG_UINT64_ARRAY,  /* u.array (typed_array) */
    JS_CLASS_FLOAT16_ARRAY,     /* u.array (typed_array) */
    JS_CLASS_FLOAT32_ARRAY,     /* u.array (typed_array) */
    JS_CLASS_FLOAT64_ARRAY,     /* u.array (typed_array) */
    JS_CLASS_DATAVIEW,          /* u.typed_array */
    JS_CLASS_BIG_INT,           /* u.object_data */
    JS_CLASS_MAP,               /* u.map_state */
    JS_CLASS_SET,               /* u.map_state */
    JS_CLASS_WEAKMAP,           /* u.map_state */
    JS_CLASS_WEAKSET,           /* u.map_state */
    JS_CLASS_ITERATOR,
    JS_CLASS_ITERATOR_CONCAT,   /* u.iterator_concat_data */
    JS_CLASS_ITERATOR_HELPER,   /* u.iterator_helper_data */
    JS_CLASS_ITERATOR_WRAP,     /* u.iterator_wrap_data */
    JS_CLASS_MAP_ITERATOR,      /* u.map_iterator_data */
    JS_CLASS_SET_ITERATOR,      /* u.map_iterator_data */
    JS_CLASS_ARRAY_ITERATOR,    /* u.array_iterator_data */
    JS_CLASS_STRING_ITERATOR,   /* u.array_iterator_data */
    JS_CLASS_REGEXP_STRING_ITERATOR,   /* u.regexp_string_iterator_data */
    JS_CLASS_GENERATOR,         /* u.generator_data */
    JS_CLASS_DISPOSABLE_STACK,
    JS_CLASS_PROXY,             /* u.proxy_data */
    JS_CLASS_PROMISE,           /* u.promise_data */
    JS_CLASS_PROMISE_RESOLVE_FUNCTION,  /* u.promise_function_data */
    JS_CLASS_PROMISE_REJECT_FUNCTION,   /* u.promise_function_data */
    JS_CLASS_ASYNC_FUNCTION,            /* u.func */
    JS_CLASS_ASYNC_FUNCTION_RESOLVE,    /* u.async_function_data */
    JS_CLASS_ASYNC_FUNCTION_REJECT,     /* u.async_function_data */
    JS_CLASS_ASYNC_FROM_SYNC_ITERATOR,  /* u.async_from_sync_iterator_data */
    JS_CLASS_ASYNC_GENERATOR_FUNCTION,  /* u.func */
    JS_CLASS_ASYNC_GENERATOR,   /* u.async_generator_data */
    JS_CLASS_ASYNC_DISPOSABLE_STACK,
    JS_CLASS_WEAK_REF,
    JS_CLASS_FINALIZATION_REGISTRY,
    JS_CLASS_DOM_EXCEPTION,
    JS_CLASS_CALL_SITE,
    JS_CLASS_RAWJSON,

    JS_CLASS_INIT_COUNT, /* last entry for predefined classes */
};

/* number of typed array types */
#define JS_TYPED_ARRAY_COUNT  (JS_CLASS_FLOAT64_ARRAY - JS_CLASS_UINT8C_ARRAY + 1)
static uint8_t const typed_array_size_log2[JS_TYPED_ARRAY_COUNT];
#define typed_array_size_log2(classid)  (typed_array_size_log2[(classid)- JS_CLASS_UINT8C_ARRAY])

typedef enum JSErrorEnum {
    JS_EVAL_ERROR,
    JS_RANGE_ERROR,
    JS_REFERENCE_ERROR,
    JS_SYNTAX_ERROR,
    JS_TYPE_ERROR,
    JS_URI_ERROR,
    JS_INTERNAL_ERROR,
    JS_AGGREGATE_ERROR,
    JS_SUPPRESSED_ERROR,

    JS_NATIVE_ERROR_COUNT, /* number of different NativeError objects */
    JS_PLAIN_ERROR = JS_NATIVE_ERROR_COUNT
} JSErrorEnum;

#define JS_MAX_LOCAL_VARS 65535
#define JS_STACK_SIZE_MAX 65534
#define JS_STRING_LEN_MAX ((1 << 30) - 1)
// 1,024 bytes is about the cutoff point where it starts getting
// more profitable to ref slice than to copy
#define JS_STRING_SLICE_LEN_MAX 1024 // in bytes

/* strings <= this length are not concatenated using ropes. if too
   small, the rope memory overhead becomes high. */
#define JS_STRING_ROPE_SHORT_LEN  512
/* specific threshold for initial rope use */
#define JS_STRING_ROPE_SHORT2_LEN 8192
/* rope depth at which we rebalance */
#define JS_STRING_ROPE_MAX_DEPTH 60

#define __exception __attribute__((warn_unused_result))

typedef struct JSShape JSShape;
typedef struct JSString JSString;
typedef struct JSString JSAtomStruct;
typedef struct JSStringRope JSStringRope;

#define JS_VALUE_GET_OBJ(v) ((JSObject *)JS_VALUE_GET_PTR(v))
#define JS_VALUE_GET_STRING(v) ((JSString *)JS_VALUE_GET_PTR(v))
#define JS_VALUE_GET_STRING_ROPE(v) ((JSStringRope *)JS_VALUE_GET_PTR(v))

typedef enum {
    JS_GC_PHASE_NONE,
    JS_GC_PHASE_DECREF,
    JS_GC_PHASE_REMOVE_CYCLES,
} JSGCPhaseEnum;

typedef struct JSMallocState {
    size_t malloc_count;
    size_t malloc_size;
    size_t malloc_limit;
    void *opaque; /* user opaque */
} JSMallocState;

/* Small-block "arena" allocator.
   js_{malloc,free,realloc,calloc}_rt serve allocations up to ~512
   bytes from size-classed 4KB arenas carved out of the underlying
   JSMallocFunctions allocator, so the backing allocator (system malloc,
   mimalloc, ...) sees roughly one request per arena refill instead of one per
   object. */

#define JS_ARENA_ALIGN             8
#define JS_ARENA_SIZE              4096
#define JS_ARENA_BLOCK_SIZE_COUNT  31
#define JS_ARENA_MAX_SMALL_SIZE    512
#define JS_ARENA_FREE_NIL          0xffff

#if defined(__SANITIZE_ADDRESS__)
/* route every allocation through the backing malloc so ASan sees each block */
#define JS_ARENA_LARGE_BLOCKS_ONLY 1
#else
#define JS_ARENA_LARGE_BLOCKS_ONLY 0
#endif

/* 8-byte header preceding every user allocation. It carries the allocator
   bookkeeping (block_idx/free_next + block_size_idx) and, the
   GC/refcount fields (gc_obj_type/mark/ref_count) that would otherwise sit in
   the object body. The 8-byte size keeps user_data JS_ARENA_ALIGN-aligned. */
typedef struct JSMallocBlockHeader {
    union {
        uint16_t block_idx;   /* JS_ARENA_FREE_NIL => large or zero-size block */
        uint16_t free_next;   /* next free block index while on a free list */
    } u;
    uint8_t  block_size_idx;
    /* GC/refcount header merged into the allocator header:
       the object body keeps only JSGCObjectHeader.link (no ref_count/flags). */
    uint8_t  gc_obj_type : 7; /* JSGCObjectTypeEnum for GC objects */
    uint8_t  mark : 1;        /* used by the cycle collector */
    int      ref_count;
    _Alignas(JS_ARENA_ALIGN) uint8_t user_data[];
} JSMallocBlockHeader;

typedef struct JSArena {
    struct list_head free_link;   /* in free_arena_list[idx] while not full */
    struct list_head link;        /* in arena_list[idx] for the whole lifetime */
    uint8_t  block_size_idx;
    uint16_t n_used_blocks;
    uint16_t n_blocks;
    uint16_t first_free_block;    /* JS_ARENA_FREE_NIL if none */
    _Alignas(JS_ARENA_ALIGN) uint8_t blocks[];
} JSArena;

typedef struct JSArenaState {
    struct list_head arena_list[JS_ARENA_BLOCK_SIZE_COUNT];
    struct list_head free_arena_list[JS_ARENA_BLOCK_SIZE_COUNT];
    _Alignas(JS_ARENA_ALIGN) uint8_t zero_size_block[sizeof(JSMallocBlockHeader)];
} JSArenaState;

typedef struct JSRuntimeFinalizerState {
    struct JSRuntimeFinalizerState *next;
    JSRuntimeFinalizer *finalizer;
    void *arg;
} JSRuntimeFinalizerState;

typedef struct JSValueLink {
    struct JSValueLink *next;
    JSValueConst value;
} JSValueLink;

struct JSRuntime {
    JSMallocFunctions mf;
    JSMallocState malloc_state;
    JSArenaState arena_state;
    const char *rt_info;

    int atom_hash_size; /* power of two */
    int atom_count;
    int atom_size;
    int atom_count_resize; /* resize hash table at this count */
    uint32_t *atom_hash;
    JSAtomStruct **atom_array;
    int atom_free_index; /* 0 = none */

    JSClassID js_class_id_alloc; /* counter for user defined classes */
    int class_count;    /* size of class_array */
    JSClass *class_array;

    struct list_head context_list; /* list of JSContext.link */
    /* list of JSGCObjectHeader.link. List of allocated GC objects (used
       by the garbage collector) */
    struct list_head gc_obj_list;
    /* list of JSGCObjectHeader.link. Used during JS_FreeValueRT() */
    struct list_head gc_zero_ref_count_list;
    struct list_head tmp_obj_list; /* used during GC */
    JSGCPhaseEnum gc_phase : 8;
    size_t malloc_gc_threshold;
#ifdef ENABLE_DUMPS // JS_DUMP_LEAKS
    struct list_head string_list; /* list of JSString.link */
#endif
    /* stack limitation */
    uintptr_t stack_size; /* in bytes, 0 if no limit */
    uintptr_t stack_top;
    uintptr_t stack_limit; /* lower stack limit */

    JSValue current_exception;
    /* true if inside an out of memory error, to avoid recursing */
    bool in_out_of_memory;
    /* true if inside build_backtrace, to avoid recursing */
    bool in_build_stack_trace;
    /* true if inside JS_FreeRuntime */
    bool in_free;

    struct JSStackFrame *current_stack_frame;

    JSInterruptHandler *interrupt_handler;
    void *interrupt_opaque;

    JSPromiseHook *promise_hook;
    void *promise_hook_opaque;
    // for smuggling the parent promise from js_promise_then
    // to js_promise_constructor
    JSValueLink *parent_promise;

    JSHostPromiseRejectionTracker *host_promise_rejection_tracker;
    void *host_promise_rejection_tracker_opaque;

    struct list_head job_list; /* list of JSJobEntry.link */

    bool module_normalize_has_attr;
    union {
        JSModuleNormalizeFunc *module_normalize_func;
        JSModuleNormalizeFunc2 *module_normalize_func2;
    } normalize_u;
    bool module_loader_has_attr;
    union {
        JSModuleLoaderFunc *module_loader_func;
        JSModuleLoaderFunc2 *module_loader_func2;
    } u;
    JSModuleCheckSupportedImportAttributes *module_check_attrs;
    void *module_loader_opaque;
    /* timestamp for internal use in module evaluation */
    int64_t module_async_evaluation_next_timestamp;

    /* used to allocate, free and clone SharedArrayBuffers */
    JSSharedArrayBufferFunctions sab_funcs;

    bool can_block; /* true if Atomics.wait can block */
    uint32_t dump_flags : 24;

    /* Shape hash table */
    int shape_hash_bits;
    int shape_hash_size;
    int shape_hash_count; /* number of hashed shapes */
    JSShape **shape_hash;
    void *user_opaque;
    void *libc_opaque;
    JSRuntimeFinalizerState *finalizers;

    /* TurboJS baseline JIT state. Kept runtime-owned so native code never
       outlives the engine instance that created it. */
    void *jit_code_cache;
    uint32_t jit_compile_threshold;
    uint64_t jit_interpreted_calls;
    uint64_t jit_native_calls;
    uint64_t jit_guard_failures;
};

struct JSClass {
    uint32_t class_id; /* 0 means free entry */
    JSAtom class_name;
    JSClassFinalizer *finalizer;
    JSClassGCMark *gc_mark;
    JSClassCall *call;
    /* pointers for exotic behavior, can be NULL if none are present */
    const JSClassExoticMethods *exotic;
};

typedef struct JSStackFrame {
    struct JSStackFrame *prev_frame; /* NULL if first stack frame */
    JSValue cur_func; /* current function, JS_UNDEFINED if the frame is detached */
    JSValue *arg_buf; /* arguments */
    JSValue *var_buf; /* variables */
    struct JSVarRef **var_refs; /* references to arguments or local variables */
    uint8_t *cur_pc; /* only used in bytecode functions : PC of the
                        instruction after the call */
    uint16_t var_ref_count; /* number of var refs */
    uint16_t arg_count;
    bool is_strict_mode;
    /* only used in generators. Current stack pointer value. NULL if
       the function is running. */
    JSValue *cur_sp;
} JSStackFrame;

typedef enum {
    JS_GC_OBJ_TYPE_JS_OBJECT,
    JS_GC_OBJ_TYPE_FUNCTION_BYTECODE,
    JS_GC_OBJ_TYPE_SHAPE,
    JS_GC_OBJ_TYPE_VAR_REF,
    JS_GC_OBJ_TYPE_ASYNC_FUNCTION,
    JS_GC_OBJ_TYPE_JS_CONTEXT,
} JSGCObjectTypeEnum;

/* header for GC objects. GC objects are C data structures with a
   reference count that can reference other GC objects. JS Objects are
   a particular type of GC object. */
struct JSGCObjectHeader {
    /* ref_count/gc_obj_type/mark live in the allocator block header (js_rc(p),
       the 8 bytes before every allocation); only the GC list link remains in
       the object body. */
    struct list_head link;
};

typedef struct JSVarRef {
    JSGCObjectHeader header; /* {link}; must come first so &p->header == p */
    uint8_t is_detached;
    uint8_t is_lexical; /* only used with global variables */
    uint8_t is_const; /* only used with global variables */
    JSValue *pvalue; /* pointer to the value, either on the stack or
                        to 'value' */
    union {
        JSValue value; /* used when is_detached = true */
        struct {
            uint16_t var_ref_idx; /* index in JSStackFrame.var_refs[] */
            JSStackFrame *stack_frame;
        }; /* used when is_detached = false */
    };
} JSVarRef;

/* Accessors for the reference count and GC mark/type. These fields live in the
   arena block header (the 8 bytes before every allocation, reached via
   js_rc()), not in the object body. The macros yield lvalues, and the argument
   is always the object/header pointer (whose address equals the GC header's,
   since the header is the first member). */
static inline JSMallocBlockHeader *js_rc(const void *p) {
    return (JSMallocBlockHeader *)((uint8_t *)(uintptr_t)p - offsetof(JSMallocBlockHeader, user_data));
}
#define JS_REF_COUNT(p) (js_rc(p)->ref_count)
#define JS_GC_TYPE(p)   (js_rc(p)->gc_obj_type)
#define JS_GC_MARK(p)   (js_rc(p)->mark)

/* bigint */
typedef int32_t js_slimb_t;
typedef uint32_t js_limb_t;
typedef int64_t js_sdlimb_t;
typedef uint64_t js_dlimb_t;

#define JS_LIMB_DIGITS 9

/* Must match the size of short_big_int in JSValueUnion */
#define JS_LIMB_BITS 32
#define JS_SHORT_BIG_INT_BITS JS_LIMB_BITS
#define JS_BIGINT_MAX_SIZE ((1024 * 1024) / JS_LIMB_BITS) /* in limbs */
#define JS_SHORT_BIG_INT_MIN INT32_MIN
#define JS_SHORT_BIG_INT_MAX INT32_MAX


typedef struct JSBigInt {
    uint32_t len; /* number of limbs, >= 1 */
    js_limb_t tab[]; /* two's complement representation, always
                        normalized so that 'len' is the minimum
                        possible length >= 1 */
} JSBigInt;

/* this bigint structure can hold a 64 bit integer */
typedef struct {
    js_limb_t big_int_buf[sizeof(JSBigInt) / sizeof(js_limb_t)]; /* for JSBigInt */
    /* must come just after */
    js_limb_t tab[(64 + JS_LIMB_BITS - 1) / JS_LIMB_BITS];
} JSBigIntBuf;

typedef enum {
    JS_AUTOINIT_ID_PROTOTYPE,
    JS_AUTOINIT_ID_MODULE_NS,
    JS_AUTOINIT_ID_PROP,
    JS_AUTOINIT_ID_BYTECODE,
} JSAutoInitIDEnum;

enum {
    JS_BUILTIN_ARRAY_FROMASYNC = 1,
    JS_BUILTIN_ITERATOR_ZIP,
    JS_BUILTIN_ITERATOR_ZIP_KEYED,
};

/* must be large enough to have a negligible runtime cost and small
   enough to call the interrupt callback often. */
#define JS_INTERRUPT_COUNTER_INIT 10000

struct JSContext {
    JSGCObjectHeader header; /* must come first */
    JSRuntime *rt;
    struct list_head link;

    uint16_t binary_object_count;
    uint32_t binary_object_size : 31;

    /* true if the array prototype is "normal":
       - no small index properties which are get/set or non writable
       - its prototype is Object.prototype
       - Object.prototype has no small index properties which are get/set or non writable
       - the prototype of Object.prototype is null (always true as it is immutable)
    */
    uint8_t std_array_prototype : 1;

    JSShape *array_shape;   /* initial shape for Array objects */
    JSShape *arguments_shape;  /* shape for arguments objects */
    JSShape *mapped_arguments_shape;  /* shape for mapped arguments objects */
    JSShape *regexp_shape;  /* shape for regexp objects */
    JSShape *regexp_result_shape;  /* shape for regexp result objects */

    JSValue *class_proto;
    JSValue function_proto;
    JSValue function_ctor;
    JSValue array_ctor;
    JSValue regexp_ctor;
    JSValue promise_ctor;
    JSValue native_error_proto[JS_NATIVE_ERROR_COUNT];
    JSValue error_ctor;
    JSValue error_back_trace;
    JSValue error_prepare_stack;
    JSValue error_stack_trace_limit;
    JSValue iterator_ctor;
    JSValue iterator_ctor_getset;
    JSValue iterator_proto;
    JSValue async_iterator_proto;
    JSValue array_proto_values;
    JSValue throw_type_error;
    JSValue eval_obj;

    JSValue global_obj; /* global object */
    JSValue global_var_obj; /* contains the global let/const definitions */

    double time_origin;

    uint64_t random_state;

    /* when the counter reaches zero, JSRutime.interrupt_handler is called */
    int interrupt_counter;

    struct list_head loaded_modules; /* list of JSModuleDef.link */

    /* if NULL, RegExp compilation is not supported */
    JSValue (*compile_regexp)(JSContext *ctx, JSValueConst pattern,
                              JSValueConst flags);
    /* if NULL, eval is not supported */
    JSValue (*eval_internal)(JSContext *ctx, JSValueConst this_obj,
                             const char *input, size_t input_len,
                             const char *filename, int line, int flags, int scope_idx);
    void *user_opaque;
};

typedef union JSFloat64Union {
    double d;
    uint64_t u64;
    uint32_t u32[2];
} JSFloat64Union;

typedef enum {
    JS_WEAK_REF_KIND_MAP,
    JS_WEAK_REF_KIND_WEAK_REF,
    JS_WEAK_REF_KIND_FINALIZATION_REGISTRY_ENTRY,
} JSWeakRefKindEnum;

typedef struct JSWeakRefRecord {
    JSWeakRefKindEnum kind;
    struct JSWeakRefRecord *next_weak_ref;
    union {
        struct JSMapRecord *map_record;
        struct JSWeakRefData *weak_ref_data;
        struct JSFinRecEntry *fin_rec_entry;
    } u;
} JSWeakRefRecord;

typedef struct JSMapRecord {
    int ref_count; /* used during enumeration to avoid freeing the record */
    bool empty; /* true if the record is deleted */
    struct JSMapState *map;
    struct list_head link;
    struct list_head hash_link;
    JSValue key;
    JSValue value;
} JSMapRecord;

typedef struct JSMapState {
    bool is_weak; /* true if WeakSet/WeakMap */
    struct list_head records; /* list of JSMapRecord.link */
    uint32_t record_count;
    struct list_head *hash_table;
    uint32_t hash_size; /* must be a power of two */
    uint32_t record_count_threshold; /* count at which a hash table
                                        resize is needed */
} JSMapState;

enum
{
    JS_TO_STRING_IS_PROPERTY_KEY = 1 << 0,
    JS_TO_STRING_NO_SIDE_EFFECTS = 1 << 1,
};

enum {
    JS_ATOM_TYPE_STRING = 1,
    JS_ATOM_TYPE_GLOBAL_SYMBOL,
    JS_ATOM_TYPE_SYMBOL,
    JS_ATOM_TYPE_PRIVATE,
};

enum {
    JS_ATOM_HASH_SYMBOL,
    JS_ATOM_HASH_PRIVATE,
};

typedef enum {
    JS_ATOM_KIND_STRING,
    JS_ATOM_KIND_SYMBOL,
    JS_ATOM_KIND_PRIVATE,
} JSAtomKindEnum;

typedef enum {
    JS_STRING_KIND_NORMAL,
    JS_STRING_KIND_SLICE,
    JS_STRING_KIND_INDIRECT,
} JSStringKind;

#define JS_ATOM_HASH_MASK  ((1 << 28) - 1)

struct JSString {
    uint32_t len : 31;
    uint32_t is_wide_char : 1; /* 0 = 8 bits, 1 = 16 bits characters */
    /* for JS_ATOM_TYPE_SYMBOL: hash = 0, atom_type = 3,
       for JS_ATOM_TYPE_PRIVATE: hash = 1, atom_type = 3
       XXX: could change encoding to have one more bit in hash */
    uint32_t hash : 28;
    uint32_t kind : 2;
    uint32_t atom_type : 2; /* != 0 if atom, JS_ATOM_TYPE_x */
    uint32_t hash_next; /* atom_index for JS_ATOM_TYPE_SYMBOL */
    JSWeakRefRecord *first_weak_ref;
#ifdef ENABLE_DUMPS // JS_DUMP_LEAKS
    struct list_head link; /* string list */
#endif
};

typedef struct JSStringSlice {
    JSString *parent;
    uint32_t start; // in bytes, not characters
} JSStringSlice;

struct JSStringRope {
    uint32_t len;
    uint8_t is_wide_char; /* 0 = 8 bits, 1 = 16 bits characters */
    uint8_t depth;        /* max depth of the rope tree */
    JSValue left;
    JSValue right;        /* might be the empty string */
};

static inline void *strv(JSString *p)
{
    JSStringSlice *slice;
    void **indirect;

    switch (p->kind) {
    case JS_STRING_KIND_NORMAL:
        return (void *)&p[1];
    case JS_STRING_KIND_SLICE:
        slice = (void *)&p[1];
        return (char *)&slice->parent[1] + slice->start;
    case JS_STRING_KIND_INDIRECT:
        indirect = (void *)&p[1];
        return *indirect;
    }
    abort();
    return NULL;
}

static inline uint8_t *str8(JSString *p)
{
    return strv(p);
}

static inline uint16_t *str16(JSString *p)
{
    return strv(p);
}

typedef enum {
    JS_CLOSURE_LOCAL, /* 'var_idx' is the index of a local variable in the parent function */
    JS_CLOSURE_ARG, /* 'var_idx' is the index of an argument variable in the parent function */
    JS_CLOSURE_REF, /* 'var_idx' is the index of a closure variable in the parent function */
    JS_CLOSURE_GLOBAL_REF, /* 'var_idx' is the index of a closure variable in the parent
                              function referencing a global variable */
    JS_CLOSURE_GLOBAL_DECL, /* global variable declaration (eval code only) */
    JS_CLOSURE_GLOBAL, /* global variable (eval code only) */
    JS_CLOSURE_MODULE_DECL, /* definition of a module variable (eval code only) */
    JS_CLOSURE_MODULE_IMPORT, /* definition of a module import (eval code only) */
} JSClosureTypeEnum;

typedef struct JSClosureVar {
    uint8_t closure_type : 3; /* see JSClosureTypeEnum */
    uint8_t is_lexical : 1; /* lexical variable */
    uint8_t is_const : 1; /* const variable (is_lexical = 1 if is_const = 1) */
    uint8_t var_kind : 4; /* see JSVarKindEnum */
    /* 7 bits available */
    uint16_t var_idx; /* JS_CLOSURE_LOCAL/JS_CLOSURE_ARG: index to a normal variable of the
                    parent function. otherwise: index to a closure
                    variable of the parent function */
    JSAtom var_name;
} JSClosureVar;

#define ARG_SCOPE_INDEX 1
#define ARG_SCOPE_END (-2)

typedef struct JSVarScope {
    int parent;  /* index into fd->scopes of the enclosing scope */
    int first;   /* index into fd->vars of the last variable in this scope */
    uint8_t has_using : 1; /* scope has using declarations */
    uint8_t is_await_using : 1; /* scope has await using declarations */
    int using_label_catch; /* label for catch handler (-1 if none) */
    int using_label_end;   /* label for end of disposal block (-1 if none) */
} JSVarScope;

typedef enum {
    /* XXX: add more variable kinds here instead of using bit fields */
    JS_VAR_NORMAL,
    JS_VAR_FUNCTION_DECL, /* lexical var with function declaration */
    JS_VAR_NEW_FUNCTION_DECL, /* lexical var with async/generator
                                 function declaration */
    JS_VAR_CATCH,
    JS_VAR_FUNCTION_NAME, /* function expression name */
    JS_VAR_PRIVATE_FIELD,
    JS_VAR_PRIVATE_METHOD,
    JS_VAR_PRIVATE_GETTER,
    JS_VAR_PRIVATE_SETTER, /* must come after JS_VAR_PRIVATE_GETTER */
    JS_VAR_PRIVATE_GETTER_SETTER, /* must come after JS_VAR_PRIVATE_SETTER */
    JS_VAR_USING, /* using declaration variable */
    JS_VAR_USING_METHOD, /* hidden local holding the cached dispose method
                            for the preceding JS_VAR_USING var (always
                            allocated immediately after it). */
} JSVarKindEnum;

/* XXX: could use a different structure in bytecode functions to save
   memory */
typedef struct JSVarDef {
    JSAtom var_name;
    /* index into fd->scopes of this variable lexical scope */
    int scope_level;
    /* during compilation:
        - if scope_level = 0: scope in which the variable is defined
        - if scope_level != 0: index into fd->vars of the next
          variable in the same or enclosing lexical scope
       in a bytecode function:
       index into fd->vars of the next
       variable in the same or enclosing lexical scope
    */
    int scope_next;
    uint8_t is_const : 1;
    uint8_t is_lexical : 1;
    uint8_t is_captured : 1;
    uint8_t is_static_private : 1; /* only used during private class field parsing */
    uint8_t var_kind : 4; /* see JSVarKindEnum */
    /* if is_captured = true, provides the index of the corresponding
       JSVarRef on stack */
    uint16_t var_ref_idx;
    /* only used during compilation: function pool index for lexical
       variables with var_kind =
       JS_VAR_FUNCTION_DECL/JS_VAR_NEW_FUNCTION_DECL or scope level of
       the definition of the 'var' variables (they have scope_level =
       0) */
    int func_pool_idx; /* only used during compilation : index in
                          the constant pool for hoisted function
                          definition */
} JSVarDef;

/* for the encoding of the pc2line table */
#define PC2LINE_BASE     (-1)
#define PC2LINE_RANGE    5
#define PC2LINE_OP_FIRST 1
#define PC2LINE_DIFF_PC_MAX ((255 - PC2LINE_OP_FIRST) / PC2LINE_RANGE)

typedef enum JSFunctionKindEnum {
    JS_FUNC_NORMAL = 0,
    JS_FUNC_GENERATOR = (1 << 0),
    JS_FUNC_ASYNC = (1 << 1),
    JS_FUNC_ASYNC_GENERATOR = (JS_FUNC_GENERATOR | JS_FUNC_ASYNC),
} JSFunctionKindEnum;

typedef struct JSFunctionBytecode {
    JSGCObjectHeader header; /* must come first */
    uint8_t is_strict_mode : 1;
    uint8_t has_prototype : 1; /* true if a prototype field is necessary */
    uint8_t has_simple_parameter_list : 1;
    uint8_t is_derived_class_constructor : 1;
    /* true if home_object needs to be initialized */
    uint8_t need_home_object : 1;
    uint8_t func_kind : 2;
    uint8_t new_target_allowed : 1;
    uint8_t super_call_allowed : 1;
    uint8_t super_allowed : 1;
    uint8_t arguments_allowed : 1;
    uint8_t backtrace_barrier : 1; /* stop backtrace on this function */
    /* XXX: 5 bits available */
    uint8_t *byte_code_buf; /* (self pointer) */
    int byte_code_len;
    JSAtom func_name;
    JSVarDef *vardefs; /* arguments + local variables (arg_count + var_count) (self pointer) */
    JSClosureVar *closure_var; /* list of variables in the closure (self pointer) */
    uint16_t arg_count;
    uint16_t var_count;
    uint16_t defined_arg_count; /* for length function property */
    uint16_t stack_size; /* maximum stack size */
    uint16_t var_ref_count; /* number of local variable references */
    uint16_t closure_var_count;
    int cpool_count;
    JSContext *realm; /* function realm */
    JSValue *cpool; /* constant pool (self pointer) */
    JSAtom filename;
    int line_num;
    int col_num;
    int source_len;
    int pc2line_len;
    uint8_t *pc2line_buf;
    char *source;

    /* Per-function tier metadata. The native allocation itself is owned by
       JSRuntime.jit_code_cache and keyed by this bytecode object. */
    uint32_t jit_call_count;
    uint8_t jit_compilation_attempted;
    uint8_t jit_reserved[3];
} JSFunctionBytecode;

typedef struct JSBoundFunction {
    JSValue func_obj;
    JSValue this_val;
    int argc;
    JSValue argv[];
} JSBoundFunction;

typedef enum JSIteratorKindEnum {
    JS_ITERATOR_KIND_KEY,
    JS_ITERATOR_KIND_VALUE,
    JS_ITERATOR_KIND_KEY_AND_VALUE,
} JSIteratorKindEnum;

typedef enum JSIteratorHelperKindEnum {
    JS_ITERATOR_HELPER_KIND_DROP,
    JS_ITERATOR_HELPER_KIND_EVERY,
    JS_ITERATOR_HELPER_KIND_FILTER,
    JS_ITERATOR_HELPER_KIND_FIND,
    JS_ITERATOR_HELPER_KIND_FLAT_MAP,
    JS_ITERATOR_HELPER_KIND_FOR_EACH,
    JS_ITERATOR_HELPER_KIND_MAP,
    JS_ITERATOR_HELPER_KIND_SOME,
    JS_ITERATOR_HELPER_KIND_TAKE,
} JSIteratorHelperKindEnum;

typedef struct JSForInIterator {
    JSValue obj;
    bool is_array;
    uint32_t array_length;
    uint32_t idx;
} JSForInIterator;

typedef struct JSRegExp {
    JSString *pattern;
    JSString *bytecode; /* also contains the flags */
} JSRegExp;

typedef struct JSProxyData {
    JSValue target;
    JSValue handler;
    uint8_t is_func;
    uint8_t is_revoked;
} JSProxyData;

typedef struct JSArrayBuffer {
    int byte_length; /* 0 if detached */
    int max_byte_length; /* -1 if not resizable; >= byte_length otherwise */
    uint8_t detached;
    uint8_t immutable;
    uint8_t shared; /* if shared, the array buffer cannot be detached */
    uint8_t *data; /* NULL if detached */
    struct list_head array_list;
    void *opaque;
    JSFreeArrayBufferDataFunc *free_func;
} JSArrayBuffer;

typedef struct JSTypedArray {
    struct list_head link; /* link to arraybuffer */
    JSObject *obj; /* back pointer to the TypedArray/DataView object */
    JSObject *buffer; /* based array buffer */
    uint32_t offset; /* byte offset in the array buffer */
    uint32_t length; /* byte length in the array buffer */
    bool track_rab; /* auto-track length of backing array buffer */
} JSTypedArray;

typedef struct JSAsyncFunctionState {
    JSValue this_val; /* 'this' generator argument */
    int argc; /* number of function arguments */
    bool throw_flag; /* used to throw an exception in JS_CallInternal() */
    JSStackFrame frame;
} JSAsyncFunctionState;

/* XXX: could use an object instead to avoid the
   JS_TAG_ASYNC_FUNCTION tag for the GC */
typedef struct JSAsyncFunctionData {
    JSGCObjectHeader header; /* must come first */
    JSValue resolving_funcs[2];
    bool is_active; /* true if the async function state is valid */
    JSAsyncFunctionState func_state;
} JSAsyncFunctionData;

typedef struct JSReqModuleEntry {
    JSAtom module_name;
    JSModuleDef *module; /* used using resolution */
    JSValue attributes; /* JS_UNDEFINED or an object containing the attributes as key/value */
} JSReqModuleEntry;

typedef enum JSExportTypeEnum {
    JS_EXPORT_TYPE_LOCAL,
    JS_EXPORT_TYPE_INDIRECT,
} JSExportTypeEnum;

typedef struct JSExportEntry {
    union {
        struct {
            int var_idx; /* closure variable index */
            JSVarRef *var_ref; /* if != NULL, reference to the variable */
        } local; /* for local export */
        int req_module_idx; /* module for indirect export */
    } u;
    JSExportTypeEnum export_type;
    JSAtom local_name; /* '*' if export ns from. not used for local
                          export after compilation */
    JSAtom export_name; /* exported variable name */
} JSExportEntry;

typedef struct JSStarExportEntry {
    int req_module_idx; /* in req_module_entries */
} JSStarExportEntry;

typedef struct JSImportEntry {
    int var_idx; /* closure variable index */
    JSAtom import_name;
    int req_module_idx; /* in req_module_entries */
} JSImportEntry;

typedef enum {
    JS_MODULE_STATUS_UNLINKED,
    JS_MODULE_STATUS_LINKING,
    JS_MODULE_STATUS_LINKED,
    JS_MODULE_STATUS_EVALUATING,
    JS_MODULE_STATUS_EVALUATING_ASYNC,
    JS_MODULE_STATUS_EVALUATED,
} JSModuleStatus;

struct JSModuleDef {
    JSAtom module_name;
    struct list_head link;

    JSReqModuleEntry *req_module_entries;
    int req_module_entries_count;
    int req_module_entries_size;

    JSExportEntry *export_entries;
    int export_entries_count;
    int export_entries_size;

    JSStarExportEntry *star_export_entries;
    int star_export_entries_count;
    int star_export_entries_size;

    JSImportEntry *import_entries;
    int import_entries_count;
    int import_entries_size;

    JSValue module_ns;
    JSValue func_obj; /* only used for JS modules */
    JSModuleInitFunc *init_func; /* only used for C modules */
    bool has_tla; /* true if func_obj contains await */
    bool resolved;
    bool func_created;
    JSModuleStatus status : 8;
    /* temp use during js_module_link() & js_module_evaluate() */
    int dfs_index, dfs_ancestor_index;
    JSModuleDef *stack_prev;
    /* temp use during js_module_evaluate() */
    JSModuleDef **async_parent_modules;
    int async_parent_modules_count;
    int async_parent_modules_size;
    int pending_async_dependencies;
    bool async_evaluation;
    int64_t async_evaluation_timestamp;
    JSModuleDef *cycle_root;
    JSValue promise; /* corresponds to spec field: capability */
    JSValue resolving_funcs[2]; /* corresponds to spec field: capability */
    /* true if evaluation yielded an exception. It is saved in
       eval_exception */
    bool eval_has_exception;
    JSValue eval_exception;
    JSValue meta_obj; /* for import.meta */
    JSValue private_value; /* private value for C modules */
};

typedef struct JSJobEntry {
    struct list_head link;
    JSContext *ctx;
    JSJobFunc *job_func;
    int argc;
    JSValue argv[];
} JSJobEntry;

typedef struct JSProperty {
    union {
        JSValue value;      /* JS_PROP_NORMAL */
        struct {            /* JS_PROP_GETSET */
            JSObject *getter; /* NULL if undefined */
            JSObject *setter; /* NULL if undefined */
        } getset;
        JSVarRef *var_ref;  /* JS_PROP_VARREF */
        struct {            /* JS_PROP_AUTOINIT */
            /* in order to use only 2 pointers, we compress the realm
               and the init function pointer */
            uintptr_t realm_and_id; /* realm and init_id (JS_AUTOINIT_ID_x)
                                       in the 2 low bits */
            void *opaque;
        } init;
    } u;
} JSProperty;

#define JS_PROP_INITIAL_SIZE 2
#define JS_PROP_INITIAL_HASH_SIZE 4 /* must be a power of two */

typedef struct JSShapeProperty {
    uint32_t hash_next : 26; /* 0 if last in list */
    uint32_t flags : 6;   /* JS_PROP_XXX */
    JSAtom atom; /* JS_ATOM_NULL = free property entry */
} JSShapeProperty;

struct JSShape {
    /* hash table of size hash_mask + 1 before the start of the
       structure (see prop_hash_end()). */
    JSGCObjectHeader header;
    /* true if the shape is inserted in the shape hash table. If not,
       JSShape.hash is not valid */
    uint8_t is_hashed;
    uint32_t hash; /* current hash value */
    uint32_t prop_hash_mask;
    int prop_size; /* allocated properties */
    int prop_count; /* include deleted properties */
    int deleted_prop_count;
    JSShape *shape_hash_next; /* in JSRuntime.shape_hash[h] list */
    JSObject *proto;
    uint32_t hash_table[]; /* prop_hash_mask + 1 elements, then prop[prop_size] */
};

struct JSObject {
    /* ref_count/gc_obj_type/mark live in the allocator block header; the object
       body keeps only the GC list link plus the object's own flags. */
    JSGCObjectHeader header; /* {link}; must come first so &p->header == p */
    uint8_t is_prototype : 1; /* object may be used as prototype */
    uint8_t extensible : 1;
    uint8_t free_mark : 1; /* only used when freeing objects with cycles */
    uint8_t is_exotic : 1; /* true if object has exotic property handlers */
    uint8_t fast_array : 1; /* true if u.array is used for get/put (for JS_CLASS_ARRAY, JS_CLASS_ARGUMENTS, JS_CLASS_MAPPED_ARGUMENTS and typed arrays) */
    uint8_t is_constructor : 1; /* true if object is a constructor function */
    uint8_t is_uncatchable_error : 1; /* if true, error is not catchable */
    uint8_t tmp_mark : 1; /* used in JS_WriteObjectRec() */
    uint8_t is_HTMLDDA : 1; /* specific annex B IsHtmlDDA behavior */
    uint16_t class_id; /* see JS_CLASS_x */
    /* byte offsets: 16/24 */
    JSShape *shape; /* prototype and property names + flag */
    JSProperty *prop; /* array of properties */
    /* byte offsets: 24/40 */
    JSWeakRefRecord *first_weak_ref;
    /* byte offsets: 28/48 */
    union {
        void *opaque;
        struct JSBoundFunction *bound_function; /* JS_CLASS_BOUND_FUNCTION */
        struct JSCFunctionDataRecord *c_function_data_record; /* JS_CLASS_C_FUNCTION_DATA */
        struct JSCClosureRecord *c_closure_record; /* JS_CLASS_C_CLOSURE */
        struct JSForInIterator *for_in_iterator; /* JS_CLASS_FOR_IN_ITERATOR */
        struct JSArrayBuffer *array_buffer; /* JS_CLASS_ARRAY_BUFFER, JS_CLASS_SHARED_ARRAY_BUFFER */
        struct JSTypedArray *typed_array; /* JS_CLASS_UINT8C_ARRAY..JS_CLASS_DATAVIEW */
        struct JSMapState *map_state;   /* JS_CLASS_MAP..JS_CLASS_WEAKSET */
        struct JSMapIteratorData *map_iterator_data; /* JS_CLASS_MAP_ITERATOR, JS_CLASS_SET_ITERATOR */
        struct JSArrayIteratorData *array_iterator_data; /* JS_CLASS_ARRAY_ITERATOR, JS_CLASS_STRING_ITERATOR */
        struct JSRegExpStringIteratorData *regexp_string_iterator_data; /* JS_CLASS_REGEXP_STRING_ITERATOR */
        struct JSGeneratorData *generator_data; /* JS_CLASS_GENERATOR */
        struct JSIteratorConcatData *iterator_concat_data; /* JS_CLASS_ITERATOR_CONCAT */
        struct JSIteratorHelperData *iterator_helper_data; /* JS_CLASS_ITERATOR_HELPER */
        struct JSIteratorWrapData *iterator_wrap_data; /* JS_CLASS_ITERATOR_WRAP */
        struct JSProxyData *proxy_data; /* JS_CLASS_PROXY */
        struct JSPromiseData *promise_data; /* JS_CLASS_PROMISE */
        struct JSPromiseFunctionData *promise_function_data; /* JS_CLASS_PROMISE_RESOLVE_FUNCTION, JS_CLASS_PROMISE_REJECT_FUNCTION */
        struct JSAsyncFunctionData *async_function_data; /* JS_CLASS_ASYNC_FUNCTION_RESOLVE, JS_CLASS_ASYNC_FUNCTION_REJECT */
        struct JSAsyncFromSyncIteratorData *async_from_sync_iterator_data; /* JS_CLASS_ASYNC_FROM_SYNC_ITERATOR */
        struct JSAsyncGeneratorData *async_generator_data; /* JS_CLASS_ASYNC_GENERATOR */
        struct { /* JS_CLASS_BYTECODE_FUNCTION: 12/24 bytes */
            /* also used by JS_CLASS_GENERATOR_FUNCTION, JS_CLASS_ASYNC_FUNCTION and JS_CLASS_ASYNC_GENERATOR_FUNCTION */
            struct JSFunctionBytecode *function_bytecode;
            JSVarRef **var_refs;
            JSObject *home_object; /* for 'super' access */
        } func;
        struct { /* JS_CLASS_C_FUNCTION: 12/20 bytes */
            JSContext *realm;
            JSCFunctionType c_function;
            uint8_t length;
            uint8_t cproto;
            int16_t magic;
        } cfunc;
        /* array part for fast arrays and typed arrays */
        struct { /* JS_CLASS_ARRAY, JS_CLASS_ARGUMENTS, JS_CLASS_MAPPED_ARGUMENTS, JS_CLASS_UINT8C_ARRAY..JS_CLASS_FLOAT64_ARRAY */
            union {
                uint32_t size;          /* JS_CLASS_ARRAY */
                struct JSTypedArray *typed_array; /* JS_CLASS_UINT8C_ARRAY..JS_CLASS_FLOAT64_ARRAY */
            } u1;
            union {
                JSValue *values;        /* JS_CLASS_ARRAY, JS_CLASS_ARGUMENTS */
                JSVarRef **var_refs;    /* JS_CLASS_MAPPED_ARGUMENTS */
                void *ptr;              /* JS_CLASS_UINT8C_ARRAY..JS_CLASS_FLOAT64_ARRAY */
                int8_t *int8_ptr;       /* JS_CLASS_INT8_ARRAY */
                uint8_t *uint8_ptr;     /* JS_CLASS_UINT8_ARRAY, JS_CLASS_UINT8C_ARRAY */
                int16_t *int16_ptr;     /* JS_CLASS_INT16_ARRAY */
                uint16_t *uint16_ptr;   /* JS_CLASS_UINT16_ARRAY */
                int32_t *int32_ptr;     /* JS_CLASS_INT32_ARRAY */
                uint32_t *uint32_ptr;   /* JS_CLASS_UINT32_ARRAY */
                int64_t *int64_ptr;     /* JS_CLASS_INT64_ARRAY */
                uint64_t *uint64_ptr;   /* JS_CLASS_UINT64_ARRAY */
                uint16_t *fp16_ptr;     /* JS_CLASS_FLOAT16_ARRAY */
                float *float_ptr;       /* JS_CLASS_FLOAT32_ARRAY */
                double *double_ptr;     /* JS_CLASS_FLOAT64_ARRAY */
            } u;
            uint32_t count; /* <= 2^31-1. 0 for a detached typed array */
        } array;    /* 12/20 bytes */
        JSRegExp regexp;    /* JS_CLASS_REGEXP: 8/16 bytes */
        JSValue object_data;    /* for JS_SetObjectData(): 8/16/16 bytes */
    } u;
    /* byte sizes: 40/48/72 */
};

typedef struct JSCallSiteData {
    JSValue filename;
    JSValue func;
    JSValue func_name;
    bool native;
    int line_num;
    int col_num;
} JSCallSiteData;

enum {
    __JS_ATOM_NULL = JS_ATOM_NULL,
#define DEF(name, str) JS_ATOM_ ## name,
#include "atom_defs.h"
#undef DEF
    JS_ATOM_END,
};
#define JS_ATOM_LAST_KEYWORD JS_ATOM_using
#define JS_ATOM_LAST_STRICT_KEYWORD JS_ATOM_yield

static const char js_atom_init[] =
#define DEF(name, str) str "\0"
#include "atom_defs.h"
#undef DEF
;

typedef enum OPCodeFormat {
#define FMT(f) OP_FMT_ ## f,
#define DEF(id, size, n_pop, n_push, f)
#include "bytecode_opcodes.h"
#undef DEF
#undef FMT
} OPCodeFormat;

typedef enum OPCodeEnum {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) OP_ ## id,
#define def(id, size, n_pop, n_push, f)
#include "bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
    OP_COUNT, /* excluding temporary opcodes */
    /* temporary opcodes : overlap with the short opcodes */
    OP_TEMP_START = OP_nop + 1,
    OP___dummy = OP_TEMP_START - 1,
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f)
#define def(id, size, n_pop, n_push, f) OP_ ## id,
#include "bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
    OP_TEMP_END,
} OPCodeEnum;


#endif
