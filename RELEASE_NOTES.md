# TurboJS 1.0.0

TurboJS 1.0.0 is the first stable release of the TurboJS engine and toolchain. It establishes a supported baseline for embedding, command-line execution, bytecode compilation, tiered native execution, portable AOT artifacts, validation, and performance measurement.

## Release highlights

- Stable `1.0.0` engine identity across CMake, Meson, public headers, and runtime version APIs.
- Compact native engine designed for direct C and C++ embedding.
- Interpreter, baseline native tier, optimizing SSA tier, OSR, deoptimization, inline caches, and application-region compilation.
- Portable AOT and module containers with versioned formats and inspection tooling.
- Public stable embedding table with explicit API and ABI versions.
- Reproducible benchmark runners and retained raw comparison data.
- Extensive architecture, subsystem, development, testing, portability, and contribution documentation.

## Compatibility and stability

The semantic engine, public API, and artifact formats continue to evolve independently. TurboJS 1.0.0 guarantees that incompatible changes to the stable embedding ABI or serialized formats will be accompanied by explicit version increments and migration documentation. Internal headers remain unsupported implementation details.

## Performance evidence

The repository retains the exact benchmark inputs, runners, checksums, and raw result files used for published comparisons. Historical result files preserve the engine versions under which they were recorded. See [Benchmarking](docs/performance/BENCHMARKING.md) and [Performance results](benchmarks/results/).

## Provenance

TurboJS contains permissively licensed derived work. Required attribution and license notices remain intact. The project history and attribution policy are documented in [Provenance](docs/project/PROVENANCE.md) and [NOTICE](NOTICE.md).
