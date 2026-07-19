# Spool CFG frame merging

Spool removes the baseline frontend's empty-stack branch restriction.

The frontend first performs a bytecode worklist analysis that computes the exact operand-stack depth at every reachable instruction. Conflicting predecessor depths are rejected with `TURBOJS_SPOOL_REJECT_STACK_MERGE` rather than producing invalid IR.

For each branch edge, live stack registers are stored into hidden Spool frame locals. A branch target reloads those slots into fresh IR registers. These hidden locals behave as baseline merge slots: they preserve Pulse-compatible frame state without requiring SSA phis in the fast-compiling tier.

`TurboJSIRFunction::source_local_count` records JavaScript-visible locals while `local_count` includes hidden baseline frame slots. Rewind can therefore distinguish source state from compiler-owned storage.

`TurboJSSpoolLoweringStats` exposes maximum stack depth, branch targets, non-empty merges, spill stores, reloads, and an exact rejection reason and bytecode offset.
