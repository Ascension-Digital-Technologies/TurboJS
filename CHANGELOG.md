# Changelog

All notable changes to TurboJS are documented here.

## Unreleased

### Engine

- Added shared application-region graphs for AST visitors, grouped accumulators, record processing, callback routing, and coupled numeric workloads.
- Added polymorphic compiled-call publication, dense-array OSR integration, native continuation tracking, and dependency-aware native entry management.
- Expanded guarded Float64 and SIMD lowering with runtime-safe AVX2 and FMA dispatch.
- Improved runtime feedback, inline-cache integration, deoptimization coverage, and native-code ownership.

### Platform support

- Added complete Windows Clang support for resource compilation, monotonic timing, aligned allocation, CPU feature detection, and platform-specific libraries.
- Added `scripts/build-windows.ps1` for automatic LLVM compiler and resource-compiler setup.
- Added shared cross-platform timing, aligned-memory, and x86 feature-detection helpers.

### Testing and reliability

- Expanded the full-release build to 293 targets.
- Expanded the native validation suite to 97 tests.
- Added coverage for shared graph regions, grouped accumulation, callback routing, dense-array OSR, coupled Float64 execution, and Windows portability.
- Updated optimized-tier tests to validate native takeover rather than stale interpreter counters.

### Repository

- Reworked the README around TurboJS as a compact, embeddable, high-performance JavaScript engine.
- Removed obsolete development journals, numbered optimization history, duplicate files, and stale benchmark artifacts.
- Renamed tests and documentation around capabilities rather than internal development milestones.
- Standardized documentation around the execution pipeline and durable architectural concepts.

## 0.16.0-rc.6

- Introduced the current tiered execution architecture with interpreter, baseline compiler, feedback-directed optimizing compiler, OSR, deoptimization, native code caching, and AOT support.
- Added the public C embedding API, command-line runtime, build presets, native test suite, and cross-platform project structure.
