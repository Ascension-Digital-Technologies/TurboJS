# Optimizing control-flow graph

TurboJS now builds an explicit control-flow graph (CFG) for the optimizing tier. The baseline IR remains the fast compilation format; the CFG is created only for hot functions selected by type feedback.

## Basic blocks

Leaders are created at the entry instruction, branch targets, and instructions following branches or returns. Each block records its source instruction range, predecessor and successor lists, reachability, immediate dominator, and loop membership.

## SSA merges

When a reachable join receives different definitions of the same virtual register from two processed predecessors, the builder inserts a typed phi value. Phi insertion is conservative. Loop-carried definitions that require full dominance-frontier renaming remain a later optimization-tier task rather than being inferred unsafely.

## Analyses

The current graph computes:

- Reachability from the entry block
- Iterative dominator sets and immediate dominators
- Backedge-based natural-loop discovery
- Loop header and nesting-depth metadata

## Optimizations

The optimizer currently performs:

- Arithmetic and comparison constant folding
- Constant conditional-branch folding
- Unreachable-block marking
- Dead SSA value elimination

Graph verification checks block IDs, edge targets, value ownership, definition ordering, and phi operands.

## Next work

The next optimizer pass should add dominance frontiers, full SSA renaming for loop-carried values, branch probabilities from feedback, int32 specialization guards, and optimized-code deoptimization exits.
