#ifndef QJS_ENGINE_INTERNAL_H
#define QJS_ENGINE_INTERNAL_H

/*
 * Private umbrella header for the TurboJS engine implementation.
 *
 * Nothing in this directory is part of the embedding ABI. Public consumers
 * must include <turbojs.h> instead. The subsystem headers intentionally expose
 * ownership and dependency direction before declarations are migrated out of
 * the unity translation unit.
 */

#if !defined(TURBOJS_ENGINE_IMPLEMENTATION)
#error "qjs/internal headers are private to the engine implementation"
#endif

#include "jit.h"
#include "subsystem.h"
#include "platform.h"
#include "foundation_types.h"
#include "core.h"
#include "runtime.h"
#include "objects.h"
#include "gc.h"
#include "vm.h"
#include "compiler.h"
#include "modules.h"
#include "serialization.h"
#include "builtins.h"
#include "promise_bridge.h"
#include "exception_bridge.h"

#endif
