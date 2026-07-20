# Public API and ABI Policy

TurboJS 1.0.0 distinguishes installed public interfaces from internal implementation headers.

## Public surfaces

- `include/turbojs/turbojs_embed.h`: narrow stable embedding table.
- `include/turbojs/turbojs.h`: full language runtime API.
- `include/turbojs/turbojs-libc.h`: optional runtime library helpers.
- `include/turbojs/jit.h`: compiler and JIT contracts for advanced integrations.

## Version fields

`TURBOJS_VERSION_*` identifies the engine release. `TURBOJS_API_VERSION` and `TURBOJS_ABI_VERSION` identify public runtime contracts. The stable embedding table has its own API and ABI versions. AOT, frame, bytecode, and module formats are versioned separately.

## Compatibility rules

- Additive structure growth uses size/version checks.
- Removing, reordering, or changing the meaning of ABI-visible fields requires an ABI increment.
- New enum values must not cause older callers to read uninitialized data.
- Ownership, lifetime, thread, error, and allocation behavior are part of an API contract.
- Internal symbols, generated files, and headers below `src/` carry no compatibility guarantee.

## Public API change requirements

Every change needs declaration documentation, implementation, positive and failure-path tests, an embedding example when applicable, release notes, and a compatibility assessment. Exported symbol changes must be checked on every supported build system and platform.
