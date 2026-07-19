# Repository Maintenance

## Root-directory policy

The repository root is reserved for:

- Build-system entry points
- README and roadmap
- Contribution, security, support, and conduct policies
- Changelog
- License and notice files once finalized

Architecture notes, status records, platform fixes, and benchmark reports belong under `docs/`.

## Documentation policy

- One canonical current status: `docs/project/STATUS.md`
- One completion roadmap: `docs/project/ROADMAP.md`
- One benchmark methodology: `docs/performance/BENCHMARKING.md`
- Historical reports: `docs/performance/history/`
- No phase-by-phase banners in the README
- No unsupported or stale benchmark claim without machine and methodology context

## Generated files

Generated source must be reproducible from checked-in generators. Generated outputs should carry a header naming their generator and should be verified in CI.

## Warning policy

New or modified code must compile warning-free with the project warning set. Release-candidate CI should enable `TURBOJS_BUILD_WERROR=ON` on supported compiler configurations.

## Cleanup policy

Do not commit:

- Build directories
- Test262 checkouts
- Benchmark scratch output outside approved result directories
- Crash dumps
- IDE state
- Temporary generated binaries

## Pull-request gates

A compiler or runtime pull request should include:

1. Focused regression tests
2. Differential correctness where applicable
3. Benchmark evidence for performance changes
4. Documentation updates for public contracts
5. No unexplained binary-size or memory regression
