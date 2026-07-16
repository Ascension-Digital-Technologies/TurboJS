# JIT and AOT roadmap

## Phase 1 — foundation

- Stable optimization configuration and capability reporting
- Per-function call profiling and tier requests
- Portable bytecode AOT pipeline
- Reproducible benchmarks and correctness tests

## Phase 2 — baseline JIT

- Backend-neutral low-level IR
- Bytecode-to-IR lowering
- Linear-scan register allocation
- x86-64 and AArch64 assemblers
- Guard checks, deoptimization metadata, executable-memory abstraction

## Phase 3 — optimizing JIT

- SSA-based mid-level IR
- Type feedback, inline caches, constant folding, dead-code elimination
- Function inlining, escape analysis, loop optimizations
- On-stack replacement and precise deoptimization

## Phase 4 — native AOT

- Whole-module compilation using the same optimizing backend
- Relocatable object emission and deterministic cache keys
- Snapshotting of immutable runtime state
- Profile-guided optimization input

Every phase must preserve interpreter fallback and pass differential tests before becoming enabled by default.

## Phase 2 implementation status

TurboJS now contains an executable baseline tier rather than policy-only JIT
placeholders. The first backend intentionally targets a narrow, testable subset:
64-bit integer arguments, constants, addition, subtraction, multiplication, and
returns. The reference IR interpreter supports additional control-flow opcodes.

Every IR function is verified before interpretation or native compilation.
Unsupported native operations return `TURBOJS_IR_UNSUPPORTED`, allowing the
caller to retain the interpreter result without changing program semantics.
Executable memory follows write-then-execute discipline: code pages are emitted
as writable memory and sealed read/execute before invocation.

The next backend increment is branch lowering, stack-slot register spilling,
and a bytecode frontend that maps selected TurboJS bytecode operations into this
IR. Native compilation must remain differential-tested against the IR
interpreter for every supported operation.

## Phase 3 progress

Completed:

- Portable bytecode-to-IR frontend.
- Stack-backed allocation for all 64 virtual registers.
- Native signed comparisons, jumps, conditional branches, and loops.
- Forward/backward branch fixups.
- Differential loop coverage and maximum-register-pressure coverage.

Next:

- Translate supported `JSFunctionBytecode` numeric opcodes into TurboJS IR.
- Add boxed `JSValue` guards and slow-path exits.
- Add compiled-function ownership and runtime code-cache eviction.
- Trigger baseline compilation from profiler hotness transitions.

## Phase 4: engine bytecode bridge and owned code cache

Completed:

- direct decoding of the existing engine stack bytecode for a conservative
  integer-safe opcode subset;
- precise unsupported-opcode diagnostics with byte offsets;
- bounded LRU native code cache with explicit ownership and invalidation;
- call-count based automatic transition from the IR interpreter to native code;
- differential validation of engine bytecode, verified IR, and x86-64 output.

Next:

- guarded `JSValue` integer entry stubs;
- result boxing and slow-path return to the VM;
- per-`JSFunctionBytecode` tier metadata;
- branch translation for real engine jump opcodes;
- local-variable load/store translation;
- code invalidation hooks during bytecode destruction and runtime shutdown.

## Phase 6: mutable locals and fast development builds

Completed:

- Explicit IR local loads and stores
- x86-64 stack-frame allocation for mutable locals
- Real engine bytecode local-opcode translation
- 10,201-case interpreter/native differential coverage
- Separate focused and full-engine CMake test profiles
- Reproducible `jit-dev` and `full-release` presets

Next: control-flow dataflow, VM jump lowering, checked arithmetic exits, and source-level loop benchmarks.

## Phase 8 status

Completed: overflow-checked int32 add/subtract/multiply, status-returning native ABI, whole-call bailout, interpreter fallback, and boundary differential tests. The next deoptimization milestone is instruction-precise bailout metadata with live-value maps.


## Completed in Phase 13

- Checked int32 division and remainder IR operations
- Native divide-by-zero and signed-overflow guards
- Structured division bailout reasons
- Per-instruction engine bytecode offsets
- TJIR version 2 source-position persistence

The next deoptimization step is live-value mapping and interpreter frame reconstruction.
