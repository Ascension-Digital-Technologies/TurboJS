# Portability and Platform Support

TurboJS is written primarily in C and supports multiple build and execution environments. Platform support has three layers: semantic engine portability, host/build integration, and native backend availability.

## Semantic engine

The interpreter and runtime depend on well-defined integer widths, floating-point behavior, atomics where configured, allocation APIs, and platform abstraction in build policy. Warnings and sanitizers are portability tools, not cosmetic checks.

## Native execution

JIT support additionally requires a target backend, calling-convention mapping, executable-memory implementation, instruction-cache synchronization, CPU-feature detection, unwind/debug considerations, and tests on the target ABI. Unsupported native tiers must fail closed to interpreter execution.

## WASI and WebAssembly

The WASI reactor application exposes repeatable host-call entry behavior rather than assuming a one-shot process model. WebAssembly builds may not provide native JIT capabilities and should be configured according to the platform's executable-memory model.

## Build systems

CMake is the primary project build. Meson and Makefile workflows support alternate environments. A release change affecting files, options, exported headers, or targets must keep supported build descriptions synchronized.
