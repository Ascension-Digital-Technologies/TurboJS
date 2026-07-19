#ifndef TURBOJS_EMBED_H
#define TURBOJS_EMBED_H

#include <stddef.h>
#include <stdint.h>

#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TURBOJS_EMBED_API_VERSION 1u
#define TURBOJS_EMBED_ABI_VERSION 1u

typedef struct TurboJSEngine TurboJSEngine;

typedef enum TurboJSEmbedStatus {
    TURBOJS_EMBED_OK = 0,
    TURBOJS_EMBED_INVALID_ARGUMENT = 1,
    TURBOJS_EMBED_OUT_OF_MEMORY = 2,
    TURBOJS_EMBED_EXCEPTION = 3,
    TURBOJS_EMBED_CONVERSION_ERROR = 4
} TurboJSEmbedStatus;

typedef struct TurboJSEmbedConfig {
    uint32_t struct_size;
    uint32_t api_version;
    size_t memory_limit_bytes;
    size_t max_stack_bytes;
} TurboJSEmbedConfig;

typedef struct TurboJSEmbedAPI {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t abi_version;
    TurboJSEngine *(*create)(const TurboJSEmbedConfig *config);
    void (*destroy)(TurboJSEngine *engine);
    TurboJSEmbedStatus (*eval_i64)(TurboJSEngine *engine,
                                   const char *source,
                                   size_t source_length,
                                   int64_t *result);
    const char *(*last_error)(const TurboJSEngine *engine);
    void (*collect_garbage)(TurboJSEngine *engine);
} TurboJSEmbedAPI;

/* Returns a versioned function table. Callers must pass the exact ABI version
 * they were compiled against and validate struct_size before dereferencing
 * fields added by future releases. */
JS_EXTERN const TurboJSEmbedAPI *TurboJS_GetEmbedAPI(uint32_t abi_version);

#ifdef __cplusplus
}
#endif
#endif
