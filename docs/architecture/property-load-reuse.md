# Property load reuse

Redline reuses a guarded own-data property load when a later load in the same
basic block has the same receiver SSA value and identical shape, slot, flags,
and dependency generation cases.

Reuse stops at any potentially aliasing property store. Stores remain
side-effecting even when their result is unused and are never removed by dead
value elimination.

The optimization reports `property_loads_eliminated` and
`property_dependency_reuses` through `TurboJSSSAOptimizationStats`.
