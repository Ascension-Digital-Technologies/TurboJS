# TurboJS Roadmap

TurboJS is being developed toward a clear product goal: approach V8-class sustained JavaScript performance while retaining materially lower startup cost, memory usage, binary size, and embedding complexity.

## Compatibility

- Expand ECMAScript language and built-in coverage against Test262.
- Strengthen module, async, generator, iterator, proxy, and internationalization behavior.
- Maintain reproducible compatibility reports with categorized failures and timeouts.

## Optimizing compiler

- Broaden Redline coverage across real application control flow.
- Add more aggressive inlining, escape analysis, scalar replacement, and allocation removal.
- Improve representation selection, alias analysis, loop transforms, and range proofs.
- Expand polymorphic call, property, element, and collection specialization.

## Runtime

- Continue reducing startup latency and baseline memory commitment.
- Improve garbage-collector throughput, pause behavior, and JIT interaction.
- Strengthen native-code aging, invalidation, dependency tracking, and reclamation.
- Expand observability without adding overhead to production builds.

## Platforms

- Complete production ARM64 machine-code generation.
- Maintain first-class Windows, Linux, and macOS builds.
- Expand sanitizer, stress, and architecture-matrix validation.
- Produce reproducible release packages and embedding SDK artifacts.

## Performance validation

- Maintain startup, memory, binary-size, and sustained-throughput benchmarks.
- Compare identical workloads against V8 and other embeddable engines.
- Require checksum validation and regression thresholds for performance changes.
- Publish methodology and environment details alongside every benchmark claim.

## Release readiness

A stable release requires clean cross-platform builds, a fully passing native suite, a documented compatibility baseline, reproducible benchmark evidence, API stability, packaging validation, and clear security and support policies.
