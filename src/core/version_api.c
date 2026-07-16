/* Core version API: independently compiled and free of engine-private state. */
#include <turbojs.h>

#define QJS_STRINGIFY_INNER(x) #x
#define QJS_STRINGIFY(x) QJS_STRINGIFY_INNER(x)
#define QJS_VERSION_TEXT \
    QJS_STRINGIFY(QJS_VERSION_MAJOR) "." \
    QJS_STRINGIFY(QJS_VERSION_MINOR) "." \
    QJS_STRINGIFY(QJS_VERSION_PATCH) QJS_VERSION_SUFFIX

const char *JS_GetVersion(void)
{
    return QJS_VERSION_TEXT;
}
