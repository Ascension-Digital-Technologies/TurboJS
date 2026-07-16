# Deoptimization Resume

TurboJS baseline native code records a deoptimization frame when a checked
integer fast path cannot preserve JavaScript semantics. Phase 15 extends that
frame with control-flow-aware liveness and static value-kind metadata.

The frame contains materialized and live masks for virtual registers and
locals. The current register IR has no operand stack at native boundaries, so
`stack_count` is zero by construction. This is explicit rather than implied so
a future stack-bearing IR or direct VM resume path can extend the ABI without
changing the meaning of existing fields.

`TurboJS_IRResumeAfterBailout` reconstructs the verified IR frame, installs the
result produced by a generic runtime slow path into the failed instruction's
destination, and resumes at the following instruction. Earlier instructions
are not replayed. This is the first exact-resume mechanism; production VM
integration will replace the supplied integer result with a boxed JavaScript
value and reconstruct the engine operand stack.

Liveness is solved as a backward fixed-point over jumps, conditional branches,
and fallthrough edges. Snapshot storage remains conservative, while live masks
identify the subset required by the continuation.
