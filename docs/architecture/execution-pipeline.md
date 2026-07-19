# TurboJS execution pipeline

TurboJS uses stable codenames for its major execution and optimization components. The names are diagnostic identities, not replacements for generic engineering terminology. Public APIs expose both forms.

| Codename | Engineering role | Responsibility |
|---|---|---|
| **Rotor** | Bytecode frontend | Parses source and emits validated TurboJS bytecode. |
| **Pulse** | Interpreter | Runs cold code and preserves canonical JavaScript semantics. |
| **Spool** | Baseline JIT | Quickly compiles broad bytecode coverage with interpreter-compatible frames. |
| **Redline** | Optimizing JIT | Builds feedback-specialized SSA and high-performance native code. |
| **Forge** | AOT compiler | Produces portable or native modules outside the hot execution path. |
| **Telemetry** | Feedback system | Records value kinds, shapes, and call-target behavior during bounded warm-up windows. |
| **Relay** | Inline caches | Hosts patchable property, element, and call-site fast paths. |
| **Clutch** | Compiled call ABI | Publishes generation-checked native entries and governs compiled call boundaries. |
| **Slipstream** | OSR and tier transitions | Transfers live frames among Pulse, Spool, and Redline. |
| **Rewind** | Deoptimization | Reconstructs a lower-tier frame after speculative failure. |
| **Gearbox** | Machine backend | Performs lowering, register allocation, and architecture emission. |
| **Vault** | Native code cache | Owns executable memory, lookup, aging, and invalidation. |

## Execution flow

```text
JavaScript source
      |
    Rotor
      |
    Pulse --hot--> Spool --stable feedback--> Redline
      |                ^                         |
      +------ Rewind ---+------ Slipstream ------+

Telemetry feeds Relay, Spool, and Redline. Clutch connects guarded Relay call sites to versioned Spool entries owned by Vault. Gearbox emits native code into Vault. Forge reuses the same verified IR and backend contracts for AOT output.
```

## Naming rules

- Generic names remain in source-level APIs where clarity matters.
- Codenames are used in profiler output, diagnostics, architecture documents, and component ownership comments.
- A codename must identify a real architectural boundary with tests and ownership.
- New names are not introduced for individual optimization passes unless they become independent subsystems.

Telemetry call-target collection is budgeted per caller and freezes after warm-up. The retained feedback remains available to Relay and Redline without imposing perpetual profiling work on long-running call sites.

Redline and its VM call-target collection are compile-gated by `TURBOJS_ENABLE_OPTIMIZING_JIT`. Default builds therefore retain zero call-site instrumentation overhead until the optimizing tier is explicitly enabled.


## Relay and Clutch call-entry policy

Relay has three call dispatch classes:

- tiny affine or decoded leaf executors;
- generation-checked Spool native entries published through Clutch;
- canonical Pulse fallback for unsupported, stale, or polymorphic sites.

A Relay call entry never owns a callee object or an unversioned executable pointer. For a Spool target it retains the stable numeric function identity plus the Clutch generation expected from the function-owned entry handle. Vault invalidates that handle before executable memory is released. A generation mismatch clears the Relay entry and resumes canonical dispatch, allowing safe recompilation and reinstallation.

Validated bytecode-to-bytecode fallbacks use a direct Pulse entry flag. The caller has already resolved the function class and attempted Relay, Spool, and region dispatch, so the callee avoids repeated generic callable lookup and duplicate JIT probes.

The present Clutch edge accelerates Pulse/Relay-to-Spool calls for supported numeric functions. A native Spool caller still needs explicit call LIR, call-clobber modeling, safepoints, and argument/environment ABI lowering before TurboJS can claim fully compiled-to-compiled JavaScript calls.
