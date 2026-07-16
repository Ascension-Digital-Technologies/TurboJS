# Header Layout

TurboJS keeps headers next to the layer that owns them.

- `src/api/` contains the supported public embedding, runtime, and JIT API.
- `src/internal/` contains private contracts shared by engine subsystems.
- Subsystem-specific headers remain beside their implementations under `src/<subsystem>/`.

Build targets expose only `src/api` publicly. `src` and `src/internal` are private build interfaces. Installation places supported SDK headers under `<prefix>/include/turbojs/`; the source tree has no generic include directory and no compatibility-header layer.
