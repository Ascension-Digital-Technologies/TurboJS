# Clutch Wide Call ABI

The current ABI widens the x64 Spool-to-Spool integer-compatible call edge from two positional operands to zero through four explicitly mapped arguments.

## Contract

`TurboJSClutchCallSite` owns no executable pointer. It records a Vault entry handle, expected generation and kind, and up to four source-register mappings. Gearbox materializes those values into a contiguous argument vector immediately before the direct native call.

## Safety

Before entering the callee, generated code validates the current Vault generation, native entry kind, function publication, and code pointer. Clearing or evicting Vault invalidates the handle and causes the caller to bail out through the normal Clutch safepoint.

## Current limits

The wide edge currently supports integer-compatible arguments and return values on x64. Float64 call lowering, closure environments, automatic JavaScript bytecode selection, and ARM64 parity remain future work.
