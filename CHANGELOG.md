# Changelog

All notable changes to TurboJS are recorded here.

## Unreleased

### Performance and hardening

- Replaced linear JIT code-cache lookup and shifting removal with open-addressed hashing.
- Added saturating tiering and feedback counters.
- Fixed signed-overflow undefined behavior in SSA constant folding.
- Added worklist-based cascading dead-value elimination.
- Hardened SSA capacity growth and AOT module size validation.
- Rejected duplicate and embedded-NUL AOT export names.
- Added code-cache stress and optimizer overflow regression coverage.
- Added `docs/status/project-audit.md` with prioritized remaining risks.


### Repository maintenance

- Prepared the repository for GitHub publication.
- Added detailed project, contribution, security, support, architecture, and subsystem documentation.
- Moved repository tooling out of `src/`.
- Consolidated the legacy API implementation under `src/api/`.
