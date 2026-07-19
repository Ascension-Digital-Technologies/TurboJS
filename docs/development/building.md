# Building TurboJS

## Recommended development build

```bash
cmake --preset jit-dev
cmake --build --preset jit-dev
ctest --preset jit-dev
```

## Full Release build

```bash
cmake --preset full-release
cmake --build --preset full-release
ctest --preset full-release
```

The cross-platform Python wrappers provide the same workflow:

```bash
python scripts/build.py --preset full-release --fresh
python scripts/test.py --preset full-release --no-build
```

See the root [README](../../README.md) for the supported toolchains and the [testing guide](testing.md) for validation workflows.
