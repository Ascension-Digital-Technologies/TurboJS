# Embedding TurboJS

TurboJS exposes the public C headers under `include/turbojs/`. Applications should include
`turbojs.h`, link to the installed `turbojs` target, and treat all headers under
`src/internal/` as private implementation details.

## Lifecycle

1. Create a runtime with `JS_NewRuntime()`.
2. Create one or more contexts with `JS_NewContext()`.
3. Evaluate source or bytecode through the public API.
4. Release owned `JSValue` instances with the matching context.
5. Free contexts before freeing their runtime.

Public API version: `TURBOJS_API_VERSION`.
Public ABI version: `TURBOJS_ABI_VERSION`.

The release candidate keeps both values at `1`. ABI stability applies only to
installed public headers and exported symbols, not internal structures.
