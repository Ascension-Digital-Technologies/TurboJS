# Vault continuation dependencies

Vault treats native continuation segments as first-class compiled code owners.

When a normal compiled function entry is retired, Vault scans every live native
function, including continuation segments, and invalidates Clutch call sites
that target the retiring entry handle before executable memory or the handle
owner can be released.

Continuation invalidations are reported separately through
`continuation_dependency_invalidations`, while the aggregate
`dependent_call_sites_invalidated` counter remains available.

## Weighted aging

Eviction uses an age, code-size, and access-frequency score. Hot continuation
segments receive a higher retention weight because they are small, frequently
re-entered suffixes whose recompilation cost is far larger than their lookup
cost. Large or cold entries still age out under the global entry and byte
budgets.

The policy does not pin continuation code. All entries remain evictable and
retain generation-safe Clutch bailout behavior.
