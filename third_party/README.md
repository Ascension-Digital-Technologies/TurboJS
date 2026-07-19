# Third-party dependencies

Large test suites are intentionally not vendored in release archives.

Fetch TC39 Test262 when conformance testing is needed:

```bash
python scripts/fetch_test262.py
```

The checkout is created at `third_party/test262/` and is ignored by Git.
