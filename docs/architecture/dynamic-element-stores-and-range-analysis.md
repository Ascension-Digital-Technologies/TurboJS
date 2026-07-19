# Dynamic element stores and loop-range groundwork

The Gearbox x64 element emitter supports the Gearbox x64 element emitter to accept both a runtime Int32-compatible index and a runtime stored value. The emitted guard sequence validates the receiver, element kind, storage generation, signed index, uint32 range, and current length before writing the backing store.

The optimizer also reports element indexes that are already Int32-specialized and appear in detected loop blocks. These counters are groundwork for a later induction/range pass; they do not remove loop bounds checks by themselves.

## Safety

A failed tag, kind, generation, signed-range, or bounds guard returns `TURBOJS_IR_BAILOUT` before memory is written. Constant-index and constant-value stores remain supported.
