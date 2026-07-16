# Baseline control-flow lowering

TurboJS Phase 7 connects the engine's real branch bytecodes to the verified IR and x86-64 baseline backend.

## Supported engine instructions

- `goto`, `goto8`, `goto16`
- `if_true`, `if_true8`
- `if_false`, `if_false8`

Branch displacements are decoded using the same operand-relative convention as the VM interpreter. The frontend records bytecode offsets, emits provisional IR branches, and resolves each target after translation.

## Stack-state rule

The baseline frontend currently requires the operand stack to be empty at every control-flow boundary after a condition is consumed. Values that survive a branch or loop iteration must be stored in locals.

This restriction is deliberate. It provides deterministic verification and supports normal compiler-generated loops without pretending that the IR already has SSA phi nodes or general stack merging.

Unsupported stack merges return `TURBOJS_IR_UNSUPPORTED` and remain in the interpreter.

## Native lowering

- `branch.true` lowers to `test` plus `jnz`.
- `branch.false` lowers to `test` plus `jz`.
- `jump` lowers to a relative `jmp`.
- Forward and backward targets use post-emission displacement fixups.

## Next safety step

Arithmetic currently uses signed 64-bit baseline operations. The VM-integrated tier must add checked int32 overflow exits before multiplication and broader arithmetic can be enabled for automatic JavaScript tiering.
