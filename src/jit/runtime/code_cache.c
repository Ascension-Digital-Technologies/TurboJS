#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "jit.h"

typedef struct TurboJSDependencyNode TurboJSDependencyNode;
typedef struct TurboJSRepatchNode TurboJSRepatchNode;

typedef struct TurboJSCodeCacheEntry {
    const void *key;
    TurboJSNativeFunction *function;
    TurboJSNativeEntryHandle *entry_handle;
    size_t code_size;
    uint64_t stamp;
    uint32_t access_count;
    uint8_t entry_kind;
    uint8_t cache_kind; /* 0 function, 1 continuation */
    uint8_t state; /* 0 empty, 1 live, 2 tombstone */
    uint64_t continuation_function_id;
    uint64_t continuation_revision;
    uint64_t target_identity;
    size_t continuation_start_instruction;
    size_t continuation_prologue_count;
    TurboJSDependencyNode *dependencies;
    TurboJSRepatchNode *repatch_dependencies;
} TurboJSCodeCacheEntry;

struct TurboJSDependencyNode {
    const TurboJSNativeEntryHandle *target;
    TurboJSCodeCacheEntry *owner;
    TurboJSDependencyNode *next_target;
    TurboJSDependencyNode *next_owner;
};


struct TurboJSRepatchNode {
    uint64_t target_identity;
    TurboJSCodeCacheEntry *owner;
    TurboJSRepatchNode *next_target;
    TurboJSRepatchNode *next_owner;
};

struct TurboJSCodeCache {
    TurboJSCodeCacheEntry *entries;
    size_t slot_count;
    size_t count;
    size_t maximum_entries;
    size_t maximum_code_bytes;
    size_t code_bytes;
    uint64_t clock;
    const void *last_key;
    TurboJSNativeFunction *last_function;
    TurboJSCodeCacheStats stats;
    TurboJSDependencyNode **dependency_buckets;
    size_t dependency_bucket_count;
    TurboJSRepatchNode **repatch_buckets;
    size_t repatch_bucket_count;
};

static size_t dependency_hash(const TurboJSNativeEntryHandle *target,
                              size_t bucket_count)
{
    uintptr_t x = (uintptr_t)target;
#if UINTPTR_MAX > UINT32_MAX
    x ^= x >> 33;
    x *= UINT64_C(0xff51afd7ed558ccd);
    x ^= x >> 33;
#else
    x ^= x >> 16;
    x *= UINT32_C(0x7feb352d);
    x ^= x >> 15;
#endif
    return bucket_count ? (size_t)x & (bucket_count - 1u) : 0;
}

static size_t identity_hash(uint64_t identity, size_t bucket_count)
{
    uint64_t x = identity;
    x ^= x >> 33;
    x *= UINT64_C(0xff51afd7ed558ccd);
    x ^= x >> 33;
    x *= UINT64_C(0xc4ceb9fe1a85ec53);
    x ^= x >> 33;
    return bucket_count ? (size_t)x & (bucket_count - 1u) : 0;
}

static void unregister_entry_dependencies(TurboJSCodeCache *cache,
                                          TurboJSCodeCacheEntry *entry)
{
    TurboJSDependencyNode *node;
    if (!cache || !entry)
        return;
    node = entry->dependencies;
    while (node) {
        TurboJSDependencyNode *next = node->next_owner;
        size_t bucket = dependency_hash(node->target,
                                        cache->dependency_bucket_count);
        TurboJSDependencyNode **link = &cache->dependency_buckets[bucket];
        while (*link && *link != node)
            link = &(*link)->next_target;
        if (*link == node)
            *link = node->next_target;
        free(node);
        cache->stats.reverse_dependency_unregistrations++;
        node = next;
    }
    entry->dependencies = NULL;
}

static void unregister_entry_repatch_dependencies(
    TurboJSCodeCache *cache, TurboJSCodeCacheEntry *entry)
{
    TurboJSRepatchNode *node;
    if (!cache || !entry)
        return;
    node = entry->repatch_dependencies;
    while (node) {
        TurboJSRepatchNode *next = node->next_owner;
        size_t bucket = identity_hash(node->target_identity,
                                      cache->repatch_bucket_count);
        TurboJSRepatchNode **link = &cache->repatch_buckets[bucket];
        while (*link && *link != node)
            link = &(*link)->next_target;
        if (*link == node)
            *link = node->next_target;
        free(node);
        cache->stats.repatch_identity_unregistrations++;
        node = next;
    }
    entry->repatch_dependencies = NULL;
}

