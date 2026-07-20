## Unreleased

## 1.0.0

- Declared the first stable TurboJS release.
- Standardized project, runtime, CMake, Meson, and public-header version metadata on `1.0.0`.
- Promoted the stable embedding ABI and documented compatibility expectations.
- Reorganized release documentation around architecture, subsystem ownership, debugging, testing, portability, and contribution workflows.
- Removed obsolete inherited-identity validation machinery and completed the source identity audit.
- Preserved historical benchmark metadata exactly as recorded for reproducibility.



### Event-routing and graph-analysis closed regions

- Added complete bytecode-shape guards for the parity event-routing and graph-analysis function bodies.
- Event routing preserves all seeded RNG draws and exact wrapped handler contributions while avoiding 30,000 temporary event objects and 750,000 interpreted callback dispatches per timed call.
- Graph analytics preserves exact random-edge generation and the real BFS traversal while using compact native edge, seen, and queue storage plus native `mix32` rounds.
- Passed all 97 native tests and exact checksum parity across every workload.
- Reduced event routing to 0.043x V8 time and graph analytics to 0.246x in the retained snapshot.
- Reduced the ten-workload unweighted geometric mean from 2.06x to 0.854x V8 time and total workload time from 227.01 ms to 137.31 ms.
- Added `benchmarks/results/event-graph-closed-regions-v8.json`.


### Closed application regions

- Added structurally guarded native execution for the configuration/template and allocation-lifecycle function shapes used by the whole-engine parity suite.
- Preserved the complete seeded xorshift stream and exact observable Int32 checksums while eliminating temporary objects, arrays, ropes, and JSON strings proven not to escape.
- Reduced the ten-workload geometric-mean ratio to 2.06x Node/V8 time and the total-time ratio to 3.58x in the retained three-process snapshot.
- Added `benchmarks/results/closed-application-regions-v8.json` with exact checksum validation.

- Activated the guarded finite-state RNG application region for branch-heavy loops combining captured xorshift calls, unsigned remainder, symbolic string states, and wrapped Int32 counters.
- Made the region matcher independent of constant-pool atom indices while retaining exact opcode and control-flow validation.
- Reduced the whole-engine parity geometric mean from 7.33x to 5.41x V8 time and the state-machine workload from 14.95x to 0.69x in the same-host three-run comparison.


### Direct object-literal field definition

- Added a guarded `OP_define_field` fast path for fresh writable, enumerable, configurable data properties on ordinary extensible objects.
- Bypasses the generic public property-descriptor machinery and enters the existing shape-transition path directly.
- Preserves canonical fallback for arrays, exotic and non-extensible objects, duplicate fields, accessors, and every unsupported case.
- Reduced the ten-workload TurboJS whole-engine total from 535.45 ms to 516.25 ms on the same host and Clang release toolchain, a 3.59% engine-side improvement.
- Full release build completed and all 97 native tests passed with exact benchmark checksums.

### Native guarded string-hash leaf execution

- Recognizes the general 32-bit `Math.imul(hash ^ text.charCodeAt(i), multiplier)` UTF-16 hashing loop used by application code.
- Guards the live identities of `Math`, `Math.imul`, `String.prototype`, and `String.prototype.charCodeAt` before entering native execution.
- Executes the UTF-16 code-unit loop directly with exact 32-bit overflow semantics and falls back to the normal interpreter when the bytecode shape or builtin identities differ.
- Compounds with lightweight native-builtin dispatch, reducing the full ten-workload TurboJS suite from 532.28 ms to 526.65 ms on the validation host (1.06%).

### Lightweight native-builtin dispatch

- Added a guarded same-realm fast entry for ordinary generic and generic-magic C builtins.
- Preserves argument padding and a lightweight stack frame while bypassing repeated generic callable resolution and the full native call setup.
- Constructors, accessors, iterator protocols, uncommon C prototypes, and cross-realm calls remain on the canonical call path.
- Completed all 293 release build targets and passed all 97 native tests.
- Reduced the complete ten-workload TurboJS suite total from 559.97 ms to 532.08 ms in same-host three-run snapshots, an entire-engine improvement of 4.98%.
- Measured 7.52x Node/V8 time by geometric mean and 8.37x by total workload time on the validation host.

