# Vault-owned continuation code

Native continuation segments are executable code and are therefore owned by Vault rather than by a runtime-helper table.

A helper table may attach a `TurboJSCodeCache` with `TurboJS_RuntimeHelperAttachContinuationVault`. Continuation lookup uses the stable tuple `(IR instance id, IR revision, continuation instruction)`. On a miss, Spool compiles the suffix and transfers ownership to Vault. The helper table retains only callbacks and per-invocation counters.

This provides:

- one entry and byte budget for ordinary and continuation native code;
- LRU aging and eviction through the normal Vault policy;
- continuation reuse across helper-table recreation;
- explicit continuation code-size and pressure statistics;
- destruction through the normal native-function ownership path.

If no Vault is attached, the legacy private 16-entry cache remains available as a compatibility fallback.
