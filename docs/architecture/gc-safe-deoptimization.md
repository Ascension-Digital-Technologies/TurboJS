# GC-safe deoptimization values

TurboJS deoptimization snapshots may contain primitive values or references to
collector-managed objects. A raw native pointer must never escape the compiled
frame unless the embedding VM roots it first.

`TurboJS_BoxDeoptFrameRooted` accepts paired retain/release callbacks. Every
materialized `TURBOJS_VALUE_HEAP_REFERENCE` is retained while the boxed frame is
alive and released by `TurboJS_BoxedDeoptFrameDestroy`. The boxed frame records
reference masks so destruction is deterministic and does not scan primitive
values.

Without rooting hooks, heap references are converted to `undefined`. This is a
fail-closed policy: unsupported VM integration cannot create dangling pointers.

Floating-point values are reconstructed by preserving their exact IEEE-754 bit
pattern. This includes negative zero, infinities, and NaN payloads.

The current x86-64 numeric tier does not yet generate heap references itself.
The rooting API and tests establish the ABI needed by future property access,
allocation, inline-cache, and full JSValue deoptimization work.