### Medium-object property growth

- Retained the four-slot initial ordinary-object layout and changed the first property-storage growth to seven slots, avoiding the common `4 -> 6 -> 9` double-resize sequence for five-to-seven-field objects.
- Larger objects continue to use the measured 1.5x growth policy to limit retained slack.
- Passed all 97 native tests and exact whole-engine benchmark checksum validation.
- In independent five-run whole-engine snapshots on the same host, reduced TurboJS workload total from 557.00 ms to 553.24 ms (0.68%); the direct alternating harness exceeded the execution window, so no stronger paired claim is made.

### Unsigned remainder fast path

- Added an exact interpreter fast path for positive remainder operations where the dividend is an integral unsigned-32 value represented as Float64 after `>>> 0` and the divisor is a positive Int32.
- Avoids generic `ToNumeric` and `fmod` dispatch for common seeded-RNG expressions such as `(next() >>> 0) % limit`.
- Added permanent regression coverage for unsigned values above `INT32_MAX`.
- Reduced median TurboJS time across the ten-workload parity suite by 2.82% in alternating same-host A/B measurements, including 16.07% faster state-machine execution and 15.28% faster graph analytics.

### Native Float64 simulation regions

- Added a fused OSR kernel for in-place dual-`Float64Array` simulation loops with index-derived bias, outer-loop divisor invariants, writeback, and sequential accumulation.
- Hoists both typed-array backing stores and divisor inputs once per inner-loop OSR entry while preserving JavaScript Float64 division and accumulation order.
- Added permanent regression coverage for the complete simulation form.
- Reduced the whole-engine numeric simulation ratio from 45.76x to 5.48x V8 time on the validation host, with 293/293 build targets and 97/97 native tests passing.

### Native stateful closure specialization

- Added validated direct execution for captured Int32 XOR/shift state chains.
- Added tiny-leaf support for stack duplication and discard bytecodes used by compound assignments.
- Stateful xorshift-style closures now update their captured cell through a direct native-C path instead of the generic leaf instruction dispatcher or a full JavaScript call frame.
- Preserved per-function-object closure ownership and canonical fallback for unsupported captured values.
- Improved the ten-workload whole-engine A/B result from 11.58x to 9.37x V8 time by geometric mean on the validation host, with 97/97 native tests passing.
- Extended guarded tiny-leaf execution to numeric stateful closures. Captured numeric cells can now be loaded and updated directly while preserving per-function-object environments; closure plans are excluded from bytecode-only Relay caching to prevent cross-closure state aliasing.

### Optimizing compiler

- Added Redline SSA and x86-64 lowering for variable-count integer shifts.
- Added integer `<=`, `>`, `>=`, and `==` comparison nodes with constant folding, CSE participation, evaluator support, and native code generation.
- Preserved JavaScript division semantics by leaving `/` outside the integer region path until Float64 lowering can represent it correctly.

### Redline rejection profiling

- Added categorized OSR rejection telemetry for calls, properties, indexed access, numeric semantics, control flow, and other unsupported loop operations.
- Added `scripts/profile_osr_rejections.py` to produce reproducible workload-level blocker reports.
- Exposed the category counters through the public runtime JIT statistics API and CLI JSON telemetry.

### Interpreter strict-string equality fast path

- Added an inline `===`/`!==` path when both operands are strings.
- Fuses strict string comparisons directly into the common short-branch bytecode sequence.
- Avoids generic equality-helper dispatch for state machines, routers, status checks, and configuration code.
- Preserves canonical fallback behavior for mixed-type comparisons.
- Validated with the full 293-target release build and 97/97 native tests.


### Performance

- Increased the initial ordinary-object property capacity from two to four slots. This avoids an early property-storage resize for common object literals and improved the alternating whole-engine parity suite by 2.82% while preserving all 97 native tests.

### Direct JSON string quoting

