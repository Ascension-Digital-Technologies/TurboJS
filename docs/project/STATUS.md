# Project Status

## Current development stage

TurboJS is an advanced engineering preview of a compact JavaScript engine. It is suitable for compiler development, benchmarking, experimentation, and controlled embedding. It is not yet presented as a drop-in replacement for V8 across arbitrary production workloads.

## Implemented

- JavaScript parser, bytecode compiler, interpreter, runtime, and standard-library foundation
- x86-64 baseline JIT
- SSA and CFG infrastructure
- Loop analysis, phi nodes, interval allocation, spills, edge moves, and deoptimization metadata
- OSR for several scalar, object-property, and dense-array workload families
- Guarded object/property feedback infrastructure
- Stack maps, safepoints, and runtime-helper contracts
- Portable AOT images and module validation
- Windows and System V x86-64 support paths
- Cross-platform CMake workflow
- Focused JIT and engine regression suites
- Test262 runner with resumable and unattended Windows execution

## Current performance scope

The Phase 99 tree adds redundant Int32 guard elimination, deterministic randomized optimizer differential testing, and AddressSanitizer-clean focused validation. The Phase 98 representation inference, local common-subexpression elimination, and tested AArch64 encoder foundation remain included. The current tree also supports sustained conditional Int32 loops using signed relational/equality predicates and bit-mask induction predicates. Phase 97 adds guarded dynamic-bound packed-array initialization using arguments, locals, and closure references, while retaining Phase 96 multi-accumulator scalar CFG support. The two-accumulator sustained workload completes in 0.000551 ms versus 21.873364 ms for Node/V8 because the loop is eliminated analytically. Non-reducible dependent branches, nested control flow, mixed numeric state, and general calls remain targets for the shared region compiler.

Current performance work is focused on replacing structural recognizers with one general region compiler.

## Known limitations

- Optimized coverage remains uneven across arbitrary JavaScript programs
- Several OSR paths still depend on recognizable loop families
- General calls, objects, arrays, strings, and exceptions are not uniformly optimized through one SSA pipeline
- ARM64 native code generation is not complete
- Full native AOT object emission is not complete
- GC/JIT interaction needs broader stress and fuzz validation
- Test262 still has concentrated semantic gaps
- A final project license and upstream provenance package must be completed before a public production release

## Release policy

A production release requires:

1. Warning-clean Release builds on supported platforms
2. Focused and full-engine test suites passing
3. Sanitizer and GC-stress validation
4. Published Test262 revision and results
5. Sustained multi-workload benchmark report
6. Reproducible source and binary archives
7. Completed licensing and provenance review
8. Versioned API and ABI policy

See [`ROADMAP.md`](ROADMAP.md) for the completion plan.

## Phase 99 hardening status

The shared optimizer now infers Int32, Int64, Boolean, Float64, and reference representations where proven, eliminates duplicate pure expressions within a block, and removes duplicate same-block Int32 guards. A deterministic 1,000-program stress test compares optimized and unoptimized SSA results, and both Release and AddressSanitizer focused suites pass 36/36 tests. The ARM64 layer currently provides tested instruction encoding only; executable region lowering remains unfinished. The scalar OSR path supports relational and masked-induction analytical reduction, multiple live accumulators across branch joins, ordered dependent Int32 accumulators, partial extraction, and independent closed-form recurrences. The packed dense-array initializer now resolves runtime bounds through the shared local/argument/closure slot abstraction and validates them before one-shot expansion and direct fill. The pinned core Test262 baseline remains 42,732 pass / 2,237 fail / 18 timeout / 8,711 skip at revision `9e61c128`.
