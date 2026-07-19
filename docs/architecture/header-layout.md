# Header layout

- `include/turbojs/` contains the supported embedding, runtime, optimization, and JIT API.
- `src/internal/` contains cross-subsystem private contracts and the private engine data model.
- Subsystem-local headers remain beside the implementation that owns them.

CMake and Meson expose only `include/turbojs/` to downstream consumers. `src/`, `src/internal/`, generated units, and backend headers are private build interfaces. Installation places the SDK under `<prefix>/include/turbojs/` while preserving the established includes such as `<turbojs.h>` and `<turbojs_embed.h>`.