static int register_entry_repatch_dependencies(
    TurboJSCodeCache *cache, TurboJSCodeCacheEntry *entry)
{
    size_t i, count;
    if (!cache || !entry || !entry->function)
        return 0;
    unregister_entry_repatch_dependencies(cache, entry);
    count = TurboJS_NativeClutchSiteCount(entry->function);
    for (i = 0; i < count; ++i) {
        uint64_t identity = TurboJS_NativeClutchSiteIdentityAt(
            entry->function, i);
        TurboJSRepatchNode *scan, *node;
        size_t bucket;
        if (!identity)
            continue;
        for (scan = entry->repatch_dependencies; scan;
             scan = scan->next_owner) {
            if (scan->target_identity == identity)
                break;
        }
        if (scan)
            continue;
        node = (TurboJSRepatchNode *)calloc(1, sizeof(*node));
        if (!node) {
            unregister_entry_repatch_dependencies(cache, entry);
            return 0;
        }
        bucket = identity_hash(identity, cache->repatch_bucket_count);
        node->target_identity = identity;
        node->owner = entry;
        node->next_target = cache->repatch_buckets[bucket];
        cache->repatch_buckets[bucket] = node;
        node->next_owner = entry->repatch_dependencies;
        entry->repatch_dependencies = node;
        cache->stats.repatch_identity_registrations++;
    }
    return 1;
}

static int register_entry_dependencies(TurboJSCodeCache *cache,
                                       TurboJSCodeCacheEntry *entry)
{
    size_t i, count;
    if (!cache || !entry || !entry->function)
        return 0;
    unregister_entry_dependencies(cache, entry);
    count = TurboJS_NativeClutchSiteCount(entry->function);
    for (i = 0; i < count; ++i) {
        const TurboJSNativeEntryHandle *target =
            TurboJS_NativeClutchSiteTargetAt(entry->function, i);
        TurboJSDependencyNode *node;
        size_t bucket;
        TurboJSDependencyNode *scan;
        if (!target)
            continue;
        /* One owner-target edge is enough even if multiple call sites share it. */
        for (scan = entry->dependencies; scan; scan = scan->next_owner) {
            if (scan->target == target)
                break;
        }
        if (scan)
            continue;
        node = (TurboJSDependencyNode *)calloc(1, sizeof(*node));
        if (!node) {
            unregister_entry_dependencies(cache, entry);
            return 0;
        }
        bucket = dependency_hash(target, cache->dependency_bucket_count);
        node->target = target;
        node->owner = entry;
        node->next_target = cache->dependency_buckets[bucket];
        cache->dependency_buckets[bucket] = node;
        node->next_owner = entry->dependencies;
        entry->dependencies = node;
        cache->stats.reverse_dependency_registrations++;
    }
    if (!register_entry_repatch_dependencies(cache, entry)) {
        unregister_entry_dependencies(cache, entry);
        return 0;
    }
    return 1;
}

static size_t invalidate_indexed_dependents(
    TurboJSCodeCache *cache, const TurboJSNativeEntryHandle *target)
{
    size_t invalidated_total = 0;
    size_t bucket;
    TurboJSDependencyNode *node;
    if (!cache || !target || !cache->dependency_bucket_count)
        return 0;
    cache->stats.reverse_dependency_lookups++;
    bucket = dependency_hash(target, cache->dependency_bucket_count);
    node = cache->dependency_buckets[bucket];
    while (node) {
        TurboJSDependencyNode *next = node->next_target;
        if (node->target == target && node->owner &&
            node->owner->state == 1) {
            size_t invalidated;
            cache->stats.reverse_dependency_nodes_visited++;
            invalidated = TurboJS_NativeInvalidateClutchTarget(
                node->owner->function, target);
            invalidated_total += invalidated;
            cache->stats.dependent_call_sites_invalidated += invalidated;
            if (node->owner->cache_kind == 1)
                cache->stats.continuation_dependency_invalidations +=
                    invalidated;
        }
        node = next;
    }
    return invalidated_total;
}

static uint64_t next_entry_generation(uint64_t generation)
{
    generation++;
    return generation ? generation : UINT64_C(1);
}

void TurboJS_NativeEntryHandleInit(TurboJSNativeEntryHandle *handle)
{
    if (handle)
        memset(handle, 0, sizeof(*handle));
}

void TurboJS_NativeEntryHandleInvalidate(TurboJSNativeEntryHandle *handle)
{
    if (!handle)
        return;
    if (handle->function || handle->kind != TURBOJS_NATIVE_ENTRY_NONE)
        handle->generation = next_entry_generation(handle->generation);
    handle->function = NULL;
    handle->argument_count = 0;
    handle->kind = TURBOJS_NATIVE_ENTRY_NONE;
    handle->result_kind = TURBOJS_VALUE_UNKNOWN;
}

static void publish_entry_handle(TurboJSNativeEntryHandle *handle,
                                 const TurboJSNativeFunction *function,
                                 TurboJSNativeEntryKind kind,
                                 uint16_t argument_count)
{
    if (!handle)
        return;
    if (handle->function == function && handle->kind == (uint8_t)kind &&
        handle->argument_count == argument_count)
        return;
    handle->generation = next_entry_generation(handle->generation);
    handle->function = function;
    handle->argument_count = argument_count;
    handle->kind = (uint8_t)kind;
    handle->result_kind = (uint8_t)TurboJS_NativeResultKind(function);
}

