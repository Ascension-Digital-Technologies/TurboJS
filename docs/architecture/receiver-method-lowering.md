# Receiver-aware method lowering

Spool can lower `call_method` and `tail_call_method` bytecodes when the call resolver supplies a stable monomorphic target.

The engine stack layout is interpreted as:

```
receiver, callable, argument0, argument1, ...
```

The callable value is guarded by the frontend resolver. Clutch stores the receiver as physical argument zero and shifts ordinary arguments after it. Generation validation, reverse dependencies, selective repatching, and closure-environment ownership are unchanged.

Unsupported, polymorphic, oversized, or unresolved method calls remain on the canonical Pulse/runtime-helper path.
