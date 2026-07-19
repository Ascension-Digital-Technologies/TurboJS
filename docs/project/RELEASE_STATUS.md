# Release Status

TurboJS `0.16.0-rc.6` is a release candidate focused on compact embedding, fast startup, low runtime overhead, and progressively optimized sustained execution.

## Current validation

- 293 full-release build targets complete successfully.
- 97 native tests pass.
- Windows builds support LLVM/Clang and the LLVM resource compiler through the included PowerShell launcher.
- Linux and macOS use the shared CMake and Python build workflows.
- x86-64 includes runtime-safe SIMD feature detection; ARM64 backend development remains in progress.

## Included capabilities

- Parser, bytecode compiler, interpreter, and command-line runtime.
- Baseline and feedback-directed optimizing compilation.
- Inline caches, compiled-call entries, OSR, deoptimization, and native code caching.
- Shared application-region optimization for numeric, collection, callback, and object-graph workloads.
- Portable and native AOT infrastructure.
- Stable C embedding API and native integration examples.

## Remaining release work

- Broader Test262 compatibility and published compatibility qualification.
- Production ARM64 code generation and expanded architecture testing.
- Additional GC/JIT stress coverage and long-duration lifecycle validation.
- Reproducible startup, memory, binary-size, and sustained-performance reports.
- Final API review, packaging, signing, and release artifact verification.
