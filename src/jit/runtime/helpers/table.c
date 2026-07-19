#include <string.h>

#include "internal.h"

void TurboJS_RuntimeHelperTableInit(TurboJSRuntimeHelperTable *table,
                                    const TurboJSRootingHooks *rooting)
{
    if (!table)
        return;
    memset(table, 0, sizeof(*table));
    if (rooting)
        table->rooting = *rooting;
}

TurboJSIRStatus TurboJS_RuntimeHelperRegister(TurboJSRuntimeHelperTable *table,
                                              uint16_t helper_id,
                                              TurboJSRuntimeHelperCallback callback,
                                              void *opaque)
{
    if (!table || !callback || helper_id >= TURBOJS_RUNTIME_HELPER_LIMIT)
        return TURBOJS_IR_INVALID_ARGUMENT;
    table->entries[helper_id].callback = callback;
    table->entries[helper_id].opaque = opaque;
    return TURBOJS_IR_OK;
}

void TurboJS_RuntimeHelperUnregister(TurboJSRuntimeHelperTable *table,
                                     uint16_t helper_id)
{
    if (!table || helper_id >= TURBOJS_RUNTIME_HELPER_LIMIT)
        return;
    memset(&table->entries[helper_id], 0, sizeof(table->entries[helper_id]));
}

void TurboJS_RuntimeHelperTableDestroy(TurboJSRuntimeHelperTable *table)
{
    if (!table)
        return;
    TurboJS_RuntimeContinuationCacheDestroy(table);
    memset(table, 0, sizeof(*table));
}

void TurboJS_RuntimeHelperContinuationCacheClear(TurboJSRuntimeHelperTable *table)
{
    TurboJS_RuntimeContinuationCacheDestroy(table);
}

void TurboJS_RuntimeHelperAttachContinuationVault(
    TurboJSRuntimeHelperTable *table, TurboJSCodeCache *vault)
{
    if (!table)
        return;
    TurboJS_RuntimeContinuationCacheDestroy(table);
    table->continuation_vault = vault;
}
