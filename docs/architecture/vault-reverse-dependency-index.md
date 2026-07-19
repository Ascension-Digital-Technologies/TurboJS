# Vault reverse dependency index

Vault maintains a reverse index from each published native entry handle to the
compiled functions and continuation segments that contain Clutch sites targeting
that handle.

## Ownership

Each live Vault entry owns a linked list of dependency registrations. Each
registration also belongs to a target-handle hash bucket. Removing a caller
unlinks only that caller's registrations. Retiring a callee visits only the
registrations in the callee's bucket.

## Invalidation

Before an entry handle is invalidated, Vault:

1. Looks up the handle in the reverse index.
2. Visits the registered ordinary functions and continuation segments.
3. Clears matching Clutch target pointers and expected generations.
4. Updates ordinary and continuation invalidation telemetry.
5. Invalidates the entry handle and releases the callee.

This changes dependency retirement from a full Vault scan to work proportional
to the number of actual dependent call edges.

## Telemetry

`TurboJSCodeCacheStats` exposes:

- `reverse_dependency_lookups`
- `reverse_dependency_nodes_visited`
- `reverse_dependency_registrations`
- `reverse_dependency_unregistrations`

These counters make dependency-index efficiency and churn directly measurable.
