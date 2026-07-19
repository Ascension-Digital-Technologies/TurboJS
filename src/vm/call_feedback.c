/* Engine domain source: vm/call_feedback.c.
 * Ownership: vm subsystem. Assembled by tools/generators/generate_engine_unit.py; not compiled independently yet.
 */

/*
 * Telemetry call-site feedback.
 *
 * This cache intentionally stores stable numeric function identities rather
 * than target pointers. It can therefore survive target collection safely and
 * become the observation source for future Relay direct-call stubs.
 * Collection is bounded per caller. Once the budget is exhausted the caller
 * retains its stable feedback but leaves the hot call path uninstrumented.
 */
#define TURBOJS_VM_CALL_FEEDBACK_BUDGET 8192u

static inline int turbojs_vm_call_feedback_enabled(
    const JSRuntime *rt, const JSFunctionBytecode *caller)
{
    return rt && rt->jit_profiling_enabled && caller &&
           (!caller->call_feedback || caller->jit_call_feedback_remaining != 0);
}
static inline uint64_t turbojs_vm_function_identity(JSRuntime *rt,
                                                     JSFunctionBytecode *b)
{
    uint64_t identity;
    if (likely(b->jit_identity != 0))
        return b->jit_identity;
    identity = ++rt->jit_next_function_identity;
    if (unlikely(identity == 0))
        identity = ++rt->jit_next_function_identity;
    b->jit_identity = identity;
    b->jit_registry_prev = NULL;
    b->jit_registry_next = rt->jit_function_registry;
    if (rt->jit_function_registry)
        rt->jit_function_registry->jit_registry_prev = b;
    rt->jit_function_registry = b;
    return identity;
}

static inline JSFunctionBytecode *turbojs_vm_function_by_identity(
    JSRuntime *rt, uint64_t identity)
{
    JSFunctionBytecode *b;
    if (!rt || identity == 0)
        return NULL;
    for (b = rt->jit_function_registry; b; b = b->jit_registry_next) {
        if (b->jit_identity == identity)
            return b;
    }
    return NULL;
}

static inline void turbojs_vm_function_identity_unregister(
    JSRuntime *rt, JSFunctionBytecode *b)
{
    if (!rt || !b || b->jit_identity == 0)
        return;
    if (b->jit_registry_prev)
        b->jit_registry_prev->jit_registry_next = b->jit_registry_next;
    else if (rt->jit_function_registry == b)
        rt->jit_function_registry = b->jit_registry_next;
    if (b->jit_registry_next)
        b->jit_registry_next->jit_registry_prev = b->jit_registry_prev;
    b->jit_registry_prev = NULL;
    b->jit_registry_next = NULL;
}

static inline TurboJSVMCallFeedbackEntry *turbojs_vm_call_feedback_slot(
    JSFunctionBytecode *caller, uint32_t bytecode_offset)
{
    uint32_t h;
    TurboJSVMCallFeedbackEntry *entry;
    if (unlikely(!caller->call_feedback)) {
        caller->call_feedback = js_mallocz_rt(caller->realm->rt,
            sizeof(*caller->call_feedback) * TURBOJS_VM_CALL_FEEDBACK_SIZE);
        if (unlikely(!caller->call_feedback))
            return NULL;
        caller->jit_call_feedback_remaining = TURBOJS_VM_CALL_FEEDBACK_BUDGET;
    }
    h = bytecode_offset * 2654435761u;
    entry = &caller->call_feedback[(h >> 28) &
        (TURBOJS_VM_CALL_FEEDBACK_SIZE - 1u)];
    if (unlikely(entry->bytecode_offset != bytecode_offset)) {
        memset(entry, 0, sizeof(*entry));
        entry->bytecode_offset = bytecode_offset;
        TurboJS_CallFeedbackInit(&entry->slot);
    }
    return entry;
}

static inline TurboJSCallFeedbackState turbojs_vm_call_feedback_observe(
    JSRuntime *rt, JSFunctionBytecode *caller, uint32_t bytecode_offset,
    JSFunctionBytecode *target)
{
    TurboJSVMCallFeedbackEntry *entry;
    TurboJSCallFeedbackSlot *slot;
    TurboJSCallFeedbackState before, after;
    uint64_t identity;
    uint32_t i;
    entry = turbojs_vm_call_feedback_slot(caller, bytecode_offset);
    if (unlikely(!entry))
        return TURBOJS_CALL_FEEDBACK_UNINITIALIZED;
    if (caller->jit_call_feedback_remaining == 0)
        return (TurboJSCallFeedbackState)entry->slot.state;
    caller->jit_call_feedback_remaining--;
    identity = turbojs_vm_function_identity(rt, target);
    slot = &entry->slot;
    before = (TurboJSCallFeedbackState)slot->state;
    if (likely(slot->observations != UINT64_MAX))
        slot->observations++;

    for (i = 0; i < slot->target_count; ++i) {
        if (likely(slot->targets[i].target_identity == identity)) {
            if (slot->targets[i].hits != UINT32_MAX)
                slot->targets[i].hits++;
            rt->call_feedback_observations++;
            return (TurboJSCallFeedbackState)slot->state;
        }
    }

    if (slot->misses != UINT32_MAX)
        slot->misses++;
    if (slot->state != TURBOJS_CALL_FEEDBACK_MEGAMORPHIC) {
        if (slot->target_count < TURBOJS_CALL_FEEDBACK_MAX_TARGETS) {
            TurboJSCallFeedbackTarget *observed =
                &slot->targets[slot->target_count++];
            observed->target_identity = identity;
            observed->hits = 1u;
            slot->state = slot->target_count == 1u
                ? TURBOJS_CALL_FEEDBACK_MONOMORPHIC
                : TURBOJS_CALL_FEEDBACK_POLYMORPHIC;
        } else {
            slot->state = TURBOJS_CALL_FEEDBACK_MEGAMORPHIC;
        }
    }
    after = (TurboJSCallFeedbackState)slot->state;
    rt->call_feedback_observations++;
    if (before != after) {
        if (slot->generation != UINT32_MAX)
            slot->generation++;
        rt->call_feedback_transitions++;
        switch (after) {
        case TURBOJS_CALL_FEEDBACK_MONOMORPHIC:
            rt->call_feedback_monomorphic++;
            break;
        case TURBOJS_CALL_FEEDBACK_POLYMORPHIC:
            rt->call_feedback_polymorphic++;
            break;
        case TURBOJS_CALL_FEEDBACK_MEGAMORPHIC:
            rt->call_feedback_megamorphic++;
            break;
        default:
            break;
        }
    }
    return after;
}

static inline const TurboJSCallFeedbackSlot *turbojs_vm_call_feedback_lookup(
    const JSFunctionBytecode *caller, uint32_t bytecode_offset)
{
    uint32_t h;
    const TurboJSVMCallFeedbackEntry *entry;
    if (!caller || !caller->call_feedback)
        return NULL;
    h = bytecode_offset * 2654435761u;
    entry = &caller->call_feedback[(h >> 28) &
        (TURBOJS_VM_CALL_FEEDBACK_SIZE - 1u)];
    if (entry->bytecode_offset != bytecode_offset)
        return NULL;
    return &entry->slot;
}

static void turbojs_vm_call_feedback_destroy(JSRuntime *rt,
                                               JSFunctionBytecode *b)
{
    if (!b || !b->call_feedback)
        return;
    js_free_rt(rt, b->call_feedback);
    b->call_feedback = NULL;
    b->jit_call_feedback_remaining = 0;
}
