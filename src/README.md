# Engine source tree

This directory contains production engine code only. Repository tooling belongs in `tools/`, workflow wrappers in `scripts/`, and tests in `tests/`. Public API is isolated in `api/`; cross-subsystem private contracts are isolated in `internal/`. Generated files must be reproducible and remain under `generated/`.

## Rules

Do not add catch-all `support`, `utils`, `common`, or `tools` directories here. Place code in the subsystem that owns its lifetime and invariants.

## Related documentation

Start with the [repository README](../README.md) and `docs/architecture/` for detailed design notes.
