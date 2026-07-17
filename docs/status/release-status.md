# TurboJS JIT/AOT v1 completion status

This tree completes the planned lightweight JIT/AOT v1 architecture for the currently supported numeric and runtime-helper subset.

## Completed

- Interpreter, baseline x86-64 JIT, and feedback-driven optimizing tier
- Automatic tier promotion and explicit optimized-version invalidation
- Checked arithmetic, structured bailouts, deoptimization snapshots, boxed continuation, GC rooting hooks, stack maps, and runtime safepoints
- Runtime-helper dispatch with exception propagation
- Typed SSA, CFGs, dominators, dominance frontiers, loop discovery, conservative phi insertion, specialization guards, constant folding, branch folding, and dead-value elimination
- Portable TJIR images and checksummed multi-function TJM1 modules
- AOT module inspection and verification utility
- Cross-platform build, test, benchmark, sanitizer, clean, validate, and package scripts
- 25 focused tests covering the JIT/AOT pipeline

## Deliberately deferred after v1

- ARM64 native code generation
- Direct ELF/COFF/Mach-O object emission
- General optimized lowering for every CFG/phi shape
- Full JavaScript object-shape and inline-cache specialization
- Full OSR and advanced speculative optimizations
- Complete Test262 execution through every JIT tier

Unsupported optimizing shapes fail closed to baseline or interpreted execution. These limitations are documented rather than hidden behind unsupported performance claims.
