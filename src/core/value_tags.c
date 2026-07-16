/* Engine domain source: core/engine_core.inc -> value_tags.
 * Ownership: core subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

/*
 * TurboJS JavaScript Engine
 *
 * Copyright (c) 2017-2026 Fabrice Bellard
 * Copyright (c) 2017-2025 Charlie Gordon
 * Copyright (c) 2023-2026 Ben Noordhuis
 * Copyright (c) 2023-2026 Saúl Ibarra Corretgé
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "internal/platform.h"
#include "internal/foundation_types.h"

static int JS_InitAtoms(JSRuntime *rt);
static JSAtom __JS_NewAtomInit(JSRuntime *rt, const char *str, int len,
                               int atom_type);
static void JS_FreeAtomStruct(JSRuntime *rt, JSAtomStruct *p);
static void free_function_bytecode(JSRuntime *rt, JSFunctionBytecode *b);
static JSValue js_call_c_function(JSContext *ctx, JSValueConst func_obj,
                                  JSValueConst this_obj,
                                  int argc, JSValueConst *argv, int flags);
static JSValue js_call_bound_function(JSContext *ctx, JSValueConst func_obj,
                                      JSValueConst this_obj,
                                      int argc, JSValueConst *argv, int flags);
static JSValue JS_CallInternal(JSContext *ctx, JSValueConst func_obj,
                               JSValueConst this_obj, JSValueConst new_target,
                               int argc, JSValueConst *argv, int flags);
static JSValue JS_CallConstructorInternal(JSContext *ctx,
                                          JSValueConst func_obj,
                                          JSValueConst new_target,
                                          int argc, JSValueConst *argv, int flags);
static JSValue JS_CallFree(JSContext *ctx, JSValue func_obj, JSValueConst this_obj,
                           int argc, JSValueConst *argv);
static JSValue JS_InvokeFree(JSContext *ctx, JSValue this_val, JSAtom atom,
                             int argc, JSValueConst *argv);
static __exception int JS_ToArrayLengthFree(JSContext *ctx, uint32_t *plen,
                                            JSValue val, bool is_array_ctor);
static JSValue JS_EvalObject(JSContext *ctx, JSValueConst this_obj,
                             JSValueConst val, int flags, int scope_idx);
static JSValue js_new_suppressed_error(JSContext *ctx, JSValueConst error,
                                       JSValueConst suppressed);
static __maybe_unused void JS_DumpString(JSRuntime *rt, JSString *p);
static __maybe_unused void JS_DumpObjectHeader(JSRuntime *rt);
static __maybe_unused void JS_DumpObject(JSRuntime *rt, JSObject *p);
static __maybe_unused void JS_DumpGCObject(JSRuntime *rt, JSGCObjectHeader *p);
static __maybe_unused void JS_DumpValue(JSRuntime *rt, JSValueConst val);
static __maybe_unused void JS_DumpAtoms(JSRuntime *rt);
static __maybe_unused void JS_DumpShapes(JSRuntime *rt);

static JSValue js_function_apply(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv, int magic);
static void js_array_finalizer(JSRuntime *rt, JSValueConst val);
static void js_array_mark(JSRuntime *rt, JSValueConst val,
                          JS_MarkFunc *mark_func);
static void js_mapped_arguments_finalizer(JSRuntime *rt, JSValueConst val);
static void js_mapped_arguments_mark(JSRuntime *rt, JSValueConst val,
                                     JS_MarkFunc *mark_func);
static void js_object_data_finalizer(JSRuntime *rt, JSValueConst val);
static void js_object_data_mark(JSRuntime *rt, JSValueConst val,
                                JS_MarkFunc *mark_func);
static void js_c_function_finalizer(JSRuntime *rt, JSValueConst val);
static void js_c_function_mark(JSRuntime *rt, JSValueConst val,
                               JS_MarkFunc *mark_func);
static void js_bytecode_function_finalizer(JSRuntime *rt, JSValueConst val);
static void js_bytecode_function_mark(JSRuntime *rt, JSValueConst val,
                                JS_MarkFunc *mark_func);
static void js_bound_function_finalizer(JSRuntime *rt, JSValueConst val);
static void js_bound_function_mark(JSRuntime *rt, JSValueConst val,
                                JS_MarkFunc *mark_func);
static void js_for_in_iterator_finalizer(JSRuntime *rt, JSValueConst val);
static void js_for_in_iterator_mark(JSRuntime *rt, JSValueConst val,
                                JS_MarkFunc *mark_func);
static void js_regexp_finalizer(JSRuntime *rt, JSValueConst val);
static void js_array_buffer_finalizer(JSRuntime *rt, JSValueConst val);
static void js_typed_array_finalizer(JSRuntime *rt, JSValueConst val);
static void js_typed_array_mark(JSRuntime *rt, JSValueConst val,
                                JS_MarkFunc *mark_func);
static void js_proxy_finalizer(JSRuntime *rt, JSValueConst val);
static void js_proxy_mark(JSRuntime *rt, JSValueConst val,
                                JS_MarkFunc *mark_func);
static void js_map_finalizer(JSRuntime *rt, JSValueConst val);
static void js_map_mark(JSRuntime *rt, JSValueConst val,
                                JS_MarkFunc *mark_func);
static void js_map_iterator_finalizer(JSRuntime *rt, JSValueConst val);
static void js_map_iterator_mark(JSRuntime *rt, JSValueConst val,
                                JS_MarkFunc *mark_func);
static void js_array_iterator_finalizer(JSRuntime *rt, JSValueConst val);
static void js_array_iterator_mark(JSRuntime *rt, JSValueConst val,
                                JS_MarkFunc *mark_func);
static void js_iterator_concat_finalizer(JSRuntime *rt, JSValueConst val);
static void js_iterator_concat_mark(JSRuntime *rt, JSValueConst val,
                                    JS_MarkFunc *mark_func);
static void js_iterator_helper_finalizer(JSRuntime *rt, JSValueConst val);
static void js_iterator_helper_mark(JSRuntime *rt, JSValueConst val,
                                    JS_MarkFunc *mark_func);
static void js_iterator_wrap_finalizer(JSRuntime *rt, JSValueConst val);
static void js_iterator_wrap_mark(JSRuntime *rt, JSValueConst val,
                                  JS_MarkFunc *mark_func);
static void js_regexp_string_iterator_finalizer(JSRuntime *rt,
                                                JSValueConst val);
static void js_regexp_string_iterator_mark(JSRuntime *rt, JSValueConst val,
                                JS_MarkFunc *mark_func);
static void js_generator_finalizer(JSRuntime *rt, JSValueConst val);
static void js_generator_mark(JSRuntime *rt, JSValueConst val,
                                JS_MarkFunc *mark_func);
static void js_promise_finalizer(JSRuntime *rt, JSValueConst val);
static void js_promise_mark(JSRuntime *rt, JSValueConst val,
                                JS_MarkFunc *mark_func);
static void js_promise_resolve_function_finalizer(JSRuntime *rt, JSValueConst val);
static void js_promise_resolve_function_mark(JSRuntime *rt, JSValueConst val,
                                JS_MarkFunc *mark_func);
static void js_disposable_stack_finalizer(JSRuntime *rt, JSValueConst val);
static void js_disposable_stack_mark(JSRuntime *rt, JSValueConst val,
                                     JS_MarkFunc *mark_func);

#define HINT_STRING  0
#define HINT_NUMBER  1
#define HINT_NONE    2
#define HINT_FORCE_ORDINARY (1 << 4) // don't try Symbol.toPrimitive
static JSValue JS_ToPrimitiveFree(JSContext *ctx, JSValue val, int hint);
static JSValue JS_ToStringFree(JSContext *ctx, JSValue val);
static int JS_ToBoolFree(JSContext *ctx, JSValue val);
static int JS_ToInt32Free(JSContext *ctx, int32_t *pres, JSValue val);
static int JS_ToFloat64Free(JSContext *ctx, double *pres, JSValue val);
static int JS_ToUint8ClampFree(JSContext *ctx, int32_t *pres, JSValue val);
static JSValue JS_ToPropertyKeyInternal(JSContext *ctx, JSValueConst val,
                                        int flags);
static JSValue js_new_string8_len(JSContext *ctx, const char *buf, int len);
static JSValue js_compile_regexp(JSContext *ctx, JSValueConst pattern,
                                 JSValueConst flags);
static JSValue js_regexp_constructor_internal(JSContext *ctx, JSValueConst ctor,
                                              JSValue pattern, JSValue bc);
static void gc_decref(JSRuntime *rt);
static int JS_NewClass1(JSRuntime *rt, JSClassID class_id,
                        const JSClassDef *class_def, JSAtom name);
static JSValue js_array_push(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv, int unshift);
static JSValue js_array_constructor(JSContext *ctx, JSValueConst new_target,
                                    int argc, JSValueConst *argv);
static JSValue js_error_constructor(JSContext *ctx, JSValueConst new_target,
                                    int argc, JSValueConst *argv, int magic);
static JSValue js_object_defineProperty(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv, int magic);

typedef enum JSStrictEqModeEnum {
    JS_EQ_STRICT,
    JS_EQ_SAME_VALUE,
    JS_EQ_SAME_VALUE_ZERO,
} JSStrictEqModeEnum;

static bool js_strict_eq2(JSContext *ctx, JSValueConst op1, JSValueConst op2,
                          JSStrictEqModeEnum eq_mode);
static bool js_strict_eq(JSContext *ctx, JSValueConst op1, JSValueConst op2);
static bool js_same_value(JSContext *ctx, JSValueConst op1, JSValueConst op2);
static bool js_same_value_zero(JSContext *ctx, JSValueConst op1, JSValueConst op2);
static JSValue JS_ToObjectFree(JSContext *ctx, JSValue val);
static JSProperty *add_property(JSContext *ctx,
                                JSObject *p, JSAtom prop, int prop_flags);
static void free_property(JSRuntime *rt, JSProperty *pr, int prop_flags);
static int JS_ToBigInt64Free(JSContext *ctx, int64_t *pres, JSValue val);
static JSValue JS_ThrowStackOverflow(JSContext *ctx);
static JSValue JS_ThrowTypeErrorRevokedProxy(JSContext *ctx);
static JSValue js_proxy_getPrototypeOf(JSContext *ctx, JSValueConst obj);
static int js_proxy_setPrototypeOf(JSContext *ctx, JSValueConst obj,
                                   JSValueConst proto_val, bool throw_flag);
static int js_proxy_isExtensible(JSContext *ctx, JSValueConst obj);
static int js_proxy_preventExtensions(JSContext *ctx, JSValueConst obj);
static int js_proxy_isArray(JSContext *ctx, JSValueConst obj);
static int JS_CreateProperty(JSContext *ctx, JSObject *p,
                             JSAtom prop, JSValueConst val,
                             JSValueConst getter, JSValueConst setter,
                             int flags);
static int js_string_memcmp(JSString *p1, JSString *p2, int len);
static void reset_weak_ref(JSRuntime *rt, JSWeakRefRecord **first_weak_ref);
static bool is_valid_weakref_target(JSValueConst val);
static void insert_weakref_record(JSValueConst target,
                                  struct JSWeakRefRecord *wr);
static JSValue js_array_buffer_constructor3(JSContext *ctx,
                                            JSValueConst new_target,
                                            uint64_t len, uint64_t *max_len,
                                            JSClassID class_id,
                                            uint8_t *buf,
                                            JSFreeArrayBufferDataFunc *free_func,
                                            void *opaque, bool alloc_flag);
static void js_array_buffer_free(JSRuntime *rt, void *opaque, void *ptr);
static JSArrayBuffer *js_get_array_buffer(JSContext *ctx, JSValueConst obj);
static bool array_buffer_is_resizable(const JSArrayBuffer *abuf);
static JSValue js_typed_array_constructor(JSContext *ctx,
                                          JSValueConst this_val,
                                          int argc, JSValueConst *argv,
                                          int classid);
static JSValue js_typed_array_constructor_ta(JSContext *ctx,
                                             JSValueConst new_target,
                                             JSValueConst src_obj,
                                             int classid, uint32_t len);
static bool is_typed_array(JSClassID class_id);
static bool typed_array_is_immutable(JSObject *p);
static bool typed_array_is_oob(JSObject *p);
static uint32_t typed_array_length(JSObject *p);
static int typed_array_init(JSContext *ctx, JSValue obj, JSValue buffer,
                            uint64_t offset, uint64_t len, bool track_rab);
static JSValue JS_ThrowTypeErrorDetachedArrayBuffer(JSContext *ctx);
static JSValue JS_ThrowTypeErrorImmutableArrayBuffer(JSContext *ctx);
static JSValue JS_ThrowTypeErrorArrayBufferOOB(JSContext *ctx);
static JSVarRef *get_var_ref(JSContext *ctx, JSStackFrame *sf, int var_idx,
                             bool is_arg);
static JSVarRef *js_create_var_ref(JSContext *ctx, bool is_gc_object);
static JSValue js_call_generator_function(JSContext *ctx, JSValueConst func_obj,
                                          JSValueConst this_obj,
                                          int argc, JSValueConst *argv,
                                          int flags);
static void js_async_function_resolve_finalizer(JSRuntime *rt,
                                                JSValueConst val);
static void js_async_function_resolve_mark(JSRuntime *rt, JSValueConst val,
                                           JS_MarkFunc *mark_func);
static JSValue JS_EvalInternal(JSContext *ctx, JSValueConst this_obj,
                               const char *input, size_t input_len,
                               const char *filename, int line, int flags, int scope_idx);
static void js_free_module_def(JSContext *ctx, JSModuleDef *m);
static void js_mark_module_def(JSRuntime *rt, JSModuleDef *m,
                               JS_MarkFunc *mark_func);
static JSValue js_import_meta(JSContext *ctx);
static JSValue js_dynamic_import(JSContext *ctx, JSValueConst specifier,
                                 JSValueConst options);
static void free_var_ref(JSRuntime *rt, JSVarRef *var_ref);
static JSValue js_new_promise_capability(JSContext *ctx,
                                         JSValue *resolving_funcs,
                                         JSValueConst ctor);
static __exception int perform_promise_then(JSContext *ctx,
                                            JSValueConst promise,
                                            JSValueConst *resolve_reject,
                                            JSValueConst *cap_resolving_funcs);
static JSValue js_promise_resolve(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv, int magic);
static JSValue js_promise_then(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv);
static JSValue js_promise_resolve_thenable_job(JSContext *ctx,
                                               int argc, JSValueConst *argv);
static bool js_string_eq(JSString *p1, JSString *p2);
static int js_string_compare(JSString *p1, JSString *p2);
static int JS_SetPropertyValue(JSContext *ctx, JSValueConst this_obj,
                               JSValue prop, JSValue val, int flags);
static int JS_NumberIsInteger(JSContext *ctx, JSValueConst val);
static bool JS_NumberIsNegativeOrMinusZero(JSContext *ctx, JSValueConst val);
static JSValue JS_ToNumberFree(JSContext *ctx, JSValue val);
static int JS_GetOwnPropertyInternal(JSContext *ctx, JSPropertyDescriptor *desc,
                                     JSObject *p, JSAtom prop);
static int JS_GetOwnPropertyFlagsInternal(JSContext *ctx, int *pflags,
                                          JSObject *p, JSAtom prop);
static JSValue JS_GetOwnPropertyNames2(JSContext *ctx, JSValueConst obj1,
                                       int flags, int kind);
static void js_free_desc(JSContext *ctx, JSPropertyDescriptor *desc);
static void async_func_mark(JSRuntime *rt, JSAsyncFunctionState *s,
                            JS_MarkFunc *mark_func);
static int JS_AddIntrinsicBasicObjects(JSContext *ctx);
static void js_free_shape(JSRuntime *rt, JSShape *sh);
static void js_free_shape_null(JSRuntime *rt, JSShape *sh);
static int js_shape_prepare_update(JSContext *ctx, JSObject *p,
                                   JSShapeProperty **pprs);
static int init_shape_hash(JSRuntime *rt);
static __exception int js_get_length32(JSContext *ctx, uint32_t *pres,
                                       JSValueConst obj);
static __exception int js_get_length64(JSContext *ctx, int64_t *pres,
                                       JSValueConst obj);
static __exception int js_set_length64(JSContext *ctx, JSValueConst obj,
                                       int64_t len);
static void free_arg_list(JSContext *ctx, JSValue *tab, uint32_t len);
static JSValue *build_arg_list(JSContext *ctx, uint32_t *plen,
                               JSValueConst array_arg);
static JSValue js_create_array(JSContext *ctx, int len, JSValueConst *tab);
static bool js_get_fast_array(JSContext *ctx, JSValue obj,
                              JSValue **arrpp, uint32_t *countp);
static int expand_fast_array(JSContext *ctx, JSObject *p, uint32_t new_len);
static JSValue JS_CreateAsyncFromSyncIterator(JSContext *ctx,
                                              JSValue sync_iter);
static void js_c_function_data_finalizer(JSRuntime *rt, JSValueConst val);
static void js_c_function_data_mark(JSRuntime *rt, JSValueConst val,
                                    JS_MarkFunc *mark_func);
static JSValue js_call_c_function_data(JSContext *ctx, JSValueConst func_obj,
                                       JSValueConst this_val,
                                       int argc, JSValueConst *argv, int flags);
static void js_c_closure_finalizer(JSRuntime *rt, JSValueConst val);
static JSValue js_call_c_closure(JSContext *ctx, JSValueConst func_obj,
                                 JSValueConst this_val,
                                 int argc, JSValueConst *argv, int flags);
static JSAtom JS_ValueToAtomInternal(JSContext *ctx, JSValueConst val,
                                     int flags);
static JSAtom js_symbol_to_atom(JSContext *ctx, JSValueConst val);
static void add_gc_object(JSRuntime *rt, JSGCObjectHeader *h,
                          JSGCObjectTypeEnum type);
static void remove_gc_object(JSGCObjectHeader *h);
static void js_async_function_free0(JSRuntime *rt, JSAsyncFunctionData *s);
static JSValue js_instantiate_prototype(JSContext *ctx, JSObject *p, JSAtom atom, void *opaque);
static JSValue js_module_ns_autoinit(JSContext *ctx, JSObject *p, JSAtom atom,
                                 void *opaque);
static JSValue JS_InstantiateFunctionListItem2(JSContext *ctx, JSObject *p,
                                               JSAtom atom, void *opaque);
static JSValue JS_NewObjectProtoList(JSContext *ctx, JSValueConst proto,
                                     const JSCFunctionListEntry *fields, int n_fields);

static void js_set_uncatchable_error(JSContext *ctx, JSValueConst val,
                                     bool flag);

static JSValue js_new_callsite(JSContext *ctx, JSCallSiteData *csd);
static void js_new_callsite_data(JSContext *ctx, JSCallSiteData *csd, JSStackFrame *sf);
static void js_new_callsite_data2(JSContext *ctx, JSCallSiteData *csd, const char *filename, int line_num, int col_num);
static int _JS_AddIntrinsicCallSite(JSContext *ctx);

static void JS_SetOpaqueInternal(JSValueConst obj, void *opaque);

static const JSClassExoticMethods js_arguments_exotic_methods;
static const JSClassExoticMethods js_string_exotic_methods;
static const JSClassExoticMethods js_proxy_exotic_methods;
static const JSClassExoticMethods js_module_ns_exotic_methods;

static inline bool double_is_int32(double d)
{
    uint64_t u, e;
    JSFloat64Union t;

    t.d = d;
    u = t.u64;

    e = ((u >> 52) & 0x7FF) - 1023;
    if (e > 30) {
        // accept 0, INT32_MIN, reject too large, too small, nan, inf, -0
        return !u || (u == 0xc1e0000000000000);
    } else {
        // shift out sign, exponent and whole part bits
        // value is fractional if remaining low bits are non-zero
        return !(u << 12 << e);
    }
}

static JSValue js_float64(double d)
{
    return __JS_NewFloat64(d);
}

static int compare_u32(uint32_t a, uint32_t b)
{
    return -(a < b) + (b < a); // -1, 0 or 1
}

static JSValue js_int32(int32_t v)
{
    return JS_MKVAL(JS_TAG_INT, v);
}

static JSValue js_uint32(uint32_t v)
{
    if (v <= INT32_MAX)
        return js_int32(v);
    else
        return js_float64(v);
}

static JSValue js_int64(int64_t v)
{
    if (v >= INT32_MIN && v <= INT32_MAX)
        return js_int32(v);
    else
        return js_float64(v);
}

static JSValue js_number(double d)
{
    if (double_is_int32(d))
        return js_int32((int32_t)d);
    else
        return js_float64(d);
}

/* If v is a number (int or float64), store it as a double and return true.
   Used by the interpreter arithmetic fast paths to handle mixed int/float
   operands inline instead of falling back to the slow path. */
