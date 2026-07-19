# Property PICs in Redline

TurboJS property SSA nodes may carry up to four shape/slot cases. Each case records the receiver shape identity, own-property slot index, feedback generation, and property flags.

## Execution

- One-case own-data loads remain eligible for the inline x64 object-layout path.
- Two-to-four-case loads execute through the guarded region value thunk.
- Writable stores use the same guarded PIC selection and call the embedding's `store_own_slot` operation.
- Optional dependency guards validate feedback generations before a load or store is committed.
- Shape misses, stale dependencies, unavailable stores, and non-writable feedback bail out safely.

## Next backend step

Gearbox should emit the four shape comparisons and direct slot loads/stores inline, followed by one shared bailout edge. Prototype-chain feedback should register Vault dependencies and invalidate affected code when a prototype generation changes.
