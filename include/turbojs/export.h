#ifndef TURBOJS_EXPORT_H
#define TURBOJS_EXPORT_H

/* Shared-library visibility for the public TurboJS C interfaces. */
#if defined(_WIN32) || defined(__CYGWIN__)
# define TURBOJS_PLATFORM_WINDOWS 1
#endif

#if defined(__GNUC__) || defined(__clang__)
# define TURBOJS_COMPILER_GNULIKE 1
#endif

#if defined(TURBOJS_PLATFORM_WINDOWS)
# if defined(BUILDING_TURBOJS_SHARED)
#  define JS_EXTERN __declspec(dllexport)
# elif defined(USING_TURBOJS_SHARED)
#  define JS_EXTERN __declspec(dllimport)
# else
#  define JS_EXTERN
# endif
#else
# if defined(BUILDING_TURBOJS_SHARED) && defined(TURBOJS_COMPILER_GNULIKE)
#  define JS_EXTERN __attribute__((visibility("default")))
# else
#  define JS_EXTERN
# endif
#endif

#if defined(TURBOJS_BUILD) && !defined(TURBOJS_BUILD_LIBC) && \
    defined(TURBOJS_PLATFORM_WINDOWS)
# define JS_LIBC_EXTERN
#else
# define JS_LIBC_EXTERN JS_EXTERN
#endif

#if defined(TURBOJS_PLATFORM_WINDOWS)
# if defined(TURBOJS_MODULE_BUILD)
#  define JS_MODULE_EXTERN __declspec(dllexport)
# else
#  define JS_MODULE_EXTERN __declspec(dllimport)
# endif
#else
# if defined(TURBOJS_MODULE_BUILD) && defined(TURBOJS_COMPILER_GNULIKE)
#  define JS_MODULE_EXTERN __attribute__((visibility("default")))
# else
#  define JS_MODULE_EXTERN
# endif
#endif

#endif