int TurboJS_NativeEntryHandleIsLive(const TurboJSNativeEntryHandle *handle,
                                    uint64_t expected_generation,
                                    TurboJSNativeEntryKind expected_kind)
{
    return handle && handle->function && expected_generation &&
           handle->generation == expected_generation &&
           handle->kind == (uint8_t)expected_kind;
}

TurboJSIRStatus TurboJS_NativeEntryInvokeI64(
    const TurboJSNativeEntryHandle *handle,
    uint64_t expected_generation,
    const int64_t *arguments,
    size_t argument_count,
    int64_t *result)
{
    if (!TurboJS_NativeEntryHandleIsLive(handle, expected_generation,
                                         TURBOJS_NATIVE_ENTRY_INT32))
        return TURBOJS_IR_UNSUPPORTED;
    if (argument_count < handle->argument_count)
        return TURBOJS_IR_INVALID_ARGUMENT;
    return TurboJS_NativeInvoke(handle->function, arguments, argument_count,
                                result);
}

TurboJSIRStatus TurboJS_NativeEntryInvokeF64(
    const TurboJSNativeEntryHandle *handle,
    uint64_t expected_generation,
    const double *arguments,
    size_t argument_count,
    double *result)
{
    if (!TurboJS_NativeEntryHandleIsLive(handle, expected_generation,
                                         TURBOJS_NATIVE_ENTRY_FLOAT64))
        return TURBOJS_IR_UNSUPPORTED;
    if (argument_count < handle->argument_count)
        return TURBOJS_IR_INVALID_ARGUMENT;
    return TurboJS_NativeInvokeF64(handle->function, arguments, argument_count,
                                   result);
}

void TurboJS_ClutchCallFrameInit(TurboJSClutchCallFrame *frame)
{
    if (frame)
        memset(frame, 0, sizeof(*frame));
}

TurboJSIRStatus TurboJS_ClutchCallI64(
    const TurboJSClutchCallFrame *frame, const int64_t *arguments,
    int64_t *result)
{
    if (!frame || !frame->target || !result)
        return TURBOJS_IR_INVALID_ARGUMENT;
    return TurboJS_NativeEntryInvokeI64(
        frame->target, frame->expected_generation, arguments,
        frame->argument_count, result);
}

TurboJSIRStatus TurboJS_ClutchCallF64(
    const TurboJSClutchCallFrame *frame, const double *arguments,
    double *result)
{
    if (!frame || !frame->target || !result)
        return TURBOJS_IR_INVALID_ARGUMENT;
    return TurboJS_NativeEntryInvokeF64(
        frame->target, frame->expected_generation, arguments,
        frame->argument_count, result);
}

void TurboJS_ClutchCallSiteInit(TurboJSClutchCallSite *site,
                                const TurboJSNativeEntryHandle *target,
                                uint64_t expected_generation,
                                TurboJSNativeEntryKind expected_kind,
                                uint16_t argument_count)
{
    if (!site)
        return;
    memset(site, 0, sizeof(*site));
    site->target = target;
    site->expected_generation = expected_generation;
    site->expected_kind = (uint8_t)expected_kind;
    site->argument_count = argument_count;
    site->receiver_register = TURBOJS_IR_NO_REGISTER;
    for (uint16_t i = 0; i < TURBOJS_CLUTCH_MAX_ARGUMENTS; ++i)
        site->argument_registers[i] = TURBOJS_IR_NO_REGISTER;
}

void TurboJS_ClutchCallSiteSetTargetIdentity(
    TurboJSClutchCallSite *site, uint64_t target_identity)
{
    if (site)
        site->target_identity = target_identity;
}

TurboJSIRStatus TurboJS_ClutchCallSiteSetClosureEnvironment(
    TurboJSClutchCallSite *site, void *closure_environment,
    const TurboJSRootingHooks *rooting)
{
    void *rooted = closure_environment;
    if (!site)
        return TURBOJS_IR_INVALID_ARGUMENT;
    TurboJS_ClutchCallSiteDestroy(site);
    if (closure_environment && rooting) {
        if (!rooting->retain || !rooting->release)
            return TURBOJS_IR_INVALID_ARGUMENT;
        rooted = rooting->retain(rooting->opaque, closure_environment);
        if (!rooted)
            return TURBOJS_IR_OUT_OF_MEMORY;
        site->environment_rooting = *rooting;
        site->owns_environment = 1;
    }
    site->closure_environment = rooted;
    if (rooted)
        site->flags |= TURBOJS_CLUTCH_CALL_HAS_ENVIRONMENT;
    else
        site->flags &= (uint8_t)~TURBOJS_CLUTCH_CALL_HAS_ENVIRONMENT;
    return TURBOJS_IR_OK;
}

TurboJSIRStatus TurboJS_ClutchCallSiteClone(
    TurboJSClutchCallSite *destination, const TurboJSClutchCallSite *source)
{
    if (!destination || !source)
        return TURBOJS_IR_INVALID_ARGUMENT;
    *destination = *source;
    destination->owns_environment = 0;
    memset(&destination->environment_rooting, 0,
           sizeof(destination->environment_rooting));
    if (source->closure_environment && source->owns_environment) {
        void *rooted = source->environment_rooting.retain(
            source->environment_rooting.opaque,
            (void *)source->closure_environment);
        if (!rooted) {
            memset(destination, 0, sizeof(*destination));
            return TURBOJS_IR_OUT_OF_MEMORY;
        }
        destination->closure_environment = rooted;
        destination->environment_rooting = source->environment_rooting;
        destination->owns_environment = 1;
    }
    return TURBOJS_IR_OK;
}

