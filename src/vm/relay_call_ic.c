/* Engine domain source: vm/relay_call_ic.c.
 * Ownership: vm subsystem. Assembled by tools/generators/generate_engine_unit.py; not compiled independently yet.
 */

/*
 * Relay monomorphic call inline cache.
 *
 * Relay owns two fast paths:
 *   - decoded tiny numeric leaves;
 *   - generation-checked Spool native entries.
 *
 * A call site stores only the callee's stable numeric identity, dispatch kind,
 * and the expected Vault generation. The actual executable pointer remains in
 * the target function's Spool entry handle and is invalidated before Vault
 * releases code memory. Relay therefore never retains a dangling code pointer.
 */

static inline uint32_t turbojs_relay_call_hash(uint32_t bytecode_offset)
{
    return (bytecode_offset * UINT32_C(2654435761) >> 28) &
           (TURBOJS_VM_RELAY_CALL_IC_SIZE - 1u);
}

static inline TurboJSVMRelayCallICEntry *turbojs_relay_call_slot(
    JSFunctionBytecode *caller, uint32_t bytecode_offset, int create)
{
    TurboJSVMRelayCallICEntry *entry;
    uint32_t index;
    if (!caller)
        return NULL;
    if (!caller->relay_call_ic) {
        if (!create)
            return NULL;
        caller->relay_call_ic = js_mallocz_rt(caller->realm->rt,
            sizeof(*caller->relay_call_ic) * TURBOJS_VM_RELAY_CALL_IC_SIZE);
        if (!caller->relay_call_ic)
            return NULL;
    }
    index = turbojs_relay_call_hash(bytecode_offset);
    entry = &caller->relay_call_ic[index];
    if (entry->bytecode_offset != bytecode_offset) {
        if (!create)
            return NULL;
        memset(entry, 0, sizeof(*entry));
        entry->bytecode_offset = bytecode_offset;
    }
    return entry;
}

static inline void turbojs_relay_invalidate_native_entry(
    JSRuntime *rt, TurboJSVMRelayCallICEntry *entry)
{
    entry->kind = TURBOJS_VM_RELAY_CALL_EMPTY;
    entry->secondary_kind = TURBOJS_VM_RELAY_CALL_EMPTY;
    entry->target_identity = 0;
    entry->native_generation = 0;
    entry->secondary_target_identity = 0;
    entry->secondary_native_generation = 0;
    if (entry->generation != UINT32_MAX)
        entry->generation++;
    rt->relay_call_invalidations++;
    rt->relay_spool_invalidations++;
}

