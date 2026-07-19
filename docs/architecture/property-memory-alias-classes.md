# Property Memory Alias Classes

TurboJS Redline models guarded property memory by logical alias class.

A property alias class is identified by the bytecode property atom (`metadata`)
and the guarded shape/slot cases attached to a property SSA node. Known,
distinct atoms are independent even when they are accessed through the same
receiver or hidden class. Unknown metadata remains conservative.

## Join memory phis

When a dominating property load reaches a control-flow join, Redline proves the
incoming property-memory version for that alias class on every predecessor. If
each predecessor carries the same unmodified version, the join records a
property-memory-phi proof and later loads may reuse the dominating value.

The memory phi is an optimizer proof rather than a runtime value. It therefore
adds no machine-code operation.

## Selective invalidation

A store invalidates only property classes it may alias. A known store to `y`
does not invalidate a cached load of `x`, even when both operations use the same
object shape. A store with unknown property identity, or a store to the same
property class, remains a full barrier.

## Safety rules

Redline does not reuse a property value when:

- property metadata is missing or ambiguous;
- the store and load belong to the same alias class;
- receiver shape sets overlap and property identity is unknown;
- dependency generations or PIC cases differ;
- any incoming path carries a modified version;
- loop or dominance proofs are incomplete.
