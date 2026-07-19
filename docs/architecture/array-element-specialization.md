# Array and typed-element specialization

TurboJS provides first-class SSA element operations for packed, holey, and Float64 typed storage. Element loads and stores carry an expected element kind, generation, and semantic flags. The region value path validates the element contract, obtains the current length, performs an unsigned bounds check, and only then accesses the element.

Repeated same-array/same-index loads are eliminated inside a block when their kind, generation, and flags match and no store to that array intervenes. This reuses the prior element-kind, length, and bounds proof. Stores remain side-effecting and are never removed by dead-value elimination.

This pass intentionally uses the guarded value backend. A future Gearbox pass should lower the same contract into direct x64 and ARM64 base-pointer, length, and element instructions, then combine it with Redline induction-variable and range analysis to remove loop bounds checks.
