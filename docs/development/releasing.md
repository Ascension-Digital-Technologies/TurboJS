# Release process

1. Start from a clean checkout and update `CHANGELOG.md`.
2. Configure a Release build with warnings enabled.
3. Run the focused suite and architecture validation.
4. Run ASan and UBSan where supported.
5. Run the pinned Test262 profile and archive its JSON report.
6. Install into an empty staging prefix and compile a downstream consumer.
7. Validate Markdown links and generated files.
8. Create a source archive excluding build trees, Test262, caches, and binaries.
9. Produce SHA-256 checksums.
10. Review `LICENSE`, `NOTICE.md`, and `docs/project/PROVENANCE.md`.
