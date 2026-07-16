# TurboJS Test262 integration

Test262 is intentionally not vendored. Fetch the official suite first:

```powershell
python scripts/fetch_test262.py
```

TurboJS supports two reporting profiles:

- `full`: runs the complete Test262 tree and counts missing standardized features as failures.
- `core`: excludes the optional Intl402 tree and tests tagged with the `Temporal` feature. Host hooks that TurboJS cannot provide (`createRealm`, `detachArrayBuffer`, agents, and IsHTMLDDA) are explicit skips in both profiles.

```powershell
cmake --build build/test262 --target run-test262
cmake --build build/test262 --target run-test262-core
```

Direct execution:

```powershell
python scripts/test262.py --engine build/test262/turbojs.exe --suite third_party/test262 --single-variant --profile full --resume --allow-failures
python scripts/test262.py --engine build/test262/turbojs.exe --suite third_party/test262 --single-variant --profile core --resume --allow-failures
python scripts/test262_analyze.py build/test262/test262-full-report.json
```

Reports are checkpointed atomically and can be resumed. The full profile must remain the public standards-conformance number; the core profile is useful for prioritizing engine work without allowing Intl/Temporal to dominate the failure list.
