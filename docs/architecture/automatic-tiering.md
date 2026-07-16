# Automatic tier promotion

TurboJS v1 uses three execution routes for supported functions:

1. the reference IR interpreter for cold code;
2. the baseline x86-64 JIT after the baseline threshold;
3. the SSA optimizing tier after feedback satisfies the optimization policy.

`TurboJSTieredFunction` owns the optimized code object and destroys or invalidates it explicitly. The shared `TurboJSCodeCache` continues to own baseline code. This separates inexpensive baseline compilation from speculative optimized versions.

Promotion requires stable argument and result feedback, a minimum execution count, and acceptable bailout and exception counts. An optimized bailout invalidates the speculative version and immediately falls back to baseline or interpreted execution. The function is not repeatedly reoptimized during the same lifetime.

Use `TurboJS_TieredFunctionInitAdvanced` to configure both thresholds and the policy. Always call `TurboJS_TieredFunctionDestroy` when the owning bytecode function is released.
