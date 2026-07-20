# JIT Compilation Pipeline

TurboJS separates optimization policy, bytecode analysis, IR construction, specialization, target lowering, and runtime installation. This separation keeps correctness contracts testable and permits native tiers to evolve without embedding parser internals into machine backends.

## Tiers

1. **Interpreter:** universal semantic path and feedback producer.
2. **Spool baseline tier:** lowers bytecode and tagged frame state to native control flow with runtime exits.
3. **Redline optimizing tier:** constructs SSA, specializes values and memory, applies range and alias reasoning, and emits guarded optimized code.
4. **Application regions:** recognize larger stable workloads and compile closed or shared native regions when profitable.
5. **Forge AOT:** persists portable optimizer input and module metadata for later validation and installation.

## Frontend

The bytecode frontend discovers blocks, stack states, locals, call sites, exception edges, and merge states. Unsupported or unsafe patterns must fail compilation cleanly and leave interpreter execution available.

## IR and verification

IR instructions carry explicit operands, result types, control-flow edges, side effects, and deoptimization meaning. Verification runs before code generation and rejects malformed graphs. Optimizations must preserve dominance, use-def, memory-version, and frame-state invariants.

## Guards and deoptimization

Specialization creates assumptions about tags, ranges, shapes, identities, bounds, or call targets. Guards validate those assumptions. Failure transfers to a precise bailout record, boxes native values as needed, reconstructs the JavaScript frame, and resumes at a defined continuation.

## OSR

On-stack replacement enters native code from a hot interpreter loop. Entry mapping must translate current locals and stack slots into the native frame layout. Re-entry and negative-cache policy prevent repeated expensive attempts for unsuitable sites.

## Code ownership

Installed code is associated with function identity, dependencies, entry handles, and invalidation state. Repatching is selective where possible. Executable memory lifecycle must be synchronized with active entries and runtime teardown.

## Adding an optimization

Document the proof, add IR verification, add positive and negative unit tests, add randomized or differential coverage, add deoptimization coverage, confirm GC root visibility, measure compile-time and code-size cost, then benchmark only after correctness gates pass.
