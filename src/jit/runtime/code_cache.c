#include <stdlib.h>
#include <string.h>
#include "jit.h"

typedef struct TurboJSCodeCacheEntry {
    const void *key;
    TurboJSNativeFunction *function;
    size_t code_size;
    uint64_t stamp;
} TurboJSCodeCacheEntry;

struct TurboJSCodeCache {
    TurboJSCodeCacheEntry *entries;
    size_t count;
    size_t capacity;
    size_t maximum_entries;
    size_t maximum_code_bytes;
    size_t code_bytes;
    uint64_t clock;
    TurboJSCodeCacheStats stats;
};

static size_t find_entry(const TurboJSCodeCache *cache, const void *key)
{
    size_t i;
    for (i = 0; i < cache->count; ++i)
        if (cache->entries[i].key == key)
            return i;
    return SIZE_MAX;
}

static void remove_entry(TurboJSCodeCache *cache, size_t index, int eviction)
{
    TurboJSCodeCacheEntry *entry = &cache->entries[index];
    cache->code_bytes -= entry->code_size;
    TurboJS_NativeFunctionDestroy(entry->function);
    if (index + 1 < cache->count)
        memmove(entry, entry + 1, (cache->count - index - 1) * sizeof(*entry));
    cache->count--;
    if (eviction)
        cache->stats.evictions++;
}

static void evict_lru(TurboJSCodeCache *cache)
{
    size_t i, victim = 0;
    for (i = 1; i < cache->count; ++i)
        if (cache->entries[i].stamp < cache->entries[victim].stamp)
            victim = i;
    remove_entry(cache, victim, 1);
}

TurboJSCodeCache *TurboJS_CodeCacheCreate(size_t maximum_entries,
                                          size_t maximum_code_bytes)
{
    TurboJSCodeCache *cache;
    if (maximum_entries == 0)
        maximum_entries = 64;
    if (maximum_code_bytes == 0)
        maximum_code_bytes = 4u * 1024u * 1024u;
    cache = (TurboJSCodeCache *)calloc(1, sizeof(*cache));
    if (!cache)
        return NULL;
    cache->maximum_entries = maximum_entries;
    cache->maximum_code_bytes = maximum_code_bytes;
    return cache;
}

void TurboJS_CodeCacheDestroy(TurboJSCodeCache *cache)
{
    if (!cache) return;
    TurboJS_CodeCacheClear(cache);
    free(cache->entries);
    free(cache);
}

const TurboJSNativeFunction *TurboJS_CodeCacheLookup(TurboJSCodeCache *cache,
                                                     const void *key)
{
    size_t index;
    if (!cache || !key) return NULL;
    index = find_entry(cache, key);
    if (index == SIZE_MAX) {
        cache->stats.misses++;
        return NULL;
    }
    cache->stats.hits++;
    cache->entries[index].stamp = ++cache->clock;
    return cache->entries[index].function;
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
    TurboJSCodeCacheEntry *grown;
    size_t existing;
    if (!cache || !key || !ir || !out_function)
        return TURBOJS_IR_INVALID_ARGUMENT;
    existing = find_entry(cache, key);
    if (existing != SIZE_MAX) {
        cache->entries[existing].stamp = ++cache->clock;
        *out_function = cache->entries[existing].function;
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
    while (cache->count && (cache->count >= cache->maximum_entries ||
           cache->code_bytes + code_size > cache->maximum_code_bytes))
        evict_lru(cache);
    if (cache->count == cache->capacity) {
        size_t next = cache->capacity ? cache->capacity * 2 : 8;
        if (next > cache->maximum_entries) next = cache->maximum_entries;
        grown = (TurboJSCodeCacheEntry *)realloc(cache->entries, next * sizeof(*grown));
        if (!grown) {
            TurboJS_NativeFunctionDestroy(native);
            return TURBOJS_IR_OUT_OF_MEMORY;
        }
        cache->entries = grown;
        cache->capacity = next;
    }
    cache->entries[cache->count].key = key;
    cache->entries[cache->count].function = native;
    cache->entries[cache->count].code_size = code_size;
    cache->entries[cache->count].stamp = ++cache->clock;
    cache->count++;
    cache->code_bytes += code_size;
    cache->stats.compilations++;
    *out_function = native;
    return TURBOJS_IR_OK;
}

void TurboJS_CodeCacheInvalidate(TurboJSCodeCache *cache, const void *key)
{
    size_t index;
    if (!cache || !key) return;
    index = find_entry(cache, key);
    if (index != SIZE_MAX) remove_entry(cache, index, 0);
}

void TurboJS_CodeCacheClear(TurboJSCodeCache *cache)
{
    if (!cache) return;
    while (cache->count) remove_entry(cache, cache->count - 1, 0);
}

TurboJSCodeCacheStats TurboJS_CodeCacheGetStats(const TurboJSCodeCache *cache)
{
    TurboJSCodeCacheStats stats;
    memset(&stats, 0, sizeof(stats));
    if (!cache) return stats;
    stats = cache->stats;
    stats.entry_count = cache->count;
    stats.code_bytes = cache->code_bytes;
    return stats;
}