static inline bool js_arith_to_float64(JSValue v, double *pd)
{
    uint32_t tag = JS_VALUE_GET_TAG(v);
    if (JS_TAG_IS_FLOAT64(tag))
        *pd = JS_VALUE_GET_FLOAT64(v);
    else if (tag == JS_TAG_INT)
        *pd = JS_VALUE_GET_INT(v);
    else
        return false;
    return true;
}

static JSValue __JS_NewShortBigInt(JSContext *ctx, int32_t d)
{
    (void)&ctx;
    return JS_MKVAL(JS_TAG_SHORT_BIG_INT, d);
}

JSValue JS_NewNumber(JSContext *ctx, double d)
{
    return js_number(d);
}

static JSValue js_bool(bool v)
{
    return JS_MKVAL(JS_TAG_BOOL, (v != 0));
}

static JSValue js_dup(JSValueConst v)
{
    if (JS_VALUE_HAS_REF_COUNT(v)) {
        void *p = JS_VALUE_GET_PTR(v);
        JS_REF_COUNT(p)++;
    }
    return unsafe_unconst(v);
}

JSValue JS_DupValue(JSContext *ctx, JSValueConst v)
{
    return js_dup(v);
}

JSValue JS_DupValueRT(JSRuntime *rt, JSValueConst v)
{
    return js_dup(v);
}

