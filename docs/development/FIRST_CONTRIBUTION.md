# First Contribution

A good first change is narrow, observable, and covered by an existing test family.

## Recommended workflow

1. Read the subsystem guide and nearby architecture notes.
2. Locate the owning source manifest or generated-unit source.
3. Build the existing focused test before editing.
4. Add or strengthen a test that demonstrates the desired behavior.
5. Make the smallest implementation change that satisfies the contract.
6. Run focused tests, then the validation layers described in the developer guide.
7. Update documentation when behavior, ownership, format, API, or architecture changes.

## Suitable first areas

- Documentation corrections tied to verified code behavior.
- Missing negative tests for an existing API.
- A focused Test262 compatibility fix with interpreter coverage.
- Validation of malformed serialized input.
- A backend encoder edge-case test.
- Improvements to examples or downstream CMake integration.

## Review-ready description

Explain the problem, subsystem invariant, approach, tests run, platform, and any performance or compatibility impact. For optimizer changes, include the guard or proof and describe fallback/deoptimization behavior.
