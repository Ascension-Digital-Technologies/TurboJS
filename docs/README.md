# TurboJS Documentation

This portal is the authoritative map of the TurboJS 1.0.0 codebase. It explains not only how to build and use the engine, but where each subsystem lives, which contracts connect them, how changes should be validated, and which interfaces are stable.

## Choose a path

### I want to embed TurboJS

1. [Embedding guide](embedding/EMBEDDING_GUIDE.md)
2. [Stable embedding API](embedding/README.md)
3. [Public API and ABI policy](api/PUBLIC_API_AND_ABI.md)
4. [`examples/embed-stable`](../examples/embed-stable/)

### I want to understand the engine

1. [Engine architecture](architecture/ENGINE_ARCHITECTURE.md)
2. [Execution pipeline](architecture/execution-pipeline.md)
3. [Source and subsystem map](architecture/SUBSYSTEM_MAP.md)
4. [Runtime and VM](subsystems/RUNTIME_AND_VM.md)
5. [JIT pipeline](subsystems/JIT_PIPELINE.md)

### I want to contribute

1. [Developer guide](development/DEVELOPER_GUIDE.md)
2. [First contribution](development/FIRST_CONTRIBUTION.md)
3. [Building](development/building.md)
4. [Testing](development/testing.md)
5. [Debugging](development/DEBUGGING.md)
6. [Coding and ownership conventions](development/CODEBASE_CONVENTIONS.md)

### I want to evaluate performance

1. [Performance model](performance/PERFORMANCE_MODEL.md)
2. [Benchmark methodology](performance/BENCHMARKING.md)
3. [Retained results](../benchmarks/results/)
4. [Benchmark tools](../tools/benchmarks/)

## Architecture and subsystem guides

- [Engine architecture](architecture/ENGINE_ARCHITECTURE.md)
- [Subsystem map](architecture/SUBSYSTEM_MAP.md)
- [Frontend and bytecode](subsystems/FRONTEND_AND_BYTECODE.md)
- [Runtime and VM](subsystems/RUNTIME_AND_VM.md)
- [Object model](subsystems/OBJECT_MODEL.md)
- [Memory and garbage collection](subsystems/MEMORY_AND_GC.md)
- [JIT pipeline](subsystems/JIT_PIPELINE.md)
- [Native backends](subsystems/NATIVE_BACKENDS.md)
- [AOT and serialization](subsystems/AOT_AND_SERIALIZATION.md)
- [Built-ins, modules, RegExp, Unicode, and numeric support](subsystems/LANGUAGE_SERVICES.md)

## Stability labels

- **Stable public API:** installed headers and the versioned embedding table.
- **Versioned artifact:** bytecode, TJIR, and TJM formats whose readers validate format versions.
- **Internal contract:** interfaces below `src/internal/` or private JIT headers; these can change without compatibility guarantees.
- **Historical evidence:** benchmark and audit records retained for reproducibility; they do not define current behavior.

## Documentation rules

Every architectural change should update the nearest subsystem guide and any affected contract document. New public API requires examples, tests, ABI notes, and release notes. Performance claims require retained inputs, environment details, correctness checks, raw output, and a reproducible runner.
