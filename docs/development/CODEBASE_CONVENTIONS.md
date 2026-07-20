# Codebase Conventions and Ownership

## Naming

Use TurboJS component names consistently. New implementation names should describe current ownership or function, not historical project lineage or migration phases. Public identifiers use the established `TurboJS`, `TURBOJS_`, and `JS` API conventions.

## Comments

Comments should explain invariants, ownership, non-obvious proofs, ABI requirements, and reasons for unusual performance-sensitive code. Do not preserve obsolete migration commentary or historical branding in implementation files. Required copyright, permission, generated-data, and third-party license notices must remain intact. Historical provenance belongs in project documentation and `NOTICE.md`.

## Headers

Installed headers expose supported contracts. Internal headers are private and should minimize transitive dependencies. Include the narrowest header that supplies the declaration used.

## Error handling

Check allocation and decoder boundaries. Preserve pending exceptions. Return explicit failure status across C boundaries. Cleanup paths must work after partial initialization.

## Performance-sensitive code

Prefer measured changes. Keep generic fallbacks. Document assumptions that are not obvious from types. Avoid hidden allocation in hot helpers unless it is part of the intended design.

## Ownership review

Any field storing a managed pointer/value must answer: who owns it, how it is rooted, when it is invalidated, and how it is released. Any compiled-code dependency must answer: what invalidates it and how active code remains safe during invalidation.
