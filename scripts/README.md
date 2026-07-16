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
