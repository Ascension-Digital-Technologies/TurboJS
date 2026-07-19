# JIT and AOT subsystem

Contains the baseline IR, bytecode frontend, x86-64 backend, executable memory, code caches, deoptimization, stack maps, runtime helper dispatch, type feedback, SSA optimizer, and portable AOT formats.

## Rules

Every new operation needs verifier coverage, interpreter semantics, native lowering or a defined fallback, differential tests, and deoptimization/GC metadata when values can survive a safepoint.

## Related documentation

Start with the [repository README](../../README.md) and `docs/architecture/` for detailed design notes.

## Phase 37 OSR frame transfer

The JIT now exposes typed OSR frame snapshots and a parallel-move resolver. These components preserve interpreter locals and operand-stack values across an OSR boundary and schedule register/spill/phi transfers without clobbering cyclic values. The direct machine-code jump from a live interpreter loop remains a separate integration step.

## Phase 38 live interpreter backedges

The VM now observes actual backward branches and maintains bounded per-function OSR site state. When a site reaches its threshold, TurboJS captures a validated typed snapshot of arguments, locals, and the operand stack. This completes the interpreter-side handoff contract. The final control transfer into a generated loop-entry address remains intentionally disabled until loop-entry compilation and bailout reconstruction are connected end to end.

## Phase 39 executable OSR entry

The JIT now exposes a transactional OSR entry boundary. A ready entry receives a validated typed frame, returns an explicit resume bytecode offset on success, and restores the original interpreter frame exactly on bailout. This is the control-transfer contract required by generated loop-entry machine code. The generic contract is complete; automatic compilation of arbitrary JavaScript loop bodies into entry callbacks remains the next integration step.

## Phase 41 native counted-loop OSR

TurboJS can now compile a canonical positive-step integer counted loop into an executable x86-64 OSR kernel. The kernel consumes a validated `TurboJSOSRFrame`, keeps the induction variable, limit, and accumulator in registers, and commits final values through the transactional Phase 39 entry boundary. Invalid value kinds, local indices, iteration budgets, or unsupported architectures bail out without mutating interpreter state. Automatic recognition of matching engine-bytecode loop regions is the next integration step.
