# Source provenance and licensing

## Engine lineage

TurboJS contains source derived from the QuickJS ecosystem. Original copyright
and MIT license blocks are preserved in derived source files. The repository-level
`LICENSE` summarizes the governing MIT terms; file-level notices remain authoritative
for individual derived files.

## External test suites

TC39 Test262 is fetched on demand and is intentionally excluded from release
archives. Reports must record the exact revision used. The current recorded baseline
uses revision `9e61c12835c5e4a3bdba93850427e6742c4f64c4`.

## Release checklist

Before publishing a source or binary release:

1. Preserve all file-level copyright and permission notices.
2. Include `LICENSE` and `NOTICE.md`.
3. Inventory optional linked dependencies and include their required notices.
4. Confirm generated files can be reproduced from checked-in generators.
5. Confirm Test262 itself is not accidentally packaged.
6. Record compiler, platform, build options, commit, and checksums.
7. Have maintainers review the provenance report; this document is not legal advice.
