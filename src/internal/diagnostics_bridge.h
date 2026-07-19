#ifndef TURBOJS_INTERNAL_DIAGNOSTICS_BRIDGE_H
#define TURBOJS_INTERNAL_DIAGNOSTICS_BRIDGE_H

/*
 * Private bridge for independently compiled diagnostics and memory-reporting
 * APIs. The facade sees only public opaque runtime types and JSMemoryUsage.
 */

#include <turbojs.h>

void turbojs_internal_compute_memory_usage(JSRuntime *rt, JSMemoryUsage *usage);
void turbojs_internal_dump_memory_usage(FILE *fp,
                                    const JSMemoryUsage *usage,
                                    JSRuntime *rt);

#endif
