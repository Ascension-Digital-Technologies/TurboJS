# Public SDK headers

`include/turbojs/` is the complete supported C SDK surface. Headers in `src/` are private implementation details and must not be used by embedders.

- `turbojs.h` — core runtime and language API
- `turbojs_embed.h` — stable versioned embedding facade
- `turbojs-libc.h` — optional standard-library and host helpers
- `optimization.h` — optimization controls, telemetry, and pipeline identity
- `jit.h` — low-level JIT/AOT integration API
- `export.h` — shared-library visibility contract used by all public headers
