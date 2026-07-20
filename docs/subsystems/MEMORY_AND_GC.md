# Memory Management and Garbage Collection

TurboJS is designed for controlled embedding: hosts can set memory limits, create and destroy isolated engine instances, request collection, and receive errors at host boundaries.

## Ownership classes

- Host-owned buffers and configuration remain owned by the caller unless documented otherwise.
- Engine allocations belong to a runtime, context, or compiled-code owner.
- Reference-counted values require balanced retain/release operations.
- Cyclic object graphs require tracing or cycle collection beyond local reference counts.
- Native code and compiler metadata that retain managed values must register roots or equivalent ownership.

## Allocation boundaries

Any operation that can allocate may trigger collection or failure. Live managed values stored only in unreported native locations are unsafe across such a boundary. JIT stack maps, rooted helper calls, boxed deoptimization state, and continuation records exist to make those values visible.

## Embedding limits

`TurboJSEmbedConfig` exposes bounded memory and stack configuration for the stable embedding API. Limits are enforcement tools, not substitutes for host timeouts or process isolation when running untrusted code.

## Failure behavior

Out-of-memory conditions must become a controlled engine exception or host-visible status where possible. Code must not continue with partially initialized managed objects. Cleanup paths must tolerate construction failure and repeated lifecycle stress.

## Validation

Use runtime lifecycle stress, deterministic low-memory configurations, sanitizer builds, GC-safe deoptimization tests, stack-map tests, and workload-specific leak checks. Any subsystem introducing a long-lived cache must document ownership, invalidation, and reclamation.
