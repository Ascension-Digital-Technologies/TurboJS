# Repository layout

TurboJS keeps product source, generated artifacts, tooling, tests, and historical data in separate ownership domains.

```text
apps/                 Product entry points and CLI bootstrap JavaScript
benchmarks/           Authored benchmarks and archived benchmark results
cmake/                Modular CMake policy and source manifests
docs/                 Architecture, development, performance, and project records
examples/             Downstream embedding examples
generated/            Reproducible checked-in generated sources
include/turbojs/      Supported public C SDK
scripts/              Developer workflow wrappers
src/                  Handwritten engine implementation only
tests/                Unit, embedding, JIT, VM, and conformance harness tests
third_party/          Optional fetched dependencies and conformance suites
tools/                Generators, validation, amalgamation, and AOT tools
```

## Root policy

Only cross-project entry files belong at the root: build entry points, package metadata, project governance, licensing, and the primary README. Roadmaps, release records, benchmark output, and subsystem documentation belong in their dedicated directories.

## Engine policy

`src/` has no generated output, application entry point, benchmark result, public SDK header, or repository tool. The subsystem map in `cmake/TurboJSSubsystems.json` is the canonical ownership and dependency order for the generated engine unit.
