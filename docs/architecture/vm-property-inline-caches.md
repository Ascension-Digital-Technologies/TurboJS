# VM Property Inline Caches

TurboJS uses the engine's existing immutable, reference-counted `JSShape`
objects as guards for ordinary property reads executed by `OP_get_field` and
`OP_get_field2`.

## Cache organization

Each bytecode function lazily owns 16 direct-mapped cache entries. An entry
contains the bytecode-site hash slot, property atom, retained expected shape,
and direct property-array index. No cache memory is allocated for functions
that never execute a cacheable property read.

## Hit path

A hit requires all of the following:

1. The receiver is an ordinary, non-exotic object.
2. Its current shape pointer is identical to the retained expected shape.
3. The property atom matches.
4. The cached property index remains within the immutable shape.
5. The shape entry is an ordinary data property.

The VM then duplicates `object->prop[index].u.value` directly. This avoids the
shape hash lookup and prototype-chain walk.

## Miss and invalidation behavior

Shapes are immutable. Adding, deleting, or reconfiguring a property changes the
object's shape pointer, causing an automatic miss. The cache then follows the
canonical lookup path and may refill with the new own-property slot.

Prototype, accessor, variable-reference, autoinit, proxy, array-exotic, and
typed-array accesses intentionally remain on the canonical slow path. This
keeps the cache semantically conservative.

Entries retain their expected `JSShape`, preventing dangling shape pointers.
All retained shapes and the cache allocation are released when the owning
`JSFunctionBytecode` is destroyed.

## Statistics

`TurboJS_GetRuntimeJITStats()` reports `property_ic_hits`,
`property_ic_misses`, and `property_ic_fills` for measurement and regression
testing.
