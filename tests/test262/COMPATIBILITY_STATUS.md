# Test262 compatibility status

## Full single-variant baseline

Source report: completed Windows run, 53,690 files.

| Status | Count |
|---|---:|
| Pass | 42,949 |
| Fail | 10,318 |
| Skip | 401 |
| Timeout | 22 |

Overall pass rate excluding explicit skips: **80.60%**.

The dominant unimplemented feature groups are Temporal and Intl402. For engineering prioritization, the core profile excludes the Intl402 tree and tests tagged `Temporal`; the public conformance number must continue to use the full profile.

## Priority clusters

1. Modules, dynamic import, import-defer, and top-level await.
2. TypedArray, DataView, detached-buffer, and Atomics semantics.
3. Promise job ordering and async execution.
4. Annex B legacy RegExp accessors and eval behavior.
5. Parser edge cases around single-statement function declarations and import syntax.

Use `scripts/test262_analyze.py` to regenerate path and error-signature clusters from any report.
