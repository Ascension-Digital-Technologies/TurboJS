# Notices and provenance

TurboJS is derived from the QuickJS family of JavaScript engines and retains the
upstream copyright and MIT permission notices present in the source files.
Significant compiler, tiering, JIT, AOT, testing, packaging, and documentation
work has been added by the TurboJS project.

The repository does not vendor Test262 in release archives. `scripts/fetch_test262.py`
retrieves it separately; Test262 remains governed by its own upstream license.

Before a public binary release, maintainers must verify the notices and licenses
of every optional dependency enabled by that package. See
`docs/project/PROVENANCE.md` for the release checklist.
