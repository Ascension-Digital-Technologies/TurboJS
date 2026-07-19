# Vault identity repatch index

Vault maintains a reverse index from stable callable identity to compiled owners containing
Clutch call sites for that identity. Publishing a replacement Spool generation performs one
identity lookup and visits only actual dependent owners.

The index is independent of the generation-based reverse dependency index used for retirement.
The dependency index answers "who currently calls this exact native handle?"; the identity
index answers "who can be safely reconnected to a replacement generation of this logical
JavaScript function?"

Owners register one identity edge even when several call sites reference the same function.
Owner eviction removes its identity edges. ABI compatibility is still checked at every repatch.
