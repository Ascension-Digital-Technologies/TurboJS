# TurboJS developer scripts

The scripts are safe to launch from any working directory and use the repository's CMake presets.

## Common commands

```bash
python3 scripts/build.py
python3 scripts/test.py
python3 scripts/benchmark.py --runs 20
python3 scripts/validate.py
python3 scripts/sanitize.py --kind address-undefined
python3 scripts/package.py --name turbojs-source
python3 scripts/clean.py
```

Windows PowerShell:

```powershell
.\scripts\run.ps1 build
.\scripts\run.ps1 test --preset jit-dev
.\scripts\run.ps1 benchmark --runs 20
```

Windows Command Prompt:

```bat
scripts\run.bat build
scripts\run.bat test --preset jit-dev
```

Unix launcher:

```bash
./scripts/run.sh build
./scripts/run.sh test --filter turbojs.jit
```

## Profiles

- `jit-dev`: fast focused JIT/AOT development and tests.
- `full-release`: complete engine, CLI, examples, and engine-level tests.

Use `--preset full-release` with `build.py`, `test.py`, or `validate.py` for complete release validation.

## Benchmark output

`benchmark.py` performs warmups, records multiple process-level timing samples, prints median/mean/minimum values, and writes machine-readable JSON to `build/benchmark-results.json` by default.

## Windows/Ninja regeneration-loop recovery

Older TurboJS build trees used CMake `CONFIGURE_DEPENDS` recursive globbing. On
some Windows Ninja installations this could repeatedly re-run CMake without
compiling. Current scripts detect those stale `build.ninja` files, remove the
affected preset directory, and configure a clean build automatically.

Manual recovery remains available:

```powershell
python scripts/build.py --preset jit-dev --fresh
```

## Test262

TC39 Test262 is fetched on demand and is not included in source archives.

```bash
python scripts/fetch_test262.py
```

```bash
python scripts/test262.py --single-variant --workers 16
python scripts/test262.py --filter built-ins/Array --workers 8
python scripts/test262.py --single-variant --shard-count 16 --shard-index 0
```

The JSON report defaults to `build/test262-report.json`. Host-dependent tests
requiring realms, agents, or ArrayBuffer detachment are explicitly skipped.

## Resumable Test262 runs

The Test262 runner captures child-process output as raw bytes and decodes it with
UTF-8 backslash replacement, so malformed or non-console byte sequences cannot
crash a Windows run. It writes atomic checkpoints every 250 executions by
default.

Resume an interrupted run with:

```powershell
python scripts\test262.py --engine build\test262\turbojs.exe --suite third_party\test262 --single-variant --resume
```

The CMake `run-test262` target enables `--resume` automatically and uses
`--allow-failures`, because conformance failures are report data rather than a
build-system failure. The final report remains available at
`build/test262-report.json`.
