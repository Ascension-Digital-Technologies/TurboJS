# Public API

Supported embedding headers and API-level implementation. Changes here affect embedders and require documentation, lifecycle tests, and compatibility review. `legacy_api.c` centralizes the remaining API adaptation implementation without restoring compatibility headers.

## Rules

Do not expose JIT backend structures, GC internals, or parser state through this directory.

## Related documentation

Start with the [repository README](../../README.md) and `docs/architecture/` for detailed design notes.