void TurboJS_ClutchCallSiteDestroy(TurboJSClutchCallSite *site)
{
    if (!site)
        return;
    if (site->owns_environment && site->closure_environment &&
        site->environment_rooting.release)
        site->environment_rooting.release(
            site->environment_rooting.opaque,
            (void *)site->closure_environment);
    site->closure_environment = NULL;
    memset(&site->environment_rooting, 0, sizeof(site->environment_rooting));
    site->owns_environment = 0;
    site->flags &= (uint8_t)~TURBOJS_CLUTCH_CALL_HAS_ENVIRONMENT;
}

TurboJSIRStatus TurboJS_ClutchCallSiteSetArgument(
    TurboJSClutchCallSite *site, uint16_t argument_index,
    uint16_t source_register)
{
    if (!site || argument_index >= site->argument_count ||
        argument_index >= TURBOJS_CLUTCH_MAX_ARGUMENTS ||
        source_register == TURBOJS_IR_NO_REGISTER)
        return TURBOJS_IR_INVALID_ARGUMENT;
    site->argument_registers[argument_index] = source_register;
    return TURBOJS_IR_OK;
}

TurboJSIRStatus TurboJS_ClutchCallSiteSetReceiver(
    TurboJSClutchCallSite *site, uint16_t source_register)
{
    uint16_t i;
    if (!site || source_register == TURBOJS_IR_NO_REGISTER ||
        site->argument_count >= TURBOJS_CLUTCH_MAX_ARGUMENTS)
        return TURBOJS_IR_INVALID_ARGUMENT;
    if (site->flags & TURBOJS_CLUTCH_CALL_HAS_RECEIVER)
        return TURBOJS_IR_INVALID_ARGUMENT;
    for (i = site->argument_count; i > 0; --i)
        site->argument_registers[i] = site->argument_registers[i - 1u];
    site->argument_registers[0] = source_register;
    site->receiver_register = source_register;
    site->argument_count++;
    site->flags |= TURBOJS_CLUTCH_CALL_HAS_RECEIVER;
    return TURBOJS_IR_OK;
}

TurboJSIRStatus TurboJS_ClutchCallSiteSetReceiverShapeGuard(
    TurboJSClutchCallSite *site, uint32_t shape_offset,
    uint64_t expected_shape_identity)
{
    if (!site || !(site->flags & TURBOJS_CLUTCH_CALL_HAS_RECEIVER) ||
        !expected_shape_identity)
        return TURBOJS_IR_INVALID_ARGUMENT;
    site->receiver_shape_offset = shape_offset;
    site->receiver_shape_identity = expected_shape_identity;
    site->flags |= TURBOJS_CLUTCH_CALL_HAS_RECEIVER_SHAPE_GUARD;
    return TURBOJS_IR_OK;
}

TurboJSIRStatus TurboJS_ClutchCallSiteInvokeI64(
    const TurboJSClutchCallSite *site, const int64_t *arguments,
    int64_t *result)
{
    if (!site || !site->target || !result ||
        site->argument_count > TURBOJS_CLUTCH_MAX_ARGUMENTS ||
        (site->argument_count != 0 && !arguments) ||
        site->expected_kind != TURBOJS_NATIVE_ENTRY_INT32)
        return TURBOJS_IR_INVALID_ARGUMENT;
    return TurboJS_NativeEntryInvokeI64(
        site->target, site->expected_generation, arguments,
        site->argument_count, result);
}

TurboJSIRStatus TurboJS_ClutchCallSiteInvokeF64(
    const TurboJSClutchCallSite *site, const double *arguments,
    double *result)
{
    if (!site || !site->target || !result ||
        site->argument_count > TURBOJS_CLUTCH_MAX_ARGUMENTS ||
        (site->argument_count != 0 && !arguments) ||
        site->expected_kind != TURBOJS_NATIVE_ENTRY_FLOAT64)
        return TURBOJS_IR_INVALID_ARGUMENT;
    return TurboJS_NativeEntryInvokeF64(
        site->target, site->expected_generation, arguments,
        site->argument_count, result);
}

static size_t next_pow2(size_t value)
{
    size_t result = 8;
    if (value > SIZE_MAX / 2)
        return 0;
    while (result < value) {
        if (result > SIZE_MAX / 2)
            return 0;
        result <<= 1;
    }
    return result;
}

