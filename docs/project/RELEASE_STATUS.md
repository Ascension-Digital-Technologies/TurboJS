# TurboJS 1.0.0 Release Status

TurboJS 1.0.0 is the first stable release line. The repository presents a complete embeddable engine stack: parser and bytecode compiler, interpreter, object/runtime semantics, garbage collection, baseline and optimizing native tiers, OSR and deoptimization, portable AOT artifacts, command-line tools, stable embedding API, tests, benchmarks, and developer documentation.

## Stable commitments

- Engine version APIs report `1.0.0`.
- The stable embedding table is versioned and size-checked.
- Installed public headers are separated from internal source headers.
- Persistent artifact readers validate their own format versions.
- Historical benchmark records retain their original version metadata.

## Ongoing work after 1.0

A stable release does not mean the implementation is finished forever. Compatibility coverage, optimizer breadth, backend parity, diagnostics, platform validation, and real-world workload coverage remain active engineering areas. Changes must preserve the documented fallback and compatibility contracts.
