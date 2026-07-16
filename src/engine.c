/*
 * TurboJS engine assembly coordinator.
 *
 * Implementation ownership lives in subsystem .c files. The build generates
 * one private compilation unit to preserve TurboJS's performance-sensitive
 * static linkage until those internal contracts are independently linkable.
 */
#include "internal/engine_internal.h"

const char *turbojs_engine_layout_mode(void)
{
    return "generated-subsystem-amalgamation-v1";
}