static size_t key_hash(const void *key)
{
    uintptr_t x = (uintptr_t)key;
#if UINTPTR_MAX > UINT32_MAX
    x ^= x >> 33;
    x *= UINT64_C(0xff51afd7ed558ccd);
    x ^= x >> 33;
    x *= UINT64_C(0xc4ceb9fe1a85ec53);
    x ^= x >> 33;
#else
    x ^= x >> 16;
    x *= UINT32_C(0x7feb352d);
    x ^= x >> 15;
    x *= UINT32_C(0x846ca68b);
    x ^= x >> 16;
#endif
    return (size_t)x;
}

static size_t find_slot(const TurboJSCodeCache *cache, const void *key,
                        int for_insert)
{
    const size_t mask = cache->slot_count - 1;
    size_t slot = key_hash(key) & mask;
    size_t tombstone = SIZE_MAX;
    size_t probes = 0;

    while (probes++ < cache->slot_count) {
        const TurboJSCodeCacheEntry *entry = &cache->entries[slot];
        if (entry->state == 0)
            return for_insert && tombstone != SIZE_MAX ? tombstone : slot;
        if (entry->state == 1 && entry->key == key)
            return slot;
        if (for_insert && entry->state == 2 && tombstone == SIZE_MAX)
            tombstone = slot;
        slot = (slot + 1) & mask;
    }
    return tombstone;
}

static void touch_entry(TurboJSCodeCache *cache, TurboJSCodeCacheEntry *entry)
{
    if (!cache || !entry)
        return;
    entry->stamp = ++cache->clock;
    if (entry->access_count != UINT32_MAX)
        entry->access_count++;
}

static uint64_t eviction_score(const TurboJSCodeCache *cache,
                               const TurboJSCodeCacheEntry *entry)
{
    uint64_t age, size_units, retention;
    if (!cache || !entry || entry->state != 1)
        return 0;
    age = cache->clock >= entry->stamp ? cache->clock - entry->stamp : 0;
    size_units = 1u + (uint64_t)(entry->code_size / (16u * 1024u));
    if (entry->cache_kind == 1)
        retention = 4u + (entry->access_count < 12u ? entry->access_count : 12u);
    else
        retention = 1u + (entry->access_count < 3u ? entry->access_count : 3u);
    return (age + 1u) * size_units * 1024u / retention;
}

static void remove_slot(TurboJSCodeCache *cache, size_t slot, int eviction)
{
    TurboJSCodeCacheEntry *entry = &cache->entries[slot];
    const size_t removed_code_size = entry->code_size;
    const uint8_t removed_cache_kind = entry->cache_kind;
    if (entry->state != 1)
        return;
    cache->code_bytes -= entry->code_size;
    if (cache->last_key == entry->key) {
        cache->last_key = NULL;
        cache->last_function = NULL;
    }
    if (entry->cache_kind == 0 && entry->entry_handle &&
        entry->entry_handle->function == entry->function) {
        invalidate_indexed_dependents(cache, entry->entry_handle);
        TurboJS_NativeEntryHandleInvalidate(entry->entry_handle);
    }
    unregister_entry_dependencies(cache, entry);
    unregister_entry_repatch_dependencies(cache, entry);
    TurboJS_NativeFunctionDestroy(entry->function);
    entry->key = NULL;
    entry->function = NULL;
    entry->entry_handle = NULL;
    entry->code_size = 0;
    entry->stamp = 0;
    entry->access_count = 0;
    if (removed_cache_kind == 1) {
        cache->stats.continuation_evictions += eviction ? 1u : 0u;
        if (cache->stats.continuation_entry_count)
            cache->stats.continuation_entry_count--;
        if (cache->stats.continuation_code_bytes >= removed_code_size)
            cache->stats.continuation_code_bytes -= removed_code_size;
        else
            cache->stats.continuation_code_bytes = 0;
    }
    entry->entry_kind = TURBOJS_NATIVE_ENTRY_NONE;
    entry->cache_kind = 0;
    entry->continuation_function_id = 0;
    entry->continuation_revision = 0;
    entry->target_identity = 0;
    entry->continuation_start_instruction = 0;
    entry->continuation_prologue_count = 0;
    entry->state = 2;
    cache->count--;
    if (eviction)
        cache->stats.evictions++;
}

static int evict_lru(TurboJSCodeCache *cache)
{
    size_t i;
    size_t victim = SIZE_MAX;
    uint64_t worst_score = 0;
    uint64_t oldest_stamp = UINT64_MAX;
    for (i = 0; i < cache->slot_count; ++i) {
        const TurboJSCodeCacheEntry *entry = &cache->entries[i];
        uint64_t score;
        if (entry->state != 1)
            continue;
        score = eviction_score(cache, entry);
        if (victim == SIZE_MAX || score > worst_score ||
            (score == worst_score && entry->stamp < oldest_stamp)) {
            worst_score = score;
            oldest_stamp = entry->stamp;
            victim = i;
        }
    }
    if (victim == SIZE_MAX)
        return 0;
    if (cache->entries[victim].cache_kind == 0)
        cache->stats.weighted_function_evictions++;
    else
        cache->stats.weighted_continuation_evictions++;
    remove_slot(cache, victim, 1);
    return 1;
}