static void js_trigger_gc(JSRuntime *rt, size_t size)
{
    bool force_gc;
#ifdef FORCE_GC_AT_MALLOC
    force_gc = true;
#else
    force_gc = ((rt->malloc_state.malloc_size + size) >
                rt->malloc_gc_threshold);
#endif
    if (force_gc) {
#ifdef ENABLE_DUMPS // JS_DUMP_GC
        if (check_dump_flag(rt, JS_DUMP_GC)) {
            printf("GC: size=%zd\n", rt->malloc_state.malloc_size);
        }
#endif
        JS_RunGC(rt);
        rt->malloc_gc_threshold = rt->malloc_state.malloc_size +
            (rt->malloc_state.malloc_size >> 1);
    }
}

static size_t js_malloc_usable_size_unknown(const void *ptr)
{
    return 0;
}

/* max overhead for size >= 64: 12.5% */
static const uint16_t arena_block_sizes[JS_ARENA_BLOCK_SIZE_COUNT] = {
    16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 128,
    144, 160, 176, 192, 208, 224, 240, 256,
    288, 320, 352, 384, 416, 448, 480, 512,
};

static int arena_get_size_index(size_t size)
{
    if (size <= 16)
        return 0;
    else if (size <= 128)
        return (size + 7) / 8 - 2;
    else if (size <= 256)
        return (size + 15) / 16 + 6;
    else if (size <= 512)
        return (size + 31) / 32 + 14;
    else
        return JS_ARENA_BLOCK_SIZE_COUNT;
}

