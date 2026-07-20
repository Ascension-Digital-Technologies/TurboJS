# Debugging TurboJS

## Reduce the execution surface

Reproduce first in interpreter-only mode. Then enable the baseline tier, optimizing tier, OSR, and application regions one at a time. This identifies the first layer that diverges.

## Semantic failures

Capture source, bytecode, result or exception, and the smallest failing input. Compare interpreter output with native output. Check coercion order, exception state, property/prototype effects, and lifetime of temporary values.

## JIT failures

Inspect CFG and frame merges, IR verification, type and shape guards, helper-call boundaries, stack maps, register/stack locations, and deoptimization reconstruction. A crash after collection often indicates a missing root; a wrong value after bailout often indicates incorrect frame-state metadata.

## Serialization failures

Verify magic, version, total size, offsets, counts, integer overflow, and reference indices. Add a test that mutates the smallest relevant field and expects deterministic rejection.

## Tooling

Use debug assertions, CTest filtering, sanitizers, platform debuggers, disassembly, and retained benchmark inputs. Keep optimized and debug builds available because some undefined behavior appears only under optimization.

## Bug report contents

Include commit, platform, compiler, configuration/preset, exact command, minimal source or artifact, expected result, actual result, and whether interpreter-only execution reproduces the issue.
