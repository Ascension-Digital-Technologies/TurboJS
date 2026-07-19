# Windows Clang CPU Detection Fix

## Problem

When TurboJS was built on Windows with Clang targeting the MSVC runtime, the AVX2 probe used:

```c
__builtin_cpu_init();
__builtin_cpu_supports("avx2");
```

Clang lowered those builtins to compiler-runtime symbols named `__cpu_indicator_init` and `__cpu_model`. They are not supplied by a normal MSVC runtime link, causing `lld-link` to fail while creating `turbojs.exe`.

## Fix

On `_WIN32`, TurboJS now performs AVX2 detection directly with Windows-compatible intrinsics:

1. Query the maximum CPUID leaf.
2. Require OSXSAVE and AVX support.
3. Read XCR0 and require operating-system preservation of XMM/YMM state.
4. Query CPUID leaf 7 and test the AVX2 feature bit.

Non-Windows Clang/GCC builds continue to use `__builtin_cpu_supports`.

The fix requires no additional library and works with the existing MSVC-runtime link.

## Warning cleanup

Three intentionally empty search loops now have explicit loop bodies, removing Clang `-Wempty-body` warnings in:

- `src/jit/optimizing/parallel_moves.c`
- `src/jit/optimizing/linear_scan.c`
- `src/jit/backend/x64/region_x64.c`

## Validation

The patched tree was configured with `TURBOJS_BUILD_WERROR=ON`, built successfully, and passed all 34 focused tests on the available Linux Clang/GCC-compatible toolchain.
