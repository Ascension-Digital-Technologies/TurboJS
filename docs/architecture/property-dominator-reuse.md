# Property dominator reuse

Redline may reuse a guarded property load across basic blocks when the original
load dominates the later load and the path between them is structurally unique.

The optimization requires:

- identical receiver SSA values;
- identical one-to-four-case property PIC metadata;
- identical shape, slot, property flags, and dependency generations;
- a single-predecessor chain from the later block to the dominating block; and
- no potentially aliasing property store before the later load.

This conservative rule allows straight-line cross-block reuse and reuse from a
loop header into a uniquely dominated loop body. It deliberately rejects reuse
through CFG joins and preheader-to-loop hoisting, where a store on another path
could invalidate the loaded value. Full memory SSA is required before those
cases can be optimized safely.