static inline int turbojs_relay_try_call(
    JSRuntime *rt, JSFunctionBytecode *caller, uint32_t bytecode_offset,
    JSFunctionBytecode *target, int argc, JSValueConst *argv,
    JSValue *out_value)
{
    TurboJSVMRelayCallICEntry *entry;
    TurboJSTinyLeafPlan *plan = NULL;
    uint64_t identity;
    uint64_t native_generation;
    uint8_t kind;
    int handled = 0;
    int is_spool = 0;
    if (!rt || !rt->jit_enabled || !caller || !target || !caller->relay_call_ic)
        return 0;
    entry = turbojs_relay_call_slot(caller, bytecode_offset, 0);
    if (!entry || entry->kind == TURBOJS_VM_RELAY_CALL_EMPTY ||
        entry->kind == TURBOJS_VM_RELAY_CALL_DISABLED)
        return 0;
    identity = turbojs_vm_function_identity(rt, target);
    if (likely(identity == entry->target_identity)) {
        kind = entry->kind;
        native_generation = entry->native_generation;
    } else if (entry->secondary_kind != TURBOJS_VM_RELAY_CALL_EMPTY &&
               identity == entry->secondary_target_identity) {
        kind = entry->secondary_kind;
        native_generation = entry->secondary_native_generation;
    } else {
        if (entry->misses != UINT32_MAX)
            entry->misses++;
        rt->relay_call_misses++;
        return 0;
    }

    switch (kind) {
    case TURBOJS_VM_RELAY_CALL_AFFINE_LEAF:
    case TURBOJS_VM_RELAY_CALL_GENERIC_LEAF:
        plan = (TurboJSTinyLeafPlan *)target->jit_inline_leaf_plan;
        if (unlikely(!plan || target->jit_inline_leaf_state != 1)) {
            turbojs_relay_invalidate_native_entry(rt, entry);
            return 0;
        }
        if (kind == TURBOJS_VM_RELAY_CALL_AFFINE_LEAF)
            handled = turbojs_execute_affine_leaf_plan(target, plan, argc, argv,
                                                       out_value);
        else
            handled = turbojs_try_inline_leaf_call(target, argc, argv,
                                                    out_value);
        break;
    case TURBOJS_VM_RELAY_CALL_SPOOL_INT32:
        is_spool = 1;
        handled = turbojs_spool_entry_invoke(
            rt, target, TURBOJS_NATIVE_ENTRY_INT32,
            native_generation, argc, argv, out_value);
        if (unlikely(handled == TURBOJS_SPOOL_ENTRY_STALE)) {
            rt->relay_spool_stale_bailouts++;
            turbojs_relay_invalidate_native_entry(rt, entry);
            return 0;
        }
        break;
    case TURBOJS_VM_RELAY_CALL_SPOOL_FLOAT64:
        is_spool = 1;
        handled = turbojs_spool_entry_invoke(
            rt, target, TURBOJS_NATIVE_ENTRY_FLOAT64,
            native_generation, argc, argv, out_value);
        if (unlikely(handled == TURBOJS_SPOOL_ENTRY_STALE)) {
            rt->relay_spool_stale_bailouts++;
            turbojs_relay_invalidate_native_entry(rt, entry);
            return 0;
        }
        break;
    default:
        return 0;
    }

    if (likely(handled)) {
        if (entry->hits != UINT32_MAX)
            entry->hits++;
        rt->relay_call_hits++;
        return 1;
    }
    if (entry->misses != UINT32_MAX)
        entry->misses++;
    rt->relay_call_misses++;
    if (is_spool) {
        rt->relay_spool_misses++;
        rt->relay_spool_callee_bailouts++;
    }
    return 0;
}

static inline void turbojs_relay_install_leaf(
    JSRuntime *rt, JSFunctionBytecode *caller, uint32_t bytecode_offset,
    JSFunctionBytecode *target)
{
    TurboJSVMRelayCallICEntry *entry;
    TurboJSTinyLeafPlan *plan;
    uint64_t identity;
    uint8_t kind;
    if (!rt || !rt->jit_enabled || !caller || !target ||
        target->jit_inline_leaf_state != 1)
        return;
    plan = (TurboJSTinyLeafPlan *)target->jit_inline_leaf_plan;
    if (!plan)
        return;
    kind = plan->kind == 1 ? TURBOJS_VM_RELAY_CALL_AFFINE_LEAF :
                            TURBOJS_VM_RELAY_CALL_GENERIC_LEAF;
    identity = turbojs_vm_function_identity(rt, target);
    entry = turbojs_relay_call_slot(caller, bytecode_offset, 1);
    if (!entry)
        return;
    if (entry->kind == TURBOJS_VM_RELAY_CALL_DISABLED)
        return;
    if (entry->kind == TURBOJS_VM_RELAY_CALL_EMPTY) {
        entry->target_identity = identity;
        entry->native_generation = 0;
        entry->kind = kind;
        entry->hits = 1;
        entry->generation = entry->generation ? entry->generation : 1;
        rt->relay_call_installs++;
        return;
    }
    if (entry->target_identity == identity ||
        entry->secondary_target_identity == identity)
        return;
    if (entry->secondary_kind == TURBOJS_VM_RELAY_CALL_EMPTY) {
        entry->secondary_target_identity = identity;
        entry->secondary_native_generation = 0;
        entry->secondary_kind = kind;
        if (entry->generation != UINT32_MAX)
            entry->generation++;
        rt->relay_call_installs++;
        return;
    }
    entry->kind = TURBOJS_VM_RELAY_CALL_DISABLED;
    entry->secondary_kind = TURBOJS_VM_RELAY_CALL_EMPTY;
    entry->native_generation = 0;
    entry->secondary_native_generation = 0;
    if (entry->generation != UINT32_MAX)
        entry->generation++;
    rt->relay_call_invalidations++;
}

