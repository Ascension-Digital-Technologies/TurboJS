# Object Model, Shapes, and Properties

TurboJS implements dynamic JavaScript objects while optimizing common stable layouts. The generic object model remains authoritative; shapes and inline caches accelerate observations that can be guarded.

## Core concepts

- **Object identity:** distinct allocation identity independent of property contents.
- **Prototype:** lookup continuation for properties not found on the receiver.
- **Shape/layout:** metadata describing an object's structural property arrangement.
- **Property descriptor:** value/accessor and attribute semantics.
- **Element storage:** indexed storage for arrays and array-like objects.
- **Callable/constructor state:** behavior attached to function objects.

## Property operations

Reads, writes, definitions, deletes, enumeration, and prototype changes have different semantic requirements. Optimized property access must guard every assumption that can be invalidated by shape transitions, prototypes, accessors, or exotic behavior.

## Inline caches

A cache records observed receiver layouts and fast paths. Monomorphic and polymorphic caches may directly load, store, or call when identity and layout checks succeed. Cache misses fall back to runtime semantics and may update feedback. Dependency tracking invalidates or repatches compiled paths when relevant identities change.

## Arrays and elements

Dense element paths optimize in-range indexed access. Holes, sparse layouts, non-default descriptors, length changes, prototype effects, and non-integer keys require guarded or generic handling. Range analysis and loop specialization may remove repeated bounds checks only when the proof remains valid for the entire optimized region.

## Change discipline

Object-model changes require generic semantic tests, shape-transition tests, cache hit/miss tests, prototype and accessor edge cases, deoptimization tests, and GC stress for every newly retained managed reference.