static inline JSMallocBlockHeader *arena_zero_block(JSRuntime *rt)
{
    return (JSMallocBlockHeader *)rt->arena_state.zero_size_block;
}

static void js_arena_init(JSRuntime *rt)
{
    JSArenaState *s = &rt->arena_state;
    int i;
    arena_zero_block(rt)->u.block_idx = JS_ARENA_FREE_NIL;
    for (i = 0; i < JS_ARENA_BLOCK_SIZE_COUNT; i++) {
        init_list_head(&s->arena_list[i]);
        init_list_head(&s->free_arena_list[i]);
    }
}

static inline void *arena_get_block(JSArena *ar, unsigned int idx,
                                    unsigned int block_size)
{
    return ar->blocks + (size_t)idx * block_size;
}

static no_inline JSArena *arena_new(JSRuntime *rt, int block_size_idx)
{
    JSMallocBlockHeader *b;
    JSArena *ar;
    int n_blocks, block_size, i;

    block_size = arena_block_sizes[block_size_idx];
    n_blocks = (JS_ARENA_SIZE - sizeof(JSArena)) / block_size;
    ar = rt->mf.js_malloc(rt->malloc_state.opaque,
                          sizeof(JSArena) + (size_t)n_blocks * block_size);
    if (!ar)
        return NULL;
    ar->block_size_idx = block_size_idx;
    ar->n_blocks = n_blocks;
    ar->n_used_blocks = 0;
    ar->first_free_block = 0;
    for (i = 0; i < n_blocks - 1; i++) {
        b = arena_get_block(ar, i, block_size);
        b->u.free_next = i + 1;
        b->block_size_idx = block_size_idx;
    }
    b = arena_get_block(ar, n_blocks - 1, block_size);
    b->u.free_next = JS_ARENA_FREE_NIL;
    b->block_size_idx = block_size_idx;
    list_add(&ar->link, &rt->arena_state.arena_list[block_size_idx]);
    list_add(&ar->free_link, &rt->arena_state.free_arena_list[block_size_idx]);
    return ar;
}

