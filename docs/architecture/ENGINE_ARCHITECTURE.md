# Engine Architecture

TurboJS is a compact JavaScript engine organized around a bytecode VM and multiple execution tiers. The architecture keeps language semantics in the runtime while allowing increasingly specialized execution paths to accelerate hot code without changing observable results.

## End-to-end flow

```text
JavaScript source
  -> lexer and parser
  -> bytecode function + constants + metadata
  -> bytecode interpreter
  -> feedback and hotness telemetry
  -> baseline native lowering
  -> optimizing SSA and application-region compilation
  -> native execution
  -> safepoint, helper call, OSR, or deoptimization
  -> interpreter-compatible state
```

## Architectural layers

1. **Host interface.** Public headers, the stable embedding table, CLI applications, and runtime library integration.
2. **Language frontend.** Tokenization, parsing, scope construction, bytecode emission, source metadata, and module/eval entry points.
3. **Semantic runtime.** Tagged values, objects, arrays, functions, exceptions, built-ins, modules, strings, numeric behavior, RegExp, and Unicode.
4. **Virtual machine.** Bytecode dispatch, frames, calls, property operations, feedback collection, exception transfer, and tiering hooks.
5. **Native execution.** Backend-neutral IR, baseline lowering, SSA optimization, machine-code backends, executable memory, call stubs, stack maps, and safepoints.
6. **Persistence.** Bytecode serialization, portable optimizing IR, module containers, generated built-ins, and inspection tools.

## Correctness principle

The interpreter is the semantic reference inside the engine. Native tiers may specialize only when they can prove or guard their assumptions. A failed guard does not invent a new result: it exits through a defined bailout, reconstructs a valid frame, and resumes in a semantically compatible execution path.

## Ownership boundaries

- `src/core`, `src/objects`, `src/builtins`, and `src/numeric` own language-visible semantics.
- `src/vm` owns bytecode execution and feedback integration.
- `src/jit/frontend` translates bytecode state into compiler state.
- `src/jit/ir` defines backend-neutral operations and verification.
- `src/jit/optimizing` owns SSA transformations and specialization policy.
- `src/jit/backend` owns target instruction selection and encoding.
- `src/jit/runtime` owns transitions between managed JavaScript state and native code.
- `src/gc` owns reachability and reclamation; every subsystem that holds managed values must obey its rooting rules.

## Generated unity engine

Tightly coupled semantic sources are assembled into `generated/turbojs_engine_unit.c`. The source files remain organized by subsystem, while generation preserves the compilation characteristics of a unity engine. Never edit the generated unit directly; update its source fragments or generator and regenerate it.

## Key invariants

- A `JSValue` is interpreted only according to its tag and ownership rules.
- Managed references visible across allocation or helper calls must be rooted.
- Native code can return to managed execution only through declared continuation and frame contracts.
- Serialized data is rejected before use when magic, size, version, or structural validation fails.
- Optimization is optional; disabling a tier must preserve program semantics.
