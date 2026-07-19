# Clutch selective repatching

Clutch call sites retain a stable numeric target identity independently of the
current Vault entry handle. When a target is retired, Vault clears the native
handle and generation while preserving that identity. When the same logical
function publishes a compatible replacement Spool entry, Vault can reconnect
existing callers without recompiling them.

A replacement is compatible only when its native entry kind and argument count
match the call site's recorded ABI. Incompatible sites remain safely invalid
and continue through their normal bailout path.

The VM uses Telemetry/Beacon function identities for automatically lowered call
sites. Pointer addresses are not used as logical identities, avoiding accidental
repatching if a bytecode allocation address is recycled.
