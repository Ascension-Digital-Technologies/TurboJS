# Test262 baseline results

This checked-in baseline is a **broad single-variant run**, not a claim that the
entire 53,690-file suite completed in this environment.

| Metric | Result |
|---|---:|
| Executions | 6,000 |
| Passed | 5,833 |
| Failed | 155 |
| Skipped host-dependent tests | 12 |
| Timed out | 0 |
| Pass rate excluding skips | 97.41% |

Coverage used for this baseline:

- 3,000 `language/expressions` tests
- 2,000 `language/statements` tests
- 1,000 `built-ins/Array` tests

The largest observed failure clusters were destructuring assignment, decorators,
Annex B assignment-target behavior, and several Array callback edge cases. The
complete machine-readable results are generated under `build/` by the runner.

## Full run

```bash
python scripts/test262.py --single-variant --workers 16
```

For CI or constrained environments, shard it deterministically:

```bash
python scripts/test262.py --single-variant --shard-count 16 --shard-index 0
```
