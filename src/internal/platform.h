#ifndef QJS_INTERNAL_PLATFORM_H
#define QJS_INTERNAL_PLATFORM_H

/*
 * Platform, compiler, and build-policy boundary for the engine.
 * Private and ABI-unstable. No engine data structures are declared here.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#if !defined(_MSC_VER)
#include <sys/time.h>
#if defined(_WIN32)
#include <timezoneapi.h>
#endif
#endif
#if defined(_WIN32)
#include <intrin.h>
#endif
#include <time.h>
#include <math.h>

#include "internal/cutils.h"
#include "internal/list.h"
#include <turbojs.h>
#include "src/regexp/regexp.h"
#include "src/numeric/dtoa.h"

#if defined(EMSCRIPTEN) || defined(_MSC_VER)
#define DIRECT_DISPATCH  0
#else
#define DIRECT_DISPATCH  1
#endif

#if defined(__APPLE__)
#define MALLOC_OVERHEAD  0
#else
#define MALLOC_OVERHEAD  8
#endif

#if defined(__NEWLIB__)
#define NO_TM_GMTOFF
#endif

#if defined(__sun)
#include <alloca.h>
#define NO_TM_GMTOFF
#endif

// atomic_store etc. are completely busted in recent versions of tcc;
// somehow the compiler forgets to load |ptr| into %rdi when calling
// the __atomic_*() helpers in its lib/stdatomic.c and lib/atomic.S
#if !defined(__TINYC__) && !defined(EMSCRIPTEN) && !defined(__wasi__) && !__STDC_NO_ATOMICS__ && !defined(__DJGPP)
#include "atomics_compat.h"
#define CONFIG_ATOMICS
#endif

#ifndef __GNUC__
#define __extension__
#endif

#ifndef NDEBUG
#define ENABLE_DUMPS
#endif

//#define FORCE_GC_AT_MALLOC  /* test the GC by forcing it before each object allocation */

#define check_dump_flag(rt, flag)  ((rt->dump_flags & (flag +0)) == (flag +0))


#endif
