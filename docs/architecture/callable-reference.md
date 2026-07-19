# CallableRef

`TurboJSCallableReference` is Spool's guarded native representation of a JavaScript callable.
It carries a stable Telemetry/Beacon identity, a generation-checked Vault handle, the expected
native ABI, argument count, and the closure environment required by future closure lowering.

CallableRef never owns executable memory and never assumes that a native entry remains live.
Every invocation validates the Vault generation and ABI before entering compiled code. A stale
reference returns `TURBOJS_IR_UNSUPPORTED`, allowing Relay, Rewind, or Pulse to recover safely.

This contract is the foundation for native global-function loads, closure-function loads,
property-loaded methods, and self-recursive Spool calls.
