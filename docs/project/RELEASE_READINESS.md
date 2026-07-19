# TurboJS 0.16.0-rc.5 release readiness

## Release-candidate status

TurboJS is suitable for controlled evaluation, embedding experiments, compiler
research, and benchmark development. It is **not yet presented as a drop-in V8
replacement or a fully standards-complete production runtime**.

## Verified in this release-candidate pass

- Focused Release test suite
- AddressSanitizer-focused suite from Phase 99
- Install tree generation
- CMake package export and version file
- pkg-config metadata generation
- Repository Markdown-link validation
- Source archive excludes build directories and fetched Test262 data

## Recorded compatibility baseline

| Metric | Result |
|---|---:|
| Test262 revision | `9e61c12835c5e4a3bdba93850427e6742c4f64c4` |
| Pass | 42,732 |
| Fail | 2,237 |
| Timeout | 18 |
| Skip | 8,711 |
| Pass rate excluding skips | 94.99% |
| Runtime | 184.127 seconds |

A fresh full Test262 run was not executed in the Phase 100 Linux environment.
No compatibility increase is claimed beyond this recorded baseline.

## Known release blockers

- Core Test262 is below the long-term 99% target.
- ARM64 has an instruction encoder foundation, not a complete production backend.
- General arbitrary-CFG native lowering remains incomplete.
- Broader GC, exception, OOM, and long-duration stress coverage is still needed.
- Public API compatibility needs real downstream adopters before a stable 1.0 promise.

## Recommendation

Publish this artifact as a **source release candidate**. Do not label it a stable
1.0 release until the blockers above have explicit evidence and platform coverage.
