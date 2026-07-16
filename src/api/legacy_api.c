/*
 * Legacy amalgamated-build compatibility shim.
 *
 * Normal builds use the subsystem manifest and generated private engine unit.
 * This path remains only for upstream tools that intentionally embed the
 * engine implementation in their own translation unit (fuzz/WASI utilities).
 */
#include "src/generated/turbojs_engine_unit.c"
