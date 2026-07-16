#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "jit.h"

typedef struct TurboJSCodeCacheEntry {
    const void *key;
    TurboJSNativeFunction *function;
    size_t code_size;
    uint64_t stamp;
    uint8_t state; /* 0 empty, 1 live, 2 tombstone */
} TurboJSCodeCacheEntry;

struct TurboJSCodeCache {
    TurboJSCodeCacheEntry *entries;
    size_t slot_count;
    size_t count;
    size_t maximum_entries;
    size_t maximum_code_bytes;
    size_t code_bytes;
    uint64_t clock;
    TurboJSCodeCacheStats stats;
};

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

static void remove_slot(TurboJSCodeCache *cache, size_t slot, int eviction)
{
    TurboJSCodeCacheEntry *entry = &cache->entries[slot];
    if (entry->state != 1)
        return;
    cache->code_bytes -= entry->code_size;
    TurboJS_NativeFunctionDestroy(entry->function);
    entry->key = NULL;
    entry->function = NULL;
    entry->code_size = 0;
    entry->stamp = 0;
    entry->state = 2;
    cache->count--;
    if (eviction)
        cache->stats.evictions++;
}

static int evict_lru(TurboJSCodeCache *cache)
{
    size_t i;
    size_t victim = SIZE_MAX;
    uint64_t oldest = UINT64_MAX;
    for (i = 0; i < cache->slot_count; ++i) {
        const TurboJSCodeCacheEntry *entry = &cache->entries[i];
        if (entry->state == 1 && entry->stamp < oldest) {
            oldest = entry->stamp;
            victim = i;
        }
    }
    if (victim == SIZE_MAX)
        return 0;
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
    if (!cache->entries) {
        free(cache);
        return NULL;
    }
    cache->slot_count = slots;
    cache->maximum_entries = maximum_entries;
    cache->maximum_code_bytes = maximum_code_bytes;
    return cache;
}

void TurboJS_CodeCacheDestroy(TurboJSCodeCache *cache)
{
    if (!cache)
        return;
    TurboJS_CodeCacheClear(cache);
    free(cache->entries);
    free(cache);
}

const TurboJSNativeFunction *TurboJS_CodeCacheLookup(TurboJSCodeCache *cache,
                                                     const void *key)
{
    size_t slot;
    if (!cache || !key)
        return NULL;
    slot = find_slot(cache, key, 0);
    if (slot == SIZE_MAX || cache->entries[slot].state != 1) {
        cache->stats.misses++;
        return NULL;
    }
    cache->stats.hits++;
    cache->entries[slot].stamp = ++cache->clock;
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
        cache->entries[slot].stamp = ++cache->clock;
        *out_function = cache->entries[slot].function;
        return TURBOJS_IR_OK;
    }
    status = TurboJS_BaselineCompile(ir, &native, diagnostic);
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
    cache->entries[slot].state = 1;
    cache->count++;
    cache->code_bytes += code_size;
    cache->stats.compilations++;
    *out_function = native;
    return TURBOJS_IR_OK;
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
            TurboJS_NativeFunctionDestroy(cache->entries[i].function);
    }
    memset(cache->entries, 0, cache->slot_count * sizeof(*cache->entries));
    cache->count = 0;
    cache->code_bytes = 0;
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
