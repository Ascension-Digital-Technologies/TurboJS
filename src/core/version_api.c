/* Core version API: independently compiled and free of engine-private state. */
#include <turbojs.h>

#define TURBOJS_STRINGIFY_INNER(x) #x
#define TURBOJS_STRINGIFY(x) TURBOJS_STRINGIFY_INNER(x)
#define TURBOJS_VERSION_TEXT \
    TURBOJS_STRINGIFY(TURBOJS_VERSION_MAJOR) "." \
    TURBOJS_STRINGIFY(TURBOJS_VERSION_MINOR) "." \
    TURBOJS_STRINGIFY(TURBOJS_VERSION_PATCH) TURBOJS_VERSION_SUFFIX

const char *JS_GetVersion(void)
{
    return TURBOJS_VERSION_TEXT;
}
