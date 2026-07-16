# Optimizing execution and portable AOT v1

TurboJS v1 uses a two-stage optimizing pipeline. Hot functions are converted to typed SSA, specialized from stable feedback, optimized, verified, and lowered into the proven native baseline backend. Unsupported graph shapes fail closed and remain on baseline execution.

Portable AOT uses the `TJM1` multi-function module container. Each named function owns a checksummed `TJIR` image. The module has a versioned header, function table, total-size declaration, and whole-payload checksum. Loaded functions can be interpreted or compiled locally.

## Current v1 boundary

The optimizing native path supports verified single-block numeric graphs. Multi-block SSA analysis, dominance, loops, phis, guards, and deoptimization metadata remain available, but native lowering of general optimized CFGs is intentionally deferred until the final hardening pass. This prevents unsupported phi/control-flow forms from being silently miscompiled.