static inline void turbojs_relay_install_spool(
    JSRuntime *rt, JSFunctionBytecode *caller, uint32_t bytecode_offset,
    JSFunctionBytecode *target)
{
    TurboJSVMRelayCallICEntry *entry;
    const TurboJSNativeEntryHandle *handle;
    TurboJSVMRelayCallKind kind;
    uint64_t identity;
    if (!rt || !rt->jit_enabled || !caller || !target)
        return;
    if (target->jit_reserved == 1 &&
        target->jit_spool_int32_entry.function) {
        handle = &target->jit_spool_int32_entry;
        kind = TURBOJS_VM_RELAY_CALL_SPOOL_INT32;
    } else if (target->jit_reserved == 2 &&
               target->jit_spool_float64_entry.function) {
        handle = &target->jit_spool_float64_entry;
        kind = TURBOJS_VM_RELAY_CALL_SPOOL_FLOAT64;
    } else {
        return;
    }
    identity = turbojs_vm_function_identity(rt, target);
#ifdef TURBOJS_ENABLE_OPTIMIZING_JIT
    {
        const TurboJSCallFeedbackSlot *feedback =
            turbojs_vm_call_feedback_lookup(caller, bytecode_offset);
        uint32_t i;
        int observed = 0;
        if (!feedback || feedback->target_count == 0 ||
            feedback->target_count > 2 ||
            feedback->state == TURBOJS_CALL_FEEDBACK_MEGAMORPHIC) {
            rt->relay_spool_feedback_rejections++;
            return;
        }
        for (i = 0; i < feedback->target_count; ++i) {
            if (feedback->targets[i].target_identity == identity) {
                observed = 1;
                break;
            }
        }
        if (!observed) {
            rt->relay_spool_feedback_rejections++;
            return;
        }
    }
#endif
    entry = turbojs_relay_call_slot(caller, bytecode_offset, 1);
    if (!entry || entry->kind == TURBOJS_VM_RELAY_CALL_DISABLED)
        return;
    if (entry->kind == TURBOJS_VM_RELAY_CALL_EMPTY) {
        entry->target_identity = identity;
        entry->native_generation = handle->generation;
        entry->kind = (uint8_t)kind;
        entry->hits = 1;
        entry->generation = entry->generation ? entry->generation : 1;
    } else if (entry->target_identity == identity ||
               entry->secondary_target_identity == identity) {
        return;
    } else if (entry->secondary_kind == TURBOJS_VM_RELAY_CALL_EMPTY) {
        entry->secondary_target_identity = identity;
        entry->secondary_native_generation = handle->generation;
        entry->secondary_kind = (uint8_t)kind;
        if (entry->generation != UINT32_MAX)
            entry->generation++;
    } else {
        entry->kind = TURBOJS_VM_RELAY_CALL_DISABLED;
        entry->secondary_kind = TURBOJS_VM_RELAY_CALL_EMPTY;
        entry->native_generation = 0;
        entry->secondary_native_generation = 0;
        if (entry->generation != UINT32_MAX)
            entry->generation++;
        rt->relay_call_invalidations++;
        return;
    }
    rt->relay_call_installs++;
    rt->relay_spool_installs++;
#ifdef TURBOJS_ENABLE_OPTIMIZING_JIT
    rt->relay_spool_feedback_installs++;
#endif
}

static void turbojs_vm_relay_call_ic_destroy(JSRuntime *rt,
                                              JSFunctionBytecode *b)
{
    if (!b || !b->relay_call_ic)
        return;
    js_free_rt(rt, b->relay_call_ic);
    b->relay_call_ic = NULL;
}
