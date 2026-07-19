# Generated sources

This directory contains checked-in, reproducible build products used by the engine, CLI, and source-amalgamation workflow.

- `turbojs_engine_unit.c` assembles manifest-owned engine domains.
- `runtime/` contains bytecode compiled from `apps/turbojs/scripts/`.
- `builtins/` contains bytecode compiled from `src/builtins/sources/`.

Do not hand-edit these files. Regenerate the engine unit with `python tools/generators/generate_engine_unit.py`; regenerate CLI bytecode with `python tools/generators/generate_runtime.py --compiler <path-to-turbojsc>`.

See the [repository layout](../docs/development/repository-layout.md) for ownership rules.
