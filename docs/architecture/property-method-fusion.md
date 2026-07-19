# Property method fusion

Spool can lower the engine sequence `receiver; get_field2 atom; arguments; call_method`
when the embedding VM provides monomorphic own-property method feedback.

The resolver returns a rooted `CallableRef`, the receiver shape-field offset, and the
expected stable shape identity. The frontend fuses that metadata into the receiver-aware
Clutch call site. Gearbox checks the receiver pointer and shape identity before validating
the Vault generation and entering the native method target. A shape mismatch bails out to
Rewind/Pulse without touching the cached target.

This first implementation is intentionally monomorphic and own-property only. Prototype
methods, accessor properties, proxies, and polymorphic shape families remain on the
canonical runtime path.
