# Contributing to TurboJS

Thank you for helping improve TurboJS. Compiler and runtime changes can produce subtle semantic failures, so contributions are expected to be small enough to review, heavily tested, and explicit about fallback behavior.

## Before opening a pull request

1. Open or reference an issue for substantial behavior changes.
2. Build with the focused preset.
3. Run the complete focused test suite.
4. Run architecture validation.
5. Add differential tests for execution changes.
6. Update the relevant architecture or format documentation.

```bash
python scripts/validate.py --preset jit-dev
```

## Source ownership

- Public API: `src/api/`
- Private shared contracts: `src/internal/`
- Baseline and optimizing compiler: `src/jit/`
- Repository utilities: `tools/`
- Workflow wrappers: `scripts/`
- Tests: `tests/`

Do not place repository tools, generators, or compatibility buckets inside `src/`.

## Correctness rules

- The interpreter is the semantic reference.
- A compiler must reject unsupported input rather than approximate it.
- Checked numeric instructions must preserve JavaScript edge behavior through bailout or generic runtime handling.
- GC-visible references require stack-map, rooting, or relocation support.
- Deoptimization must not replay observable side effects.
- Serialized formats require versioning, bounds checks, and integrity validation.

## Build-system rules

- List engine/tool sources explicitly where practical.
- Do not restore recursive `CONFIGURE_DEPENDS` source globbing; it caused a Windows Ninja regeneration loop.
- Generated outputs must declare their generator and inputs.
- New warnings must be fixed, not globally disabled.

## Tests

Add the narrowest test that proves the behavior, plus differential coverage where applicable. Tests must not depend on `assert()` side effects because Release builds may define `NDEBUG`.

## Commit and pull-request guidance

Use an imperative subject and keep unrelated changes separate. A pull request should explain:

- What changed
- Why it is correct
- Which fallback paths remain
- Tests executed
- Benchmark impact, when relevant
- Documentation updated

## Code style

Use C11, explicit ownership, bounded data structures, and descriptive public names. Keep backend-specific details behind JIT interfaces. Comments should explain invariants, ABI contracts, safety constraints, and non-obvious tradeoffs—not restate syntax.
