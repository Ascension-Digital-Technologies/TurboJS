# Engine callable-load lowering

Spool's engine-bytecode frontend with rooted callable-load resolution.

Supported load classes:

- stable globals (`get_var`, `get_var_undef`)
- captured closure slots (`get_var_ref`, `get_var_ref_check`, `get_var_ref0` through `get_var_ref3`)

The VM supplies a `TurboJSEngineCallableResolver`. Successful resolution creates an IR-owned `TurboJSCallableReference`. Rooting ownership transfers through IR destruction and native Clutch-site cloning, so compiled wrappers cannot outlive captured environments.

Unstable or unresolved loads return `TURBOJS_IR_UNSUPPORTED` and remain on the canonical Pulse path.
