# TurboJS source layout

The execution pipeline is organized by ownership rather than by compilation phase accidents.

- `src/jit/frontend/` — Rotor-to-Spool bytecode analysis, frame states, CFG, and lowering.
- `src/jit/ir/` — portable IR, tagged semantics, verification, and deoptimization boxing.
- `src/jit/runtime/` — Telemetry, Vault support, tiering, OSR, frame ABI, and helper continuation.
- `src/jit/runtime/helpers/` — helper registration, rooted invocation, and continuation execution.
- `src/jit/optimizing/` — Redline SSA, allocation, and optimization pipeline.
- `src/jit/backend/x64/` and `arm64/` — Gearbox target lowering.
- `src/jit/aot/` — Forge portable-module support.

The grouped source manifest lives in `cmake/TurboJSJITSources.cmake`. The root `CMakeLists.txt` owns targets and policy; it does not enumerate every JIT translation unit.
