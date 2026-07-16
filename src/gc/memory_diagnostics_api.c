/*
 * Public memory diagnostics facade.
 *
 * These API entry points compile independently from the engine core. The
 * implementation remains behind a narrow private bridge until GC/object
 * layouts are promoted into shared internal headers.
 */

#include <turbojs.h>
#include "internal/diagnostics_bridge.h"

void JS_ComputeMemoryUsage(JSRuntime *rt, JSMemoryUsage *usage)
{
    turbojs_internal_compute_memory_usage(rt, usage);
}

void JS_DumpMemoryUsage(FILE *fp, const JSMemoryUsage *usage, JSRuntime *rt)
{
    turbojs_internal_dump_memory_usage(fp, usage, rt);
}
