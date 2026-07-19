# Element loop range proofs

Redline recognizes canonical zero-based unit-step induction variables used by guarded element operations.

A proof requires:

- an Int32-compatible phi in a detected natural loop,
- an initial value of zero,
- a backedge update of `i + 1`,
- a loop-header comparison `i < limit`, and
- the element operation to name the same limit value.

A proven operation records the induction step, lower bound, limit SSA value, one-time length/base hoisting eligibility, and loop-wide bounds-check proof. These annotations are conservative metadata for subsequent Gearbox loop lowering; they do not by themselves suppress checks in unsupported backends.
