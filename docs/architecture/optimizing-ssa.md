# Optimizing SSA IR

TurboJS keeps the compact baseline IR unchanged and introduces a separate SSA graph for expensive optimization work.

The first optimizing layer provides typed SSA values, basic-block ownership, use counts, explicit phi nodes, graph verification, constant folding, and dead-value elimination. The builder currently accepts the side-effect-free arithmetic subset of baseline IR; unsupported dynamic operations remain in the baseline tier.

## Invariants

- Every value has one definition.
- Inputs reference earlier values.
- Values belong to a valid basic block.
- Phi nodes are explicit and are never synthesized by guessing stack merges.
- Optimization passes preserve graph verification.

This layer is intentionally backend-independent. Later phases will add control-flow graph construction, dominators, loop discovery, feedback-driven typing, deoptimization edges, scheduling, and register allocation.