TurboJSCodeCache *TurboJS_CodeCacheCreate(size_t maximum_entries,
                                          size_t maximum_code_bytes)
{
    TurboJSCodeCache *cache;
    size_t slots;
    if (maximum_entries == 0)
        maximum_entries = 64;
    if (maximum_code_bytes == 0)
        maximum_code_bytes = 4u * 1024u * 1024u;
    slots = next_pow2(maximum_entries < SIZE_MAX / 2 ? maximum_entries * 2 : 0);
    if (!slots)
        return NULL;
    cache = (TurboJSCodeCache *)calloc(1, sizeof(*cache));
    if (!cache)
        return NULL;
    cache->entries = (TurboJSCodeCacheEntry *)calloc(slots, sizeof(*cache->entries));
    cache->dependency_buckets = (TurboJSDependencyNode **)calloc(
        slots, sizeof(*cache->dependency_buckets));
    cache->repatch_buckets = (TurboJSRepatchNode **)calloc(
        slots, sizeof(*cache->repatch_buckets));
    if (!cache->entries || !cache->dependency_buckets ||
        !cache->repatch_buckets) {
        free(cache->repatch_buckets);
        free(cache->dependency_buckets);
        free(cache->entries);
        free(cache);
        return NULL;
    }
    cache->slot_count = slots;
    cache->dependency_bucket_count = slots;
    cache->repatch_bucket_count = slots;
    cache->maximum_entries = maximum_entries;
    cache->maximum_code_bytes = maximum_code_bytes;
    return cache;
}

void TurboJS_CodeCacheDestroy(TurboJSCodeCache *cache)
{
    if (!cache)
        return;
    TurboJS_CodeCacheClear(cache);
    free(cache->repatch_buckets);
    free(cache->dependency_buckets);
    free(cache->entries);
    free(cache);
}

const TurboJSNativeFunction *TurboJS_CodeCacheLookup(TurboJSCodeCache *cache,
                                                     const void *key)
{
    size_t slot;
    if (!cache || !key)
        return NULL;
    if (cache->last_key == key && cache->last_function) {
        cache->stats.hits++;
        /* The fast last-key path still refreshes the owning entry below when found. */
        slot = find_slot(cache, key, 0);
        if (slot != SIZE_MAX && cache->entries[slot].state == 1)
            touch_entry(cache, &cache->entries[slot]);
        return cache->last_function;
    }
    slot = find_slot(cache, key, 0);
    if (slot == SIZE_MAX || cache->entries[slot].state != 1) {
        cache->stats.misses++;
        return NULL;
    }
    cache->stats.hits++;
    touch_entry(cache, &cache->entries[slot]);
    cache->last_key = key;
    cache->last_function = cache->entries[slot].function;
    return cache->entries[slot].function;
}

TurboJSIRStatus TurboJS_CodeCacheCompile(TurboJSCodeCache *cache,
                                         const void *key,
                                         const TurboJSIRFunction *ir,
                                         const TurboJSNativeFunction **out_function,
                                         TurboJSIRDiagnostic *diagnostic)
{
    TurboJSNativeFunction *native = NULL;
    TurboJSIRStatus status;
    size_t code_size;
    size_t slot;
    if (!cache || !key || !ir || !out_function)
        return TURBOJS_IR_INVALID_ARGUMENT;
    slot = find_slot(cache, key, 0);
    if (slot != SIZE_MAX && cache->entries[slot].state == 1) {
        touch_entry(cache, &cache->entries[slot]);
        *out_function = cache->entries[slot].function;
        return TURBOJS_IR_OK;
    }
    status = TurboJS_BaselineCompile(ir, &native, diagnostic);
    if (status == TURBOJS_IR_UNSUPPORTED) {
        TurboJSIRFunction specialized;
        TurboJSIRDiagnostic specialized_diagnostic;
        TurboJSIRStatus specialized_status = TurboJS_IRSpecializeCallableReferences(
            ir, &specialized, &specialized_diagnostic);
        if (specialized_status == TURBOJS_IR_OK) {
            status = TurboJS_BaselineCompile(&specialized, &native, diagnostic);
            TurboJS_IRFunctionDestroy(&specialized);
        }
    }
    if (status != TURBOJS_IR_OK)
        return status;
    code_size = TurboJS_NativeCodeSize(native);
    if (code_size > cache->maximum_code_bytes) {
        TurboJS_NativeFunctionDestroy(native);
        return TURBOJS_IR_UNSUPPORTED;
    }
    while (cache->count &&
           (cache->count >= cache->maximum_entries ||
            code_size > cache->maximum_code_bytes - cache->code_bytes)) {
        if (!evict_lru(cache))
            break;
    }
    if (cache->count >= cache->maximum_entries ||
        code_size > cache->maximum_code_bytes - cache->code_bytes) {
        TurboJS_NativeFunctionDestroy(native);
        return TURBOJS_IR_UNSUPPORTED;
    }
    slot = find_slot(cache, key, 1);
    if (slot == SIZE_MAX) {
        TurboJS_NativeFunctionDestroy(native);
        return TURBOJS_IR_OUT_OF_MEMORY;
    }
    cache->entries[slot].key = key;
    cache->entries[slot].function = native;
    cache->entries[slot].code_size = code_size;
    cache->entries[slot].stamp = ++cache->clock;
    cache->entries[slot].access_count = 1;
    cache->entries[slot].cache_kind = 0;
    cache->entries[slot].state = 1;
    cache->count++;
    cache->code_bytes += code_size;
    if (!register_entry_dependencies(cache, &cache->entries[slot])) {
        remove_slot(cache, slot, 0);
        return TURBOJS_IR_OUT_OF_MEMORY;
    }
    cache->stats.compilations++;
    cache->last_key = key;
    cache->last_function = native;
    *out_function = native;
    return TURBOJS_IR_OK;
}