static no_inline void *arena_malloc_large(JSRuntime *rt, size_t size)
{
    JSMallocBlockHeader *b;
    b = rt->mf.js_malloc(rt->malloc_state.opaque,
                         sizeof(JSMallocBlockHeader) + size);
    if (!b)
        return NULL;
    b->u.block_idx = JS_ARENA_FREE_NIL;
    b->block_size_idx = 0xff; /* fail safe */
    return b->user_data;
}

static no_inline void *arena_calloc_large(JSRuntime *rt, size_t size)
{
    JSMallocBlockHeader *b;
    b = rt->mf.js_calloc(rt->malloc_state.opaque, 1,
                         sizeof(JSMallocBlockHeader) + size);
    if (!b)
        return NULL;
    b->u.block_idx = JS_ARENA_FREE_NIL;
    b->block_size_idx = 0xff; /* fail safe */
    return b->user_data;
}

static void *js_arena_malloc(JSRuntime *rt, size_t size)
{
    size_t total_size;

    if (unlikely(size == 0))
        return arena_zero_block(rt)->user_data;
    total_size = ((size + JS_ARENA_ALIGN - 1) & ~(size_t)(JS_ARENA_ALIGN - 1)) +
        sizeof(JSMallocBlockHeader);
    if (!JS_ARENA_LARGE_BLOCKS_ONLY && total_size <= JS_ARENA_MAX_SMALL_SIZE) {
        int block_size_idx;
        unsigned int block_idx, block_size;
        JSMallocBlockHeader *b;
        JSArena *ar;
        struct list_head *el, *head;

        block_size_idx = arena_get_size_index(total_size);
        block_size = arena_block_sizes[block_size_idx];
        head = &rt->arena_state.free_arena_list[block_size_idx];
        el = head->next;
        if (unlikely(el == head)) {
            ar = arena_new(rt, block_size_idx);
            if (!ar)
                return NULL;
        } else {
            ar = list_entry(el, JSArena, free_link);
        }
        block_idx = ar->first_free_block;
        b = arena_get_block(ar, block_idx, block_size);
        ar->first_free_block = b->u.free_next;
        b->u.block_idx = block_idx;
        ar->n_used_blocks++;
        if (unlikely(ar->n_used_blocks == ar->n_blocks))
            list_del(&ar->free_link);
        return b->user_data;
    } else {
        return arena_malloc_large(rt, size);
    }
}

