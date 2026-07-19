# Gearbox Element Loop Unrolling

TurboJS Gearbox lowers Redline-proven zero-based packed-element sum loops into an x64 loop with one-time receiver, element-kind, generation, length, and backing-store validation.

The unrolled loop processes four elements per main-loop iteration and alternates additions between two independent accumulators. This reduces loop-control overhead and shortens the serial addition dependency chain. A scalar tail handles limits that are not divisible by four.

The optimization is used only after Redline has established the canonical induction and bounds proof. Invalid limits, stale storage generations, and receiver/layout mismatches bail out before element memory is accessed.

Telemetry reports the selected unroll factor and accumulator count through `TurboJSRegionNativeStats`.
