# Runtime continuation cache

TurboJS includes an LRU cache for native Spool continuation segments created after runtime-helper exits.

The cache key is the IR function's stable instance identity, mutation revision, and continuation instruction index. Cache entries own their native functions and are destroyed with the runtime-helper table or explicit cache clear.

The cache stores at most 16 continuation segments. On a miss, TurboJS compiles the segment once, records the prologue length used for Rewind mapping, and inserts it using least-recently-used replacement. On a hit, only the materialized register/local argument vector is rebuilt.

Public lifecycle APIs:

- `TurboJS_RuntimeHelperTableDestroy`
- `TurboJS_RuntimeHelperContinuationCacheClear`

Telemetry:

- `native_continuation_cache_hits`
- `native_continuation_cache_misses`
- `native_continuation_cache_evictions`
