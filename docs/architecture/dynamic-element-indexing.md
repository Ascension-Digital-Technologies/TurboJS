# Dynamic element indexing

The x64 backend extends the x64 Gearbox element fast path from constant indexes to runtime Int32-compatible indexes.

The direct path validates the receiver argument, element kind, storage generation, non-negative index, unsigned 32-bit range, and current length before scaling the index by the embedding-provided element stride. Failed guards bail out before storage access.

The initial implementation supports dynamic element loads in exact single-block regions. Stores, loop-carried indexes, range-derived guard elimination, and ARM64 parity remain follow-up work.
