# Native Element Loops

TurboJS connects Redline's canonical element range proofs to Gearbox x64 loop emission.

The initial native loop supports zero-based, unit-step Int32 induction over packed or typed 64-bit element storage with a sum accumulator. Gearbox validates the receiver, element kind, storage generation, and `limit <= length` once before loop entry, then hoists the backing-store base and executes the body without per-iteration bounds checks.

Safety exits remain before all backing-store access. Negative limits, values outside the uint32 range, limits larger than the current length, tag mismatches, element-kind mismatches, and generation mismatches return `TURBOJS_IR_BAILOUT`.

The initial implementation is deliberately narrow while the general multi-block loop backend matures. It consumes SSA proof metadata rather than recognizing engine bytecode.
