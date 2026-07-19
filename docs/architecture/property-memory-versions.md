# Property memory versions

Redline models guarded property loads as reusable only while every control-flow
path carries the same property memory version.

A dominating load may replace a later equivalent load when:

- all PIC shape, slot, flag, and dependency-generation cases match;
- every incoming path from the dominating block is free of potentially aliasing
  property stores; or
- the later load is inside a natural loop whose blocks contain no potentially
  aliasing store and the earlier load dominates the loop header.

The proof remains conservative around irreducible cycles, unknown stores, and
receivers whose guarded shape sets overlap. Any such ambiguity preserves the
later load and its guards.

Optimization telemetry distinguishes unique-path reuse, branch-join reuse,
proved memory versions, and loop-invariant reuse.
