# Native Backends

TurboJS provides target-specific lowering for x86-64 and ARM64 behind backend-neutral compiler and runtime contracts.

## Backend responsibilities

- Select machine instructions for verified IR.
- Allocate or assign registers and stack locations.
- Implement the TurboJS call and frame ABI.
- Emit branches, calls, constants, relocations, and patchable sites.
- Produce stack maps and deoptimization location records.
- Respect platform calling conventions and executable-memory rules.
- Expose unsupported operations as clean compilation failures.

## x86-64

The x64 backend handles scalar integer and floating-point lowering, call sequences, guard branches, inline-cache access, and SIMD/FMA paths where the runtime dispatch policy permits them. CPU feature use must be guarded by detection and must retain a correct fallback.

## ARM64

The ARM64 backend encodes fixed-width instructions, calling sequences, branches, floating-point operations, and NEON paths. Immediate ranges, branch distances, register classes, and ABI-preserved registers require explicit validation.

## Porting to another architecture

A new backend needs an assembler/encoder, ABI mapping, register policy, lowering coverage, relocation and executable-memory support, stack maps, runtime stubs, deoptimization materialization, feature detection, and differential tests. Begin with a small verified IR subset and interpreter fallback rather than broad unverified coverage.

## Security and correctness

Never emit executable code from unverified IR or unchecked serialized data. Bounds-check code buffers, validate relocation targets, separate writable and executable phases where supported, flush instruction caches as required, and test malformed inputs.