static void repatch_identity_dependents(
    TurboJSCodeCache *cache, uint64_t target_identity,
    TurboJSNativeEntryHandle *handle, TurboJSNativeEntryKind kind,
    uint16_t argument_count)
{
    size_t bucket, owner_count = 0, owner_capacity = 0, i;
    TurboJSRepatchNode *node;
    TurboJSCodeCacheEntry **owners = NULL;
    if (!cache || !target_identity || !handle || !cache->repatch_bucket_count)
        return;
    cache->stats.repatch_identity_lookups++;
    bucket = identity_hash(target_identity, cache->repatch_bucket_count);
    for (node = cache->repatch_buckets[bucket]; node;
         node = node->next_target) {
        TurboJSCodeCacheEntry *owner = node->owner;
        if (node->target_identity != target_identity || !owner ||
            owner->state != 1 || !owner->function)
            continue;
        cache->stats.repatch_identity_nodes_visited++;
        if (owner_count == owner_capacity) {
            size_t next_capacity = owner_capacity ? owner_capacity * 2u : 8u;
            TurboJSCodeCacheEntry **next =
                (TurboJSCodeCacheEntry **)realloc(
                    owners, next_capacity * sizeof(*owners));
            if (!next) {
                free(owners);
                return;
            }
            owners = next;
            owner_capacity = next_capacity;
        }
        owners[owner_count++] = owner;
    }
    for (i = 0; i < owner_count; ++i) {
        TurboJSCodeCacheEntry *owner = owners[i];
        size_t incompatible = 0, patched;
        cache->stats.clutch_repatch_attempts++;
        patched = TurboJS_NativeRepatchClutchIdentity(
            owner->function, target_identity, handle, handle->generation, kind,
            argument_count, &incompatible);
        cache->stats.clutch_call_sites_repatched += patched;
        cache->stats.clutch_repatch_incompatible += incompatible;
        if (patched) {
            cache->stats.clutch_repatch_successes++;
            (void)register_entry_dependencies(cache, owner);
        }
    }
    free(owners);
}

TurboJSIRStatus TurboJS_CodeCacheAttachEntryHandleIdentity(
    TurboJSCodeCache *cache, const void *key,
    TurboJSNativeEntryHandle *handle, TurboJSNativeEntryKind kind,
    uint16_t argument_count, uint64_t target_identity)
{
    size_t slot;
    TurboJSCodeCacheEntry *entry;
    if (!cache || !key || !handle ||
        (kind != TURBOJS_NATIVE_ENTRY_INT32 &&
         kind != TURBOJS_NATIVE_ENTRY_FLOAT64))
        return TURBOJS_IR_INVALID_ARGUMENT;
    slot = find_slot(cache, key, 0);
    if (slot == SIZE_MAX || cache->entries[slot].state != 1)
        return TURBOJS_IR_UNSUPPORTED;
    entry = &cache->entries[slot];
    if (entry->entry_handle && entry->entry_handle != handle &&
        entry->entry_handle->function == entry->function)
        TurboJS_NativeEntryHandleInvalidate(entry->entry_handle);
    entry->entry_handle = handle;
    entry->entry_kind = (uint8_t)kind;
    entry->target_identity = target_identity;
    publish_entry_handle(handle, entry->function, kind, argument_count);
    repatch_identity_dependents(cache, target_identity, handle, kind,
                                argument_count);
    return TURBOJS_IR_OK;
}

TurboJSIRStatus TurboJS_CodeCacheAttachEntryHandle(
    TurboJSCodeCache *cache, const void *key,
    TurboJSNativeEntryHandle *handle, TurboJSNativeEntryKind kind,
    uint16_t argument_count)
{
    return TurboJS_CodeCacheAttachEntryHandleIdentity(
        cache, key, handle, kind, argument_count, 0);
}

void TurboJS_CodeCacheInvalidate(TurboJSCodeCache *cache, const void *key)
{
    size_t slot;
    if (!cache || !key)
        return;
    slot = find_slot(cache, key, 0);
    if (slot != SIZE_MAX)
        remove_slot(cache, slot, 0);
}

