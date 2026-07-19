# Clutch recursion and receiver ABI

Clutch supports two JavaScript call patterns that must remain safe
under Vault publication and invalidation.

## Late-bound self recursion

A recursive Spool function may be compiled before its own native entry handle
has been published. The recursive Clutch site therefore carries a stable
Telemetry/Beacon identity with a null native target. When Vault publishes the
function under that identity, the identity index repatches the native-owned
site to the new handle and generation. Before publication the call safely
bails out; after publication recursive calls remain inside Spool.

## Receiver-aware calls

`TurboJS_ClutchCallSiteSetReceiver` prepends a receiver register to the native
argument vector and marks the site with `TURBOJS_CLUTCH_CALL_HAS_RECEIVER`.
The existing argument order is preserved after the receiver, so a method ABI is
represented as `(this, arg0, arg1, ...)`. Receiver-bearing calls continue to
use generation guards, Vault dependencies, safepoints, and selective
repatching.

The current compiled ABI supports at most four physical arguments, including
the receiver.