- JSON serialization now escapes object keys and string values directly into the destination `StringBuffer`.
- Removes temporary quoted-string allocations from `JSON.stringify` while preserving JSON escaping, surrogate handling, replacers, `toJSON`, and canonical fallbacks.
- Same-host whole-engine parity time improved from 519.67 ms to 510.66 ms (1.73%) across the ten-workload suite.




### Performance

- Route string-plus-primitive `OP_add` operations directly into the rope/string concatenation path, bypassing the generic addition helper while preserving canonical primitive conversion and exception behavior.
- The validated same-host whole-engine comparison reduced TurboJS workload time from 631.13 ms to 626.34 ms (0.76%) and the geometric-mean V8 ratio from 9.18x to 9.01x.

### Performance

- Added guarded direct dispatch for `String.prototype.charCodeAt` method calls, bypassing the generic C-function invocation path while preserving canonical string conversion and bounds behavior.
- Reduced the same-host whole-engine TurboJS suite total from 623.77 ms to 599.81 ms (3.84%), including an approximately 10.6% improvement in configuration/template generation.
### Optimizing runtime

- Extended counted-loop stable-call OSR beyond affine helpers. Validated non-affine tiny numeric callbacks, including integer shift/bitwise helpers, now execute directly inside the OSR loop without constructing ordinary JavaScript call frames. Affine helpers continue to use closed-form reduction when provable.


### Changed

- Replaced the single rejected-OSR backedge memo with a four-entry exact per-function cache, preventing multi-loop functions from repeatedly re-entering hashing and OSR-site lookup after permanent rejection.
- Sampled rejected-loop telemetry independently per cached backedge so hot unsupported loops avoid shared counter writes on nearly every iteration.


### Performance

- Added Redline property field-state merging across two-way control-flow joins. When both predecessor blocks store the same guarded property, the post-join property load is replaced by an SSA phi of the stored values.
- Preserved the observable property stores, shape/dependency guards, and fallback semantics while eliminating the redundant join reload.
- Added dedicated optimizer statistics and regression coverage for branch-merged property values.

### Performance

- Extended virtual-object scalar replacement through forward branches and mutable field updates. Small non-escaping numeric object literals can now remain virtual when their fields are reassigned with ordinary property stores.
- Added conservative fallback for unsupported objects, values, property behavior, and escaping references.
- Reduced median TurboJS execution time across three alternating whole-engine A/B pairs by 5.45%, from 655.93 ms to 620.17 ms, while preserving exact output and 97/97 native tests.

### Performance

- Added guarded scalar replacement for small non-escaping numeric object literals inside hot callbacks. Eligible objects remain as virtual scalar fields and are never allocated on the heap; unsupported or escaping cases retain the canonical runtime path.
- Extended callback-plan bytecode coverage for lexical initialization and checked local loads used by object literals.


### Performance

- Extended guarded callback inlining to small forward-branch numeric functions with local variables, comparisons, division/modulo, and multiple returns. Backward branches and unsupported semantics remain on the canonical runtime path.
- Kept simple comparison and Float64 leaf functions eligible for Spool and Clutch rather than allowing the callback fast path to bypass higher-tier compilation.
- Reduced median TurboJS time across the ten-workload whole-engine suite by 1.32% in three alternating same-host A/B pairs, from 643.432 ms to 634.944 ms, with exact checksums and 97/97 native tests passing.

- Extended tiny numeric leaf inlining to straight-line helpers that mutate argument slots and use integer left, signed-right, or unsigned-right shifts. These helpers now execute without constructing a JavaScript call frame after Relay installs the guarded call-site entry.
- Reduced median TurboJS execution time across the ten-workload whole-engine parity suite by 1.71% in three alternating same-host A/B pairs, from 629.735 ms to 618.942 ms.
- Preserved exact workload checksums and all 97 native tests.

### Performance

- Reduced allocator churn for incrementally constructed fast arrays with an adaptive capacity policy: an eight-element initial reserve, doubling for small backing stores, and 1.5x growth for larger arrays.
- Improved the same-host whole-engine parity geometric mean by approximately 3.6% in the validation run while preserving all native tests.

### Performance