void TurboJS_CodeCacheClear(TurboJSCodeCache *cache)
{
    size_t i;
    if (!cache)
        return;
    for (i = 0; i < cache->slot_count; ++i) {
        if (cache->entries[i].state == 1)
            remove_slot(cache, i, 0);
    }
    memset(cache->entries, 0, cache->slot_count * sizeof(*cache->entries));
    memset(cache->dependency_buckets, 0,
           cache->dependency_bucket_count * sizeof(*cache->dependency_buckets));
    cache->count = 0;
    cache->code_bytes = 0;
    cache->last_key = NULL;
    cache->last_function = NULL;
}

TurboJSCodeCacheStats TurboJS_CodeCacheGetStats(const TurboJSCodeCache *cache)
{
    TurboJSCodeCacheStats stats;
    memset(&stats, 0, sizeof(stats));
    if (!cache)
        return stats;
    stats = cache->stats;
    stats.entry_count = cache->count;
    stats.code_bytes = cache->code_bytes;
    return stats;
}


static TurboJSCodeCacheEntry *find_continuation_entry(
    TurboJSCodeCache *cache, uint64_t function_id, uint64_t function_revision,
    size_t start_instruction)
{
    size_t i;
    if (!cache)
        return NULL;
    for (i = 0; i < cache->slot_count; ++i) {
        TurboJSCodeCacheEntry *entry = &cache->entries[i];
        if (entry->state == 1 && entry->cache_kind == 1 &&
            entry->continuation_function_id == function_id &&
            entry->continuation_revision == function_revision &&
            entry->continuation_start_instruction == start_instruction)
            return entry;
    }
    return NULL;
}

const TurboJSNativeFunction *TurboJS_CodeCacheLookupContinuation(
    TurboJSCodeCache *cache, uint64_t function_id, uint64_t function_revision,
    size_t start_instruction, size_t *out_prologue_count)
{
    TurboJSCodeCacheEntry *entry = find_continuation_entry(
        cache, function_id, function_revision, start_instruction);
    if (!entry) {
        if (cache)
            cache->stats.continuation_misses++;
        return NULL;
    }
    touch_entry(cache, entry);
    cache->stats.continuation_hits++;
    if (out_prologue_count)
        *out_prologue_count = entry->continuation_prologue_count;
    return entry->function;
}

TurboJSIRStatus TurboJS_CodeCacheStoreContinuation(
    TurboJSCodeCache *cache, uint64_t function_id, uint64_t function_revision,
    size_t start_instruction, size_t prologue_count,
    TurboJSNativeFunction *native, const TurboJSNativeFunction **out_function)
{
    TurboJSCodeCacheEntry *existing;
    size_t code_size, slot;
    if (!cache || !native || !out_function || !function_id)
        return TURBOJS_IR_INVALID_ARGUMENT;
    existing = find_continuation_entry(cache, function_id, function_revision,
                                       start_instruction);
    if (existing) {
        TurboJS_NativeFunctionDestroy(native);
        touch_entry(cache, existing);
        *out_function = existing->function;
        return TURBOJS_IR_OK;
    }
    code_size = TurboJS_NativeCodeSize(native);
    if (code_size > cache->maximum_code_bytes) {
        TurboJS_NativeFunctionDestroy(native);
        return TURBOJS_IR_UNSUPPORTED;
    }
    while (cache->count &&
           (cache->count >= cache->maximum_entries ||
            code_size > cache->maximum_code_bytes - cache->code_bytes)) {
        if (!evict_lru(cache))
            break;
    }
    if (cache->count >= cache->maximum_entries ||
        code_size > cache->maximum_code_bytes - cache->code_bytes) {
        TurboJS_NativeFunctionDestroy(native);
        return TURBOJS_IR_UNSUPPORTED;
    }
    /* Continuation entries use a unique internal pointer key only to occupy a
     * normal Vault slot; lookup is by stable continuation identity above. */
    slot = SIZE_MAX;
    for (size_t i = 0; i < cache->slot_count; ++i) {
        if (cache->entries[i].state != 1) {
            slot = i;
            break;
        }
    }
    if (slot == SIZE_MAX) {
        TurboJS_NativeFunctionDestroy(native);
        return TURBOJS_IR_OUT_OF_MEMORY;
    }
    cache->entries[slot].key = NULL;
    cache->entries[slot].function = native;
    cache->entries[slot].code_size = code_size;
    cache->entries[slot].stamp = ++cache->clock;
    cache->entries[slot].access_count = 1;
    cache->entries[slot].cache_kind = 1;
    cache->entries[slot].continuation_function_id = function_id;
    cache->entries[slot].continuation_revision = function_revision;
    cache->entries[slot].continuation_start_instruction = start_instruction;
    cache->entries[slot].continuation_prologue_count = prologue_count;
    cache->entries[slot].state = 1;
    cache->count++;
    cache->code_bytes += code_size;
    if (!register_entry_dependencies(cache, &cache->entries[slot])) {
        remove_slot(cache, slot, 0);
        return TURBOJS_IR_OUT_OF_MEMORY;
    }
    cache->stats.continuation_compilations++;
    cache->stats.continuation_entry_count++;
    cache->stats.continuation_code_bytes += code_size;
    *out_function = native;
    return TURBOJS_IR_OK;
}
