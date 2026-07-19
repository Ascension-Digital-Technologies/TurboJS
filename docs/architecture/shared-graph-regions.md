# Shared Graph Regions

TurboJS can optimize guarded application regions that operate on shared object graphs rather than treating every hot operation as an isolated fragment. This is particularly useful for AST visitors, record-processing pipelines, grouped accumulators, callback routers, and other workloads where object identity and repeated subgraphs matter.

## Design

The region matcher recognizes stable opcode and control-flow structure without depending on source-order-specific atom identifiers. The evaluator dispatches supported node types through interned atoms and maintains a bounded object-identity memo table with active and complete states.

- Active entries detect cycles and force a safe fallback.
- Complete entries return an already-validated result for shared subgraphs.
- Unsupported shapes, accessors, proxies, inherited properties, unknown node types, excessive depth, and oversized graphs remain on the canonical interpreter path.

## Supported execution patterns

The current implementation supports guarded regions for:

- shared DAG-style AST traversal;
- grouped integer accumulation;
- local record transformation;
- callback routing with observed targets;
- dense-array reductions and updates;
- coupled Float64 recurrences.

## Correctness model

Every optimized region is entered only after its assumptions have been validated. Runtime guards cover object identity, shape, property ownership, element kind, backing storage, call targets, and relevant generations. When an assumption no longer holds, execution returns to a safe lower tier through deoptimization or canonical fallback.

## Validation

Shared-region behavior is covered by dedicated native tests for ordinary trees, shared graphs, source-order variation, accessor fallback, cycle rejection, grouped accumulation, callback routing, and dense-array OSR. Benchmark results are kept separate from architectural documentation so this document remains durable across releases.
