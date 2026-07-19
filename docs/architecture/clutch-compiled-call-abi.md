# Clutch compiled-call ABI

Clutch is TurboJS's stable hand-off contract for compiled JavaScript calls.

A Spool caller records the target handle, expected Vault generation, argument
count, closure environment, safepoint, caller-save masks, and call flags in a
`TurboJSClutchCallFrame`. The frame never owns a raw executable allocation.
Vault remains responsible for publishing and invalidating the target handle.

This pass establishes the ABI and validation boundary used by the forthcoming
Gearbox `CALL_JS` lowering. It supports recursive and environment-bearing call
metadata, exact caller-clobber masks, and stale-entry rejection. The current
portable helper still enters through `TurboJS_NativeInvoke*`; the next backend
step lowers the same record to a direct machine-code edge.
