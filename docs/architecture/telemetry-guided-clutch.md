# Telemetry-guided Clutch calls

Telemetry connects the VM call-bytecode path to the execution pipeline:

`Pulse -> Telemetry -> Relay -> Clutch -> Spool -> Vault`

In Redline-enabled builds, Relay may publish a Spool-native Clutch edge only when
Telemetry reports exactly one stable target identity for that bytecode offset.
Polymorphic, megamorphic, missing, or colliding feedback refuses installation and
keeps execution on the canonical Pulse fallback path.

The call edge continues to retain only a numeric target identity and Vault
generation. It never owns a raw callee object or executable pointer.

## Counters

- `relay_spool_feedback_installs`: direct edges approved by monomorphic feedback.
- `relay_spool_feedback_rejections`: attempted edges rejected by unstable feedback.
- `relay_spool_stale_bailouts`: edges invalidated by a Vault generation mismatch.
- `relay_spool_callee_bailouts`: valid entries that could not complete natively.

Default builds without Redline preserve the prior zero-feedback-overhead Relay
behavior.
