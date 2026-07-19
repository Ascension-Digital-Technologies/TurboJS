# Beacon live function identity registry

Beacon is TurboJS's runtime-owned weak registry for resolving Telemetry's numeric function identities back to currently live `JSFunctionBytecode` objects.

## Safety contract

- Telemetry continues to store only numeric identities.
- A bytecode object enters Beacon only after receiving an identity.
- `free_function_bytecode()` unlinks the object before Vault invalidation and object destruction.
- Resolver lookups never retain the returned pointer beyond the synchronous compilation attempt.
- Clutch and Vault generation checks remain the authority for executable-code validity.

## Runtime integration

Spool's VM bytecode compiler now provides a Telemetry-backed call resolver. It accepts a call edge only when the site is monomorphic, Beacon still resolves the target, argument counts match, the target has no captured local references, and a matching live Spool entry is published. Otherwise compilation returns to Pulse without speculating.

The current frontend still rejects many ordinary callee-producing bytecodes, such as dynamic global and closure loads. Beacon removes the lifetime blocker, but broader frontend coverage is still required before most JavaScript callers can become fully native.