static void js_arena_free(JSRuntime *rt, void *ptr)
{
    JSMallocBlockHeader *b;

    if (!ptr)
        return;
    b = container_of(ptr, JSMallocBlockHeader, user_data);
    if (unlikely(b->u.block_idx == JS_ARENA_FREE_NIL)) {
        /* large or zero-size block */
        if (b == arena_zero_block(rt)) {
            /* nothing to do */
        } else {
            rt->mf.js_free(rt->malloc_state.opaque, b);
        }
    } else {
        unsigned int block_idx = b->u.block_idx;
        unsigned int block_size_idx = b->block_size_idx;
        unsigned int block_size = arena_block_sizes[block_size_idx];
        JSArena *ar = (JSArena *)((uint8_t *)b -
                                  (size_t)block_size * block_idx -
                                  sizeof(JSArena));
        b->u.free_next = ar->first_free_block;
        ar->first_free_block = block_idx;
        if (unlikely(ar->n_used_blocks == ar->n_blocks))
            list_add(&ar->free_link,
                     &rt->arena_state.free_arena_list[block_size_idx]);
        ar->n_used_blocks--;
        if (unlikely(ar->n_used_blocks == 0)) {
            list_del(&ar->link);
            list_del(&ar->free_link);
            rt->mf.js_free(rt->malloc_state.opaque, ar);
        }
    }
}