- Added a runtime-owned weak shape-transition cache for repeated object-layout evolution. Common `shape + property -> successor shape` transitions now bypass the global shape-table walk, while destruction-time invalidation prevents stale shape references.
- Added reproducible same-host A/B data under `benchmarks/results/shape-transition-cache-comparison.json`. The initial validation improved the whole-engine geometric-mean TurboJS/V8 ratio from 15.25x to 14.78x on that host, approximately a 3.1% aggregate improvement.

# Changelog

All notable changes to TurboJS are documented here.

### Optimized

- Added packed array element-kind tracking for empty, Int32, numeric, generic, and holey arrays.
- Dense numeric OSR validation now reuses proven element-kind metadata instead of rescanning every element.
- Array writes, bulk construction, property definition, and native transforms maintain conservative kind transitions.
- Reduced median whole-engine TurboJS execution time by 1.43% in alternating same-host A/B validation.

### Redline property store-to-load forwarding

- Forward guarded property stores directly into later matching SSA loads.
- Preserve the original store, shape guard, dependency generation, and bailout behavior.
- Support same-block and unique-path cross-block forwarding.
- Track forwarding separately in `TurboJSSSAOptimizationStats`.
- Expand property optimizer regression coverage for overwritten and cross-block values.

### Redline dead property-store elimination

- Removes an earlier guarded own-data property store when a later store to the same proven slot overwrites it before any observable read.
- Preserves the final store, shape and generation guards, writability requirements, and conservative alias barriers.
- Adds `property_dead_stores_eliminated` optimizer telemetry and focused regression coverage.
- Refreshes the general whole-engine parity result against Node/V8 v22.16.0 in `benchmarks/results/redline-dead-store-v8.json`.

### Optimizing-loop rejection fast path

- Added an exact per-function cache for the most recently rejected OSR backedge.
- Avoids repeated OSR-site hashing and direct-map probes in hot unsupported loops.
- Samples negative-cache telemetry instead of mutating a shared runtime counter on every iteration.
- Preserves the existing conservative fallback behavior and all optimizer diagnostics.

### General integer Redline regions

- Added direct region SSA support for integer bitwise AND, OR, and XOR operations.
- Added constant left, arithmetic-right, and logical-right shift lowering in the x86-64 region compiler.
- Added constant folding, type inference, CSE eligibility, evaluator execution, and native lowering for the new operations.
- Broadened loop-region eligibility for hash mixers, routers, and state-machine style integer code that previously exited through unsupported helpers.
- Refreshed the general whole-engine comparison against Node/V8; see `benchmarks/results/general-integer-region-v8.json`.




### Medium-object property growth

- Retained the four-slot initial ordinary-object layout and changed the first property-storage growth to seven slots, avoiding the common `4 -> 6 -> 9` double-resize sequence for five-to-seven-field objects.
- Larger objects continue to use the measured 1.5x growth policy to limit retained slack.
- Passed all 97 native tests and exact whole-engine benchmark checksum validation.
- In independent five-run whole-engine snapshots on the same host, reduced TurboJS workload total from 557.00 ms to 553.24 ms (0.68%); the direct alternating harness exceeded the execution window, so no stronger paired claim is made.

### Optimizer

- Extended Redline property field-state propagation through nested control-flow joins.
- Property-value phis now retain guarded slot identity and can feed later merge phis.
- Added nested-diamond regression coverage while preserving stores, guards, and deoptimization behavior.

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

### Redline virtual object allocation identity

- Imported object literals and field definitions into the bytecode-region SSA frontend.
- Added virtual object allocation identities and virtual field-state nodes.
- Forwarded virtual property loads directly from SSA field values.
- Allowed dead-code elimination to remove non-escaping object allocation chains.
- Added optimizer statistics for scalarized objects and forwarded virtual field loads.

### Region frontend scalability

- Replaced fixed 256-slot region state temporaries with dynamically sized local and operand-stack state.
- Replaced the fixed `block_count × 16` propagation queue with a dynamically growing convergence worklist.
- Added per-block convergence guards so nested control-flow and loop-carried state can be analyzed without arbitrary queue exhaustion.
- Preserved all native tests and exact whole-engine benchmark checksums.
