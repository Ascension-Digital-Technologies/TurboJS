# GitHub Release Checklist

## Repository

- [ ] Choose the final GitHub organization/user and repository URL.
- [ ] Confirm the default branch name.
- [ ] Confirm `LICENSE`, `NOTICE.md`, contributor policy, support policy, and security policy.
- [ ] Enable branch protection and require CI before merge.
- [ ] Enable Dependabot and secret scanning where available.

## Qualification

- [ ] Linux CI passes.
- [ ] Windows CI passes.
- [ ] macOS CI passes.
- [ ] Warnings-as-errors workflow passes.
- [ ] ASan and UBSan pass.
- [ ] ARM64 cross-qualification passes or is documented as non-blocking.
- [ ] Full Test262 result is attached to the release.
- [ ] Whole-engine benchmark is reproduced on the release host.

## Artifacts

- [ ] Source archive contains no build directory or machine-local files.
- [ ] Install and embedding smoke tests pass from a clean archive.
- [ ] Release archive SHA-256 is published.
- [ ] Release notes describe benchmark methodology and limitations.
- [ ] Tag is created only after CI completes.

## Suggested release title

`TurboJS 1.0.0 — Whole-Engine Performance Milestone`
