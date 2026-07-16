# Building TurboJS

TurboJS requires a C11 compiler, CMake 3.10 or newer, Python 3 for generated engine-unit checks, and a standard platform toolchain.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DTURBOJS_BUILD_TESTS=ON
cmake --build build --parallel
```

Useful hardening options inherited from the current build are ASan, UBSan, TSan, MSan, warnings-as-errors, shared-library builds, and optional libc integration.
